#pragma once
#include <LovyanGFX.hpp>

class LGFX_ST7735_SDL : public lgfx::LGFX_Device {
public:
    LGFX_ST7735_SDL() {
        auto cfg = panel_.config();
        cfg.memory_width  = 160;
        cfg.memory_height = 128;
        cfg.panel_width   = 160;
        cfg.panel_height  = 128;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        panel_.config(cfg);
        setPanel(&panel_);
    }

private:
    lgfx::Panel_sdl panel_;
};
