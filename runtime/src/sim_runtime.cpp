#include "sim_runtime.h"
#include <Arduino.h>
#include <SDL.h>
#include <LovyanGFX.hpp>
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
    clock_type::time_point   g_start;

    struct PinState {
        uint8_t mode    = 0;   // INPUT
        uint8_t digital = 0;
        int     analog  = 0;
    };

    constexpr size_t MAX_PINS = 64;
    PinState g_pins[MAX_PINS];

    void sigusr1_handler(int /*sig*/) {
        // Signal-safe: just flip a flag. Real work happens in sim_pump_events.
        g_screenshot_requested.store(1);
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
    g_start = clock_type::now();
    for (auto& p : g_pins) p = PinState{};
    load_analog_env_overrides();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[boardghost] SDL_Init failed: %s\n", SDL_GetError());
    }
    std::signal(SIGUSR1, sigusr1_handler);
    lgfx::Panel_sdl::setup();
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

    if (g_screenshot_requested.exchange(0) == 1) {
        if (void* dev = g_active_display.load()) {
            const char* path = std::getenv("BOARDGHOST_SCREENSHOT_PATH");
            if (!path) path = "/tmp/boardghost-screenshot.png";
            sim_screenshot(path, dev);
        }
    }
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
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
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
}

} // extern "C"
