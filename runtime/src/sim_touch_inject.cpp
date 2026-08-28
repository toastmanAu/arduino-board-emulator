// Runtime touch injection backend. See sim_touch_inject.h.
//
// One pending tap, guarded by a mutex (the lock is uncontended — one HTTP
// producer, one sketch-thread consumer — and getTouchRaw is not real-time the
// way the audio callback is, so a mutex here is fine; the lock-free constraint
// was specific to sim_audio).
#include <cstdint>
#include "sim_touch_inject.h"

#include <SDL.h>
#include <mutex>

namespace {

// How long an injected tap reads as "pressed". Long enough for a few
// getTouchRaw cycles (a sketch debounces / waits for press→release), short
// enough to auto-release into a clean tap without a separate release call.
constexpr uint32_t kPressWindowMs = 120;

struct PendingTap {
    int      x = 0;
    int      y = 0;
    bool     screen_space = true;
    uint32_t expiry_ms = 0;
    bool     active = false;
};

std::mutex g_mtx;
PendingTap g_tap;

}  // namespace

extern "C" void boardghost_inject_touch(int x, int y, int screen_space) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_tap.x = x;
    g_tap.y = y;
    g_tap.screen_space = (screen_space != 0);
    g_tap.expiry_ms = SDL_GetTicks() + kPressWindowMs;
    g_tap.active = true;
}

bool boardghost_pending_touch(int& x, int& y, bool& screen_space) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_tap.active) return false;
    if (SDL_GetTicks() >= g_tap.expiry_ms) {
        g_tap.active = false;  // window elapsed → release
        return false;
    }
    x = g_tap.x;
    y = g_tap.y;
    screen_space = g_tap.screen_space;
    return true;
}
