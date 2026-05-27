#include "sim_runtime.h"
#include <Arduino.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
    using clock_type = std::chrono::steady_clock;
    std::atomic<int>         g_should_quit{0};
    clock_type::time_point   g_start;
}

extern "C" {

void sim_runtime_init(int /*argc*/, char** /*argv*/) {
    g_should_quit.store(0);
    g_start = clock_type::now();
}

void sim_runtime_shutdown(void) {
    // Nothing yet.
}

void sim_pump_events(void) {
    // Wired in Task 9.
}

int sim_should_quit(void) {
    return g_should_quit.load();
}

void sim_log(const char* msg) {
    std::fprintf(stderr, "[boardghost] %s\n", msg);
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

} // extern "C"
