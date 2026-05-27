#pragma once
#include <LovyanGFX.hpp>

// SSD1306 display simulator: 128x64, SDL framebuffer backend.
//
// Panel_sdl renders at 16bpp internally; this is a 16bpp emulation
// of a 1bpp panel. Callers should use TFT_WHITE / TFT_BLACK only.
// setColorDepth(1) was intentionally omitted because Panel_sdl does not
// support 1bpp depth and would assert or silently misbehave.
//
// sdl_setup() / sdl_close() are thin wrappers around the static
// Panel_sdl::setup() / Panel_sdl::close() methods, provided so test
// code can call them on the LGFX object without knowing about Panel_sdl
// internals. (These were not in the original spec but keep the test
// symmetrical with the ILI9488 pattern.)
class LGFX_SSD1306_SDL : public lgfx::LGFX_Device {
public:
    LGFX_SSD1306_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 128;
        cfg.memory_height = 64;
        cfg.panel_width   = 128;
        cfg.panel_height  = 64;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        panel_.config(cfg);
        setPanel(&panel_);
    }

    // Initialise SDL internals (semaphores) before calling tft.init().
    // Mirrors Panel_sdl's static setup/close used in the ILI9488 path.
    static void sdl_setup() { lgfx::Panel_sdl::setup(); }
    static void sdl_close() { lgfx::Panel_sdl::close(); }

private:
    lgfx::Panel_sdl panel_;
};
