#pragma once
#include <LovyanGFX.hpp>

// GC9A01 is a 240×240 round display. The simulated window is still a
// square SDL window; cropping to a circle is a frontend concern, not a
// panel concern. Real hardware ignores draws outside the visible circle.
class LGFX_GC9A01_SDL : public lgfx::LGFX_Device {
public:
    LGFX_GC9A01_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 240;
        cfg.memory_height = 240;
        cfg.panel_width   = 240;
        cfg.panel_height  = 240;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
    }

private:
    lgfx::Panel_sdl panel_;
};
