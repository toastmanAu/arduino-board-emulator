#include "sim_runtime.h"
#include <Arduino.h>
#include <SDL.h>
#include <LovyanGFX.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
    using clock_type = std::chrono::steady_clock;
    std::atomic<int>         g_should_quit{0};
    std::atomic<void*>       g_active_display{nullptr};
    std::atomic<int>         g_screenshot_requested{0};
    std::atomic<int>         g_screenshot_done{0};  // set by watcher thread after taking screenshot
    // Sketches commonly initialise globals as `long x = millis() - 300000`
    // (5 minutes ago) or `long y = millis() - 900000` (15 min ago) so that
    // their first staleness check `if (millis() - x > 300000)` immediately
    // fires. On ESP32 `long` is 32-bit and the underflow wraps cleanly to a
    // negative-looking value that arithmetic recovers from. On 64-bit Linux
    // `long` is 64-bit, so the uint32_t underflow gets zero-extended into a
    // huge positive number and the staleness check is broken forever.
    //
    // The simplest fix that doesn't touch user code: seed our clock so
    // millis() at first call returns a value larger than the largest
    // millisecond constant sketches subtract. 1 hour covers every realistic
    // timeout interval we've seen.
    constexpr auto kMillisOffset = std::chrono::hours(1);

    // Use Meyers' singleton for the start timestamp. Static init order
    // across translation units is undefined, so a plain `g_start` here
    // might still be default-constructed (epoch) when user-global code
    // like `long LAST_TOUCH = millis();` runs. Function-local static
    // guarantees initialisation on first access, no matter which TU's
    // global ctors run first.
    clock_type::time_point& g_start() {
        static clock_type::time_point start = clock_type::now() - kMillisOffset;
        return start;
    }

    struct PinState {
        uint8_t mode    = 0;   // INPUT
        uint8_t digital = 0;
        int     analog  = 0;
    };

    constexpr size_t MAX_PINS = 64;
    PinState g_pins[MAX_PINS];

    void sigusr1_handler(int /*sig*/) {
        // Signal-safe: just flip a flag. The background watcher thread handles
        // the actual screenshot so the main thread doesn't need to be unblocked.
        g_screenshot_requested.store(1);
    }

    // Background watcher thread: takes screenshots without needing the main thread.
    // This decouples screenshot capture from sketch execution — the sketch can be
    // blocked in calibrateTouch or a long drawing operation and the screenshot
    // still fires on schedule.
    void screenshot_watcher_thread() {
        while (!g_should_quit.load()) {
            if (g_screenshot_requested.exchange(0) == 1) {
                // Wait briefly for display to become registered if it isn't yet
                for (int retries = 0; retries < 50 && !g_active_display.load(); ++retries) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (void* dev = g_active_display.load()) {
                    const char* path = std::getenv("BOARDGHOST_SCREENSHOT_PATH");
                    if (!path) path = "/tmp/boardghost-screenshot.png";
                    std::fprintf(stderr,
                        "[boardghost] screenshot_watcher: taking screenshot → %s (display=%p)\n",
                        path, dev);
                    // Small extra settle delay so the frame is rendered
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    int rc = sim_screenshot(path, dev);
                    std::fprintf(stderr,
                        "[boardghost] screenshot_watcher: sim_screenshot rc=%d\n", rc);
                    g_screenshot_done.store(1);
                } else {
                    std::fprintf(stderr,
                        "[boardghost] screenshot_watcher: no display registered, screenshot skipped\n");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void load_analog_env_overrides() {
        for (size_t p = 0; p < MAX_PINS; ++p) {
            char key[32];
            std::snprintf(key, sizeof(key), "BOARDGHOST_ANALOG_%zu", p);
            if (const char* v = std::getenv(key)) {
                g_pins[p].analog = std::atoi(v);
            }
        }
    }
}

// SDL event watch: fires the instant any event is added to the queue (via
// SDL_PushEvent or SDL_PumpEvents pulling from the OS), BEFORE anything polls
// it. We need this because LovyanGFX's Panel_sdl::_event_proc() drains the
// whole queue with SDL_PollEvent every loop() and consumes SDL_QUIT itself —
// so a window-close quit would never reach sim_pump_events()'s own peek once
// a panel is set up. The watch records the quit regardless of who consumes it.
static int SDLCALL quit_event_watch(void* /*userdata*/, SDL_Event* event) {
    if (event && event->type == SDL_QUIT) {
        g_should_quit.store(1);
    }
    return 0;  // return value is ignored for watchers (only filters use it)
}

extern "C" {

void sim_runtime_init(int /*argc*/, char** /*argv*/) {
    g_should_quit.store(0);
    g_screenshot_done.store(0);
    // Do NOT reset g_start here — it's initialised at static-init time so
    // that millis() in user global constructors returns small values
    // consistent with millis() in setup()/loop().
    for (auto& p : g_pins) p = PinState{};
    load_analog_env_overrides();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[boardghost] SDL_Init failed: %s\n", SDL_GetError());
    }
    // Record SDL_QUIT before Panel_sdl::_event_proc() can drain it (see watch).
    SDL_AddEventWatch(quit_event_watch, nullptr);
    std::signal(SIGUSR1, sigusr1_handler);
    lgfx::Panel_sdl::setup();
    // Start background watcher thread for screenshot capture.
    // This runs independently of the sketch so screenshots fire even when the
    // main thread is blocked in drawing calls (e.g. calibrateTouch).
    std::thread(screenshot_watcher_thread).detach();
}

void sim_runtime_shutdown(void) {
    SDL_DelEventWatch(quit_event_watch, nullptr);
    lgfx::Panel_sdl::close();
    SDL_Quit();
}

void sim_pump_events(void) {
    // Drive Panel_sdl's update loop from the main thread. This is what
    // creates the SDL_Window the first time a Panel_sdl monitor is
    // registered, and what swaps the back buffer to the visible window each
    // frame thereafter. Without it, pure-LovyanGFX sketches (i.e. anything
    // not using LVGL's own SDL backend) render to an in-memory framebuffer
    // that's only ever visible via the screenshot watcher — interactive
    // mode shows no window at all. Cost: up to 1ms per pump (internal
    // SDL_SemWaitTimeout). An earlier render_thread was tried in a
    // background thread but caused window-creation lock contention under
    // Wayland; running Panel_sdl::loop on the main thread fixes both.
    lgfx::Panel_sdl::loop();

    // Pump the OS event queue so any pending events are visible. The
    // quit_event_watch installed in init() is the primary SDL_QUIT detector
    // (it sees the event even when Panel_sdl::_event_proc drains it first).
    SDL_PumpEvents();
    // Secondary path: if no panel is registered, Panel_sdl::loop() above
    // early-returns without draining, so extract any SDL_QUIT here too. We
    // leave mouse/keyboard/window events in the queue so LVGL's input drivers
    // (and any other consumer) can still see them.
    SDL_Event events[8];
    int n = SDL_PeepEvents(events, 8, SDL_GETEVENT, SDL_QUIT, SDL_QUIT);
    if (n > 0) g_should_quit.store(1);
    // Screenshots are handled by the background watcher thread (screenshot_watcher_thread).
    // No screenshot logic needed here.
}

int sim_should_quit(void) {
    return g_should_quit.load();
}

void sim_log(const char* msg) {
    std::fprintf(stderr, "[boardghost] %s\n", msg);
}

static const char* mode_name(uint8_t mode) {
    switch (mode) {
        case INPUT:        return "INPUT";
        case OUTPUT:       return "OUTPUT";
        case INPUT_PULLUP: return "INPUT_PULLUP";
        default:           return "OTHER";
    }
}

void sim_log_gpio_mode(uint8_t pin, uint8_t mode) {
    std::fprintf(stderr, "[gpio] mode %u %s\n", (unsigned)pin, mode_name(mode));
}

void sim_log_gpio_write(uint8_t pin, uint8_t value) {
    std::fprintf(stderr, "[gpio] write %u %u\n", (unsigned)pin, (unsigned)(value ? 1 : 0));
}

void sim_log_gpio_pwm(uint8_t pin, int value) {
    std::fprintf(stderr, "[gpio] pwm %u %d\n", (unsigned)pin, value);
}

uint32_t millis(void) {
    auto d = clock_type::now() - g_start();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

uint32_t micros(void) {
    auto d = clock_type::now() - g_start();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(d).count());
}

void delay(uint32_t ms) {
    // Pump SDL events during delays so long setup() phases (e.g. JPG decode
    // + render, touch calibration) keep the SDL renderer alive. Without this,
    // sketches that call delay() in setup before reaching the main loop can
    // wedge Panel_sdl waiting for an event drain that never happens.
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(ms);
    while (true) {
        sim_pump_events();
        auto now = clock::now();
        if (now >= deadline) break;
        auto remaining = deadline - now;
        auto step = std::min<std::chrono::nanoseconds>(
            remaining, std::chrono::milliseconds(10));
        std::this_thread::sleep_for(step);
    }
}

void delayMicroseconds(uint32_t us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void pinMode(uint8_t pin, uint8_t mode) {
    if (pin >= MAX_PINS) return;
    g_pins[pin].mode = mode;
    sim_log_gpio_mode(pin, mode);
}

void digitalWrite(uint8_t pin, uint8_t value) {
    if (pin >= MAX_PINS) return;
    g_pins[pin].digital = value ? 1 : 0;
    sim_log_gpio_write(pin, value);
}

int digitalRead(uint8_t pin) {
    if (pin >= MAX_PINS) return 0;
    return g_pins[pin].digital;
}

int analogRead(uint8_t pin) {
    if (pin >= MAX_PINS) return 0;
    return g_pins[pin].analog;
}

void analogWrite(uint8_t pin, int value) {
    if (pin >= MAX_PINS) return;
    g_pins[pin].analog = value;
    sim_log_gpio_pwm(pin, value);
}

void sim_set_active_display(void* lgfx_device) {
    g_active_display.store(lgfx_device);
    std::fprintf(stderr, "[boardghost] sim_set_active_display: device=%p\n", lgfx_device);
}

void* sim_get_active_display(void) {
    return g_active_display.load();
}

// M2.D — NTP/timezone shims. Sketches that call configTime() expect time(),
// localtime_r() to thereafter report the configured local time. On host we
// rely on the system clock; we set the TZ env var so localtime applies the
// offset, but ignore the NTP server (we have no NTP daemon to talk to).
void configTime(long gmtOffset_sec, int /*daylightOffset_sec*/,
                const char* /*server1*/,
                const char* /*server2*/,
                const char* /*server3*/) {
    char tz[64];
    long hours   = -gmtOffset_sec / 3600;
    long minutes = (-gmtOffset_sec % 3600) / 60;
    std::snprintf(tz, sizeof(tz), "UTC%+ld:%02ld", hours, minutes);
    setenv("TZ", tz, 1);
    tzset();
}

bool getLocalTime(struct tm* info, uint32_t /*ms*/) {
    if (!info) return false;
    time_t now = time(nullptr);
    localtime_r(&now, info);
    return true;
}

// esp_random — ESP32 hardware RNG. The sim uses rand() which is seeded by
// sim_main.cpp at startup; sketches that gate retry timing or jitter on this
// get a different sequence per run without configuration.
uint32_t esp_random(void) {
    // Combine two rand() calls to fill 32 bits since RAND_MAX is typically
    // 0x7fffffff on Linux — leaves the high bit zero otherwise.
    return (uint32_t)rand() ^ ((uint32_t)rand() << 16);
}

// ESP32 LEDC PWM API. The real chip drives a PWM signal at the configured
// frequency/duty; sketches commonly wire this to a passive piezo and play
// UI feedback tones. We re-create that on the host by treating each LEDC
// channel as a square-wave generator and mixing all active channels into a
// single SDL audio stream — the host's speakers become the sketch's piezo.
//
// Set BOARDGHOST_SOUND=off to disable (e.g. CI, shared workspaces).
extern void boardghost_ledc_set_tone(uint8_t channel, double freq_hz);
extern void boardghost_ledc_set_duty(uint8_t channel, uint32_t duty, uint8_t max_bits);

void ledcSetup(uint8_t channel, double freq, uint8_t res_bits) {
    // Treat setup as "set the frequency and reset duty to 0". Sketches that
    // call setup then writeTone get correctly re-initialised.
    boardghost_ledc_set_tone(channel, freq);
    boardghost_ledc_set_duty(channel, 0, res_bits);
}
void ledcAttachPin(uint8_t /*pin*/, uint8_t /*channel*/)               {}
void ledcDetachPin(uint8_t /*pin*/)                                    {}
void ledcWrite(uint8_t channel, uint32_t duty) {
    // The ESP32 res_bits is typically 8 — duty 0..255. We don't know it
    // here (no separate setup state cached), so assume 8 and clamp at
    // 0..255; the audio backend just needs a "duty>0 → on" signal anyway.
    boardghost_ledc_set_duty(channel, duty, 8);
}
void ledcWriteTone(uint8_t channel, double freq) {
    boardghost_ledc_set_tone(channel, freq);
}
void ledcWriteNote(uint8_t channel, uint8_t note, uint8_t octave) {
    // Equal temperament: note = semitones from A4 (440 Hz). ESP32 numbers
    // notes 0..11 = C..B, so semitones from A is (note - 9) and octave
    // shift is 2^(octave-4) relative to A4.
    int semitones = (int)note - 9 + ((int)octave - 4) * 12;
    double freq = 440.0 * std::pow(2.0, semitones / 12.0);
    boardghost_ledc_set_tone(channel, freq);
}
uint32_t ledcRead(uint8_t /*channel*/) { return 0; }

} // extern "C"
