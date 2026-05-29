#include "sim_runtime.h"
#include <Arduino.h>
#include <SDL.h>
#include <LovyanGFX.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
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
    clock_type::time_point   g_start;

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
    // Background render thread driving LovyanGFX's Panel_sdl::loop. Sketches
    // that don't use Panel_sdl::main (we use our own sim_main) still need its
    // OUT semaphore to be posted so per-pixel writes in drawJpgFile etc. don't
    // each wait 1ms for nothing (153K pixels × 1ms ≈ 150s of pointless sleep).
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

extern "C" {

void sim_runtime_init(int /*argc*/, char** /*argv*/) {
    g_should_quit.store(0);
    g_screenshot_done.store(0);
    g_start = clock_type::now();
    for (auto& p : g_pins) p = PinState{};
    load_analog_env_overrides();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[boardghost] SDL_Init failed: %s\n", SDL_GetError());
    }
    std::signal(SIGUSR1, sigusr1_handler);
    lgfx::Panel_sdl::setup();
    // Start background watcher thread for screenshot capture.
    // This runs independently of the sketch so screenshots fire even when the
    // main thread is blocked in drawing calls (e.g. calibrateTouch).
    std::thread(screenshot_watcher_thread).detach();
}

void sim_runtime_shutdown(void) {
    lgfx::Panel_sdl::close();
    SDL_Quit();
}

void sim_pump_events(void) {
    // Pump the OS event queue so any pending events are visible.
    SDL_PumpEvents();
    // Extract ONLY SDL_QUIT events; leave mouse/keyboard/window events in
    // the queue so LVGL's input drivers (and any other consumer) can see them.
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
    auto d = clock_type::now() - g_start;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

uint32_t micros(void) {
    auto d = clock_type::now() - g_start;
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

} // extern "C"
