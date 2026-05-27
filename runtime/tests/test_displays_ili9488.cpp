#include <gtest/gtest.h>
#include "sim_runtime.h"
#include "LGFX_ILI9488_SDL.hpp"
#include <SDL.h>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace {

uint64_t fnv1a(const uint8_t* data, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

}

TEST(ILI9488Panel, DrawsRedRectangleReproducibly) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    sim_runtime_init(0, nullptr);

    // Panel_sdl::setup() creates the semaphores used by the lock_t RAII
    // guard inside every draw call. Without this the lock destructor calls
    // SDL_SemPost(nullptr) and crashes.
    lgfx::Panel_sdl::setup();

    LGFX_ILI9488_SDL tft;
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(10, 10, 100, 50, TFT_RED);

    // Read back via LovyanGFX's readRect into RGB565 buffer.
    // Panel_FrameBufferBase implements readRect() directly from its line
    // buffer, so this works without a real display or GPU.
    std::vector<uint16_t> pixels(480 * 320);
    tft.readRect(0, 0, 480, 320, pixels.data());

    uint64_t h = fnv1a(reinterpret_cast<uint8_t*>(pixels.data()),
                       pixels.size() * sizeof(uint16_t));

    std::printf("ILI9488 red rect hash = 0x%016lx\n", h);
    EXPECT_EQ(h, 0xa6ba9d5a4e64aac3ULL);

    lgfx::Panel_sdl::close();
    sim_runtime_shutdown();
}
