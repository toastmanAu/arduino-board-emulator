#pragma once
#include <LovyanGFX.hpp>
#include "Touch_sdl.hpp"
#include "sim_runtime.h"  // sim_set_active_display

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
        panel_.setTouch(&touch_);
    }

    // Auto-register with the sim runtime on init_impl() so SIGUSR1 screenshots
    // work even when the sketch never explicitly calls sim_set_active_display.
    bool init_impl(bool use_reset, bool use_clear) override {
        bool ok = lgfx::LGFX_Device::init_impl(use_reset, use_clear);
        sim_set_active_display(this);
        return ok;
    }

private:
    lgfx::Panel_sdl panel_;
    Touch_sdl       touch_;
};
