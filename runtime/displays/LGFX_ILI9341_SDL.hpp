#pragma once
#include <LovyanGFX.hpp>

class LGFX_ILI9341_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ILI9341_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 320;
        cfg.memory_height = 240;
        cfg.panel_width   = 320;
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
