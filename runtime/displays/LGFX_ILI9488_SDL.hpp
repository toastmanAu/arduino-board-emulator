#pragma once
#include <LovyanGFX.hpp>
#include "Touch_sdl.hpp"

// ILI9488 display simulator: 480x320, SDL framebuffer backend.
//
// Panel_Device (the base of Panel_sdl) exposes config() as a getter/setter pair
// that operates on a config_t struct with the fields used below.
// No deviations from the plan — field names match exactly.
class LGFX_ILI9488_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ILI9488_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 480;
        cfg.memory_height = 320;
        cfg.panel_width   = 480;
        cfg.panel_height  = 320;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
        panel_.setTouch(&touch_);
    }

private:
    lgfx::Panel_sdl panel_;
    Touch_sdl       touch_;
};
