#include <gtest/gtest.h>
#include "sim_runtime.h"
#include "LGFX_SSD1306_SDL.hpp"
#include <vector>
#include <cstdlib>
#include <cstdio>

TEST(SSD1306Panel, FillAndDrawLine) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    sim_runtime_init(0, nullptr);

    LGFX_SSD1306_SDL oled;
    oled.init();
    oled.fillScreen(TFT_BLACK);
    oled.drawLine(0, 0, 127, 63, TFT_WHITE);

    // Read back via LovyanGFX's readRect into RGB565 buffer.
    // Panel_FrameBufferBase implements readRect() directly from its line
    // buffer, so this works without a real display.
    std::vector<uint16_t> pixels(128 * 64);
    oled.readRect(0, 0, 128, 64, pixels.data());

    // Count "lit" pixels (non-black) — RGB565 black is 0x0000.
    int set = 0;
    for (int i = 0; i < 128 * 64; ++i) {
        if (pixels[i] != 0) set++;
    }
    std::printf("ssd1306 lit count = %d\n", set);

    // A Bresenham line from (0,0) to (127,63) on a 128x64 grid lights
    // approximately 128 pixels (observed: ~128). Allow ±40% margin.
    EXPECT_GE(set, 77);
    EXPECT_LE(set, 180);

    sim_runtime_shutdown();
}
