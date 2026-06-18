// SDL audio backend for the ESP32 LEDC PWM shim. Sketches like ckb_pos wire
// a passive piezo to a GPIO, drive it with ledcWriteTone(channel, freq) +
// ledcWrite(channel, duty), and expect short tones for UI feedback. We
// recreate that on the host: each active LEDC channel becomes a square-wave
// oscillator that mixes into a single SDL audio stream, so when the sketch
// calls playTune() you actually hear it.
//
// Design notes:
//   - One audio device opened lazily on the first tone — paying the SDL_Init
//     for audio at program start would slow non-audio sketches.
//   - State protected by a mutex because the audio callback runs on SDL's
//     audio thread while sketch code calls writeTone/write on its main loop.
//   - 16 channels: matches the ESP32's LEDC peripheral.
//   - Square waves only. Real LEDC at high duty cycles is closer to PWM
//     than square wave, but for a piezo's resonant response this maps to
//     the same audible behaviour.
//   - BOARDGHOST_SOUND=off skips SDL audio init entirely; the shim then
//     becomes silent (sketch keeps booting as if it had no speaker).

#include "spsc_ring.h"

#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

constexpr int      kSampleRate = 44100;
constexpr int      kNumChannels = 16;
constexpr int      kBufferSamples = 1024;

struct LedcChannel {
    double   freq_hz       = 0.0;
    double   duty_fraction = 0.0;  // 0..1
    double   phase         = 0.0;  // accumulator in radians
};

LedcChannel        g_channels[kNumChannels];
std::mutex         g_mutex;
SDL_AudioDeviceID  g_dev = 0;

// Mirror tap: the audio callback (sole producer) pushes every mixed sample;
// the /mirror/audio HTTP thread (sole consumer) drains. ~0.74s at 44.1kHz mono
// — enough to absorb HTTP scheduling jitter without unbounded latency.
boardghost::SpscRing<int16_t, 1u << 15> g_audio_ring;
std::atomic<bool>  g_init_attempted{false};
std::atomic<bool>  g_init_failed{false};

bool disabled() {
    const char* env = std::getenv("BOARDGHOST_SOUND");
    return env && (std::strcmp(env, "off") == 0 || std::strcmp(env, "0") == 0);
}

void audio_callback(void* /*ud*/, Uint8* stream, int len) {
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    int samples = len / (int)sizeof(int16_t);
    // Snapshot channel state under the lock so the audio thread doesn't see
    // a torn update from the sketch thread changing freq + duty at once.
    LedcChannel snap[kNumChannels];
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        std::memcpy(snap, g_channels, sizeof(snap));
        for (int c = 0; c < kNumChannels; ++c) {
            // Advance the persistent phase too so the next callback's snap
            // continues where this one left off — avoids clicks at the
            // boundary between callbacks.
            g_channels[c].phase = snap[c].phase;
        }
    }
    for (int i = 0; i < samples; ++i) {
        double mixed = 0.0;
        int active = 0;
        for (int c = 0; c < kNumChannels; ++c) {
            auto& ch = snap[c];
            if (ch.freq_hz <= 0.0 || ch.duty_fraction <= 0.0) continue;
            ++active;
            // Triangle wave — has fewer high harmonics than a square wave
            // so it sounds noticeably less harsh through laptop speakers
            // while still conveying the same "piezo bleep" character.
            // Real ESP32 piezos at PWM-driven frequencies sound more like
            // a square wave, but the host audio path is reproducing a sound
            // through hi-fi drivers, where square waves accentuate the
            // ringing that real piezos physically can't produce.
            double phase_norm = ch.phase / (2.0 * M_PI);
            double tri = (phase_norm < 0.5)
                ? (4.0 * phase_norm - 1.0)
                : (3.0 - 4.0 * phase_norm);
            // Duty is a gate (>0 = on, 0 = off), NOT a linear amplitude
            // scaler. Real sketches use tiny duty values (2 of 255) as
            // "play this tone" because on a piezo the average voltage
            // barely matters — the piezo responds to the transitions, not
            // the DC level. Scaling amplitude by duty would make
            // ledcWrite(0, 2) essentially silent through the host's
            // speakers, which was the regression that prompted this code:
            // before scaling, the user heard playTune; after, they didn't.
            mixed += tri;
            ch.phase += 2.0 * M_PI * ch.freq_hz / kSampleRate;
            if (ch.phase >= 2.0 * M_PI) ch.phase -= 2.0 * M_PI;
        }
        // Average across active channels so adding a second tone doesn't
        // double the volume. 18% master volume — louder than 12% (which
        // turned out to be too quiet for the triangle-wave + low-amplitude
        // combination) but still safe for long debug sessions.
        if (active > 0) mixed = (mixed / active) * 0.18;
        int sample = (int)(mixed * 32767);
        sample = std::clamp(sample, -32767, 32767);
        out[i] = (int16_t)sample;
        // Tap for the mirror. Lock-free by contract (see g_audio_ring) so the
        // real-time audio thread never blocks; drops silently when no consumer
        // is draining (ring full).
        g_audio_ring.push(out[i]);
    }
    // Persist the advanced phases back so the next callback continues
    // smoothly. We do this outside the audio loop above to minimize
    // lock-held time.
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        for (int c = 0; c < kNumChannels; ++c) {
            g_channels[c].phase = snap[c].phase;
        }
    }
}

