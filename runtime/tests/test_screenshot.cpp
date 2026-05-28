#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include "LGFX_ILI9488_SDL.hpp"
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

TEST(Screenshot, WritesPngFile) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    sim_runtime_init(0, nullptr);

    LGFX_ILI9488_SDL tft;
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(10, 10, 50, 50, TFT_RED);

    auto path = fs::temp_directory_path() / "boardghost-test.png";
    if (fs::exists(path)) fs::remove(path);

    int rc = sim_screenshot(path.c_str(), &tft);
    EXPECT_EQ(rc, 0);
    ASSERT_TRUE(fs::exists(path));
    EXPECT_GT(fs::file_size(path), 100u);   // at least a PNG header + minimal data

    fs::remove(path);
    sim_runtime_shutdown();
}
