#pragma once
#include <LovyanGFX.hpp>

class LGFX_ST7796_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ST7796_SDL() {
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
    }

private:
    lgfx::Panel_sdl panel_;
};
