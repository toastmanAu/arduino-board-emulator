#include <gtest/gtest.h>
#include "sim_runtime.h"
#include "LGFX_ILI9341_SDL.hpp"
#include "LGFX_ST7789_SDL.hpp"
#include "LGFX_ST7796_SDL.hpp"
#include "LGFX_GC9A01_SDL.hpp"
#include "LGFX_ST7735_SDL.hpp"
#include <cstdlib>

namespace {
struct PanelCase {
    const char* name;
    int width;
    int height;
};

class ExtraDisplayTest : public ::testing::TestWithParam<PanelCase> {
protected:
    void SetUp() override {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        sim_runtime_init(0, nullptr);
    }
    void TearDown() override { sim_runtime_shutdown(); }
};

template <typename TFT>
void exercise(const PanelCase& c) {
    TFT tft;
    tft.init();
    EXPECT_EQ(tft.width(),  c.width);
    EXPECT_EQ(tft.height(), c.height);
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, c.width / 4, c.height / 4, TFT_RED);
    // No assertion on pixel value (different panels handle colour ordering
    // differently). The point is: no crash, init succeeds, dimensions match.
}

}  // namespace

TEST_F(ExtraDisplayTest, ILI9341)  { exercise<LGFX_ILI9341_SDL>({"ILI9341", 320, 240}); }
TEST_F(ExtraDisplayTest, ST7789)   { exercise<LGFX_ST7789_SDL> ({"ST7789",  240, 320}); }
TEST_F(ExtraDisplayTest, ST7796)   { exercise<LGFX_ST7796_SDL> ({"ST7796",  480, 320}); }
TEST_F(ExtraDisplayTest, GC9A01)   { exercise<LGFX_GC9A01_SDL> ({"GC9A01",  240, 240}); }
TEST_F(ExtraDisplayTest, ST7735)   { exercise<LGFX_ST7735_SDL> ({"ST7735",  160, 128}); }