void ensure_audio_init() {
    if (g_init_attempted.exchange(true)) return;
    if (disabled()) {
        g_init_failed.store(true);
        return;
    }
    // SDL_Init for video is already done elsewhere; init audio additively.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr,
            "[boardghost] audio: SDL_InitSubSystem(AUDIO) failed: %s — "
            "LEDC tones will be silent. Set BOARDGHOST_SOUND=off to suppress.\n",
            SDL_GetError());
        g_init_failed.store(true);
        return;
    }
    SDL_AudioSpec want{}, have{};
    want.freq     = kSampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = kBufferSamples;
    want.callback = audio_callback;
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                                SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (g_dev == 0) {
        std::fprintf(stderr,
            "[boardghost] audio: SDL_OpenAudioDevice failed: %s — LEDC tones "
            "will be silent.\n", SDL_GetError());
        g_init_failed.store(true);
        return;
    }
    SDL_PauseAudioDevice(g_dev, 0);  // start running
    std::fprintf(stderr, "[boardghost] audio: SDL audio ready (%dHz mono)\n",
                 have.freq);
}

}  // namespace

extern "C" {

void boardghost_ledc_set_tone(uint8_t channel, double freq_hz) {
    if (channel >= kNumChannels) return;
    ensure_audio_init();
    if (g_init_failed.load()) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    g_channels[channel].freq_hz = freq_hz > 0.0 ? freq_hz : 0.0;
}

// Force the audio device open even if the sketch hasn't played a tone yet, so
// the mirror audio stream produces a steady sample cadence (silence when idle)
// instead of nothing. No-op when BOARDGHOST_SOUND=off (stays silent).
void boardghost_audio_ensure_started(void) {
    ensure_audio_init();
}

// Consumer side of the mirror tap. Copies up to `max` samples into dst, returns
// the count actually available (0 when the ring is empty / audio is disabled).
size_t boardghost_audio_drain(int16_t* dst, size_t max) {
    if (!dst || max == 0) return 0;
    return g_audio_ring.drain(dst, max);
}

void boardghost_ledc_set_duty(uint8_t channel, uint32_t duty, uint8_t max_bits) {
    if (channel >= kNumChannels) return;
    ensure_audio_init();
    if (g_init_failed.load()) return;
    double max_val = (double)((1u << max_bits) - 1);
    if (max_val <= 0.0) max_val = 255.0;
    double frac = (double)duty / max_val;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    std::lock_guard<std::mutex> lk(g_mutex);
    g_channels[channel].duty_fraction = frac;
}

}  // extern "C"
