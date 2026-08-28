#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <cstdint>
#include <stb_image_write.h>

#include <LovyanGFX.hpp>
#include "sim_runtime.h"
#include "sim_capture.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace boardghost {

bool capture_rgb565(void* lgfx_device, std::vector<uint16_t>& out, int& w, int& h) {
    if (!lgfx_device) return false;
    auto* dev = static_cast<lgfx::LGFX_Device*>(lgfx_device);
    w = dev->width();
    h = dev->height();
    if (w <= 0 || h <= 0) return false;
    out.assign(static_cast<size_t>(w) * h, 0);
    // LovyanGFX stores the framebuffer in display (big-endian) byte order, so a
    // raw readRect gives byte-swapped RGB565 (red 0xF800 reads as 0x00F8).
    // setSwapBytes(true) makes readRect hand back canonical RGB565 where
    // (v >> 11) is red — matching what every consumer here expects. Save and
    // restore so we don't mutate the sketch's own device state.
    bool prev_swap = dev->getSwapBytes();
    dev->setSwapBytes(true);
    dev->readRect(0, 0, w, h, out.data());
    dev->setSwapBytes(prev_swap);
    return true;
}

bool capture_active_rgb565(std::vector<uint16_t>& out, int& w, int& h) {
    return capture_rgb565(sim_get_active_display(), out, w, h);
}

bool encode_jpeg(const uint16_t* rgb565, int w, int h, int quality,
                 std::vector<uint8_t>& out) {
    if (!rgb565 || w <= 0 || h <= 0) return false;
    quality = std::min(std::max(quality, 1), 100);

    // RGB565 → RGB888 (stb's JPEG writer takes 8-bit channels).
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    const size_t px = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < px; ++i) {
        uint16_t v = rgb565[i];
        rgb[i * 3 + 0] = static_cast<uint8_t>(((v >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = static_cast<uint8_t>(((v >> 5)  & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = static_cast<uint8_t>(((v)       & 0x1F) * 255 / 31);
    }

    out.clear();
    auto sink = [](void* ctx, void* data, int size) {
        auto* o = static_cast<std::vector<uint8_t>*>(ctx);
        const uint8_t* p = static_cast<const uint8_t*>(data);
        o->insert(o->end(), p, p + size);
    };
    return stbi_write_jpg_to_func(sink, &out, w, h, 3, rgb.data(), quality) != 0;
}

bool encode_png(const uint16_t* rgb565, int w, int h, std::vector<uint8_t>& out) {
    if (!rgb565 || w <= 0 || h <= 0) return false;

    // RGB565 → RGBA8888, same expansion sim_screenshot uses for the file path.
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    const size_t px = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < px; ++i) {
        uint16_t v = rgb565[i];
        rgba[i * 4 + 0] = static_cast<uint8_t>(((v >> 11) & 0x1F) * 255 / 31);
        rgba[i * 4 + 1] = static_cast<uint8_t>(((v >> 5)  & 0x3F) * 255 / 63);
        rgba[i * 4 + 2] = static_cast<uint8_t>(((v)       & 0x1F) * 255 / 31);
        rgba[i * 4 + 3] = 255;
    }

    out.clear();
    auto sink = [](void* ctx, void* data, int size) {
        auto* o = static_cast<std::vector<uint8_t>*>(ctx);
        const uint8_t* p = static_cast<const uint8_t*>(data);
        o->insert(o->end(), p, p + size);
    };
    return stbi_write_png_to_func(sink, &out, w, h, 4, rgba.data(), w * 4) != 0;
}

}  // namespace boardghost

extern "C" int sim_screenshot(const char* path, void* lgfx_device) {
    if (!path || !lgfx_device) return -1;

    std::vector<uint16_t> rgb565;
    int w = 0, h = 0;
    if (!boardghost::capture_rgb565(lgfx_device, rgb565, w, h)) return -2;

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
