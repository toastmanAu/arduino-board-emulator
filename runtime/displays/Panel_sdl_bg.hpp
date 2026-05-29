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
#include <cstdlib>

class Panel_sdl_bg : public lgfx::Panel_sdl {
public:
    uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
        // When BOARDGHOST_SIM_TOUCHES is set, force identity calibration so
        // scripted screen-pixel coordinates pass through unchanged. Sketches
        // with hardware-tuned calibration (e.g. an XPT2046 mapping raw 0-4095
        // to screen 0-320) would otherwise transform our scripted coords into
        // off-screen values. The user's setTouchCalibrate() still runs; we
        // just override its effect on every touch read while scripted mode
        // is active.
        if (std::getenv("BOARDGHOST_SIM_TOUCHES")) {
            float identity[6] = {1, 0, 0, 0, 1, 0};
            setCalibrateAffine(identity);
        }
        if (touch()) {
            return touch()->getTouchRaw(tp, count);
        }
        return lgfx::Panel_sdl::getTouchRaw(tp, count);
    }
};
