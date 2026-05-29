// Panel_sdl_bg — BoardGhost subclass of lgfx::Panel_sdl that delegates
// getTouchRaw to an attached ITouch (e.g. our Touch_sdl) when present.
//
// Why: stock Panel_sdl::getTouchRaw returns only monitor.touched, which is
// set by SDL_MOUSEBUTTONDOWN/UP events from a real window. Sketches that
// call lcd.calibrateTouch() in headless mode (SDL_VIDEODRIVER=dummy or
// no window) hang forever waiting for touches that never come. Our Touch_sdl
// understands BOARDGHOST_AUTO_TOUCH_CAL=1 and synthesises corner taps so the
// calibration completes — but only if Panel_sdl actually delegates to it.
#pragma once
#include <LovyanGFX.hpp>

class Panel_sdl_bg : public lgfx::Panel_sdl {
public:
    uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
        if (touch()) {
            return touch()->getTouchRaw(tp, count);
        }
        return lgfx::Panel_sdl::getTouchRaw(tp, count);
    }
};
