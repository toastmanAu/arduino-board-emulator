#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include "sim_capture.h"
#include "LGFX_ILI9488_SDL.hpp"
#include <cstdlib>
#include <vector>

// capture_active_rgb565 reads back the framebuffer of the registered display
// at full dimensions, with pixels matching what was drawn.
TEST(Capture, ActiveDisplayReturnsFramebufferDimsAndPixels) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    sim_runtime_init(0, nullptr);

    LGFX_ILI9488_SDL tft;
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(10, 10, 50, 50, TFT_RED);
    sim_set_active_display(&tft);

    std::vector<uint16_t> buf;
    int w = 0, h = 0;
    bool ok = boardghost::capture_active_rgb565(buf, w, h);

    ASSERT_TRUE(ok);
    EXPECT_EQ(w, tft.width());
    EXPECT_EQ(h, tft.height());
    ASSERT_EQ(buf.size(), static_cast<size_t>(w) * static_cast<size_t>(h));

    // (20,20) is inside the red rect; (0,0) is the black background. Red is
    // extracted the same way sim_screenshot does: (v >> 11) & 0x1F.
    uint16_t red_px   = buf[static_cast<size_t>(20) * w + 20];
    uint16_t black_px = buf[0];
    EXPECT_GT((red_px >> 11) & 0x1F, 0) << "red channel should be set in the red rect";
    EXPECT_EQ((black_px >> 11) & 0x1F, 0) << "background should have no red";

    sim_set_active_display(nullptr);
    sim_runtime_shutdown();
}

// With no display registered, capture must fail rather than read a null device.
TEST(Capture, ActiveDisplayReturnsFalseWhenNoneRegistered) {
    sim_set_active_display(nullptr);
    std::vector<uint16_t> buf;
    int w = -1, h = -1;
    EXPECT_FALSE(boardghost::capture_active_rgb565(buf, w, h));
}

// encode_jpeg produces a valid JPEG (starts with the SOI marker 0xFFD8).
TEST(Capture, EncodeJpegProducesJpegMagic) {
    const int w = 16, h = 16;
    std::vector<uint16_t> buf(static_cast<size_t>(w) * h, 0xF800);  // solid red
    std::vector<uint8_t> out;

    bool ok = boardghost::encode_jpeg(buf.data(), w, h, 90, out);

    ASSERT_TRUE(ok);
    ASSERT_GE(out.size(), 2u);
    EXPECT_EQ(out[0], 0xFF);
    EXPECT_EQ(out[1], 0xD8);  // JPEG Start-Of-Image
}

// Bad args are rejected.
TEST(Capture, EncodeJpegRejectsBadArgs) {
    std::vector<uint8_t> out;
    EXPECT_FALSE(boardghost::encode_jpeg(nullptr, 16, 16, 90, out));
    uint16_t one = 0;
    EXPECT_FALSE(boardghost::encode_jpeg(&one, 0, 16, 90, out));
}

// encode_png produces a valid PNG (8-byte signature 89 50 4E 47 0D 0A 1A 0A).
TEST(Capture, EncodePngProducesPngMagic) {
    const int w = 16, h = 16;
    std::vector<uint16_t> buf(static_cast<size_t>(w) * h, 0x07E0);  // solid green
    std::vector<uint8_t> out;

    bool ok = boardghost::encode_png(buf.data(), w, h, out);

    ASSERT_TRUE(ok);
    ASSERT_GE(out.size(), 8u);
    const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) EXPECT_EQ(out[i], sig[i]) << "PNG sig byte " << i;
}

TEST(Capture, EncodePngRejectsBadArgs) {
    std::vector<uint8_t> out;
    EXPECT_FALSE(boardghost::encode_png(nullptr, 16, 16, out));
    uint16_t one = 0;
    EXPECT_FALSE(boardghost::encode_png(&one, 16, 0, out));
}
