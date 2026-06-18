#pragma once
// C++-only framebuffer capture + encode helpers, shared by the PNG screenshot
// path (sim_screenshot.cpp) and the mirror HTTP stream (sim_devtools.cpp).
//
// These live outside sim_runtime.h because that header is `extern "C"` and
// these signatures use std::vector. The implementations live in
// sim_screenshot.cpp so they share its single STB_IMAGE_WRITE_IMPLEMENTATION
// translation unit (the JPEG encoder uses stbi_write_jpg_to_func).
#include <cstdint>
#include <vector>

namespace boardghost {

// Read the framebuffer of an explicit LovyanGFX device as RGB565.
// `lgfx_device` is an lgfx::LGFX_Device* (typed void* to avoid pulling the
// LovyanGFX include chain into callers). Returns false on a null device or
// zero dimensions; on success fills `out` (size w*h) and sets w/h.
bool capture_rgb565(void* lgfx_device, std::vector<uint16_t>& out, int& w, int& h);

// Same, but reads the display registered via sim_set_active_display().
// Returns false when no active display is registered.
bool capture_active_rgb565(std::vector<uint16_t>& out, int& w, int& h);

// Encode an RGB565 buffer (size w*h) to JPEG bytes. `quality` is 1..100.
// Returns false on bad args or encoder failure.
bool encode_jpeg(const uint16_t* rgb565, int w, int h, int quality,
                 std::vector<uint8_t>& out);

// Encode an RGB565 buffer (size w*h) to PNG bytes (lossless; the on-demand
// /mirror/screen.png path). Returns false on bad args or encoder failure.
bool encode_png(const uint16_t* rgb565, int w, int h, std::vector<uint8_t>& out);

}  // namespace boardghost
