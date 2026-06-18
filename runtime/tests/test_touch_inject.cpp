// Injected-touch round-trip: a screen-space tap pushed via
// boardghost_inject_touch must come back out of lcd.getTouch() as the SAME
// screen coordinates, at any rotation (the de-rotate then convertRawXY
// re-rotate cancel). Unlike the live-mouse path this needs no SDL mouse state,
// so it asserts real values.
//
// The injection lives in Panel_sdl_bg (the panel the lgfx_codemod generates for
// real sketches), so this test backs its device with Panel_sdl_bg + Touch_sdl
// rather than the plain-Panel_sdl fixture boards.
#include <gtest/gtest.h>
#include "sim_runtime.h"
#include "sim_touch_inject.h"
#include "../displays/Panel_sdl_bg.hpp"
#include "../displays/Touch_sdl.hpp"
#include <LovyanGFX.hpp>

#include <SDL.h>
#include <cstdint>
#include <cstdlib>

namespace {
// Minimal Panel_sdl_bg-backed device mirroring LGFX_ILI9488_SDL's config.
class LGFX_BG_Test : public lgfx::LGFX_Device {
public:
    LGFX_BG_Test() {
        auto cfg = panel_.config();
        cfg.memory_width    = 480;
        cfg.memory_height   = 320;
        cfg.panel_width     = 480;
        cfg.panel_height    = 320;
        cfg.offset_x        = 0;
        cfg.offset_y        = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
        panel_.setTouch(&touch_);
    }
private:
    Panel_sdl_bg panel_;
    Touch_sdl    touch_;
};
}  // namespace

class TouchInject : public ::testing::Test {
protected:
    void SetUp() override {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        sim_runtime_init(0, nullptr);
    }
    void TearDown() override { sim_runtime_shutdown(); }
};

TEST_F(TouchInject, ScreenSpaceTapRoundTripsAtRotation0) {
    LGFX_BG_Test tft;
    tft.init();
    tft.setRotation(0);
    boardghost_inject_touch(100, 50, /*screen_space=*/1);
    int32_t x = 0, y = 0;
    ASSERT_TRUE(tft.getTouch(&x, &y));
    EXPECT_EQ(x, 100);
    EXPECT_EQ(y, 50);
}

TEST_F(TouchInject, ScreenSpaceTapRoundTripsAtRotation1) {
    LGFX_BG_Test tft;
    tft.init();
    tft.setRotation(1);  // dims swap; screen coords must still survive
    boardghost_inject_touch(120, 30, /*screen_space=*/1);
    int32_t x = 0, y = 0;
    ASSERT_TRUE(tft.getTouch(&x, &y));
    EXPECT_EQ(x, 120);
    EXPECT_EQ(y, 30);
}

TEST_F(TouchInject, ExpiredTapReadsAsNoTouch) {
    LGFX_BG_Test tft;
    tft.init();
    boardghost_inject_touch(10, 10, /*screen_space=*/1);
    SDL_Delay(160);  // outlast the ~120ms press window
    int32_t x = 0, y = 0;
    EXPECT_FALSE(tft.getTouch(&x, &y));
}
