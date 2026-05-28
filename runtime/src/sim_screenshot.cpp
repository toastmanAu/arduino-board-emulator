#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <LovyanGFX.hpp>
#include "sim_runtime.h"
#include <cstdio>
#include <vector>

extern "C" int sim_screenshot(const char* path, void* lgfx_device) {
    if (!path || !lgfx_device) return -1;
    auto* dev = static_cast<lgfx::LGFX_Device*>(lgfx_device);
    int w = dev->width();
    int h = dev->height();
    if (w <= 0 || h <= 0) return -2;

    std::vector<uint16_t> rgb565(static_cast<size_t>(w) * h);
    dev->readRect(0, 0, w, h, rgb565.data());

    // Convert RGB565 → RGBA8888 for stb_image_write.
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < rgb565.size(); ++i) {
        uint16_t v = rgb565[i];
        uint8_t r = static_cast<uint8_t>(((v >> 11) & 0x1F) * 255 / 31);
        uint8_t g = static_cast<uint8_t>(((v >> 5)  & 0x3F) * 255 / 63);
        uint8_t b = static_cast<uint8_t>(((v)       & 0x1F) * 255 / 31);
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = 255;
    }

    int ok = stbi_write_png(path, w, h, 4, rgba.data(), w * 4);
    if (!ok) {
        std::fprintf(stderr, "[boardghost] sim_screenshot: stbi_write_png failed for %s\n", path);
        return -3;
    }
    std::fprintf(stderr, "[boardghost] sim_screenshot wrote %s (%dx%d)\n", path, w, h);
    return 0;
}
