#include "sim_runtime.h"
#include <atomic>
#include <cstdio>

namespace {
    std::atomic<int> g_should_quit{0};
}

extern "C" {

void sim_runtime_init(int /*argc*/, char** /*argv*/) {
    g_should_quit.store(0);
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

} // extern "C"
