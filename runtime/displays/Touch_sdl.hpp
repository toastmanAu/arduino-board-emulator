#pragma once
#include <LovyanGFX.hpp>

// Pull in SDL the same way LovyanGFX's own sdl platform does: try SDL2 first,
// then fall back to bare <SDL.h>. The entire class is gated on SDL_h_ so that
// arduino-cli preprocessing (which lacks SDL headers) can still #include this
// file without errors — Panel_sdl.hpp uses the same guard.
#define SDL_MAIN_HANDLED
#if __has_include(<SDL2/SDL.h>)
#  include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#  include <SDL.h>
#endif

#if defined(SDL_h_)

// A LovyanGFX touch driver that returns the current SDL mouse position
// while the left mouse button is held down. This emulates the on-chip
// behaviour of XPT2046 / FT6236 / GT911 etc. for desktop simulation —
// regardless of which controller a board profile claims, the user-facing
// API (tft.getTouch / tft.getTouchPointRaw) just works.
//
// API confirmed against lgfx/v1/Touch.hpp (v1.1.16):
//   base class:  lgfx::ITouch  (lgfx::v1::ITouch via inline namespace)
//   _cfg:        protected member of ITouch
//   pure virtuals: bool init(void), void wakeup(void), void sleep(void),
//                  uint_fast8_t getTouchRaw(touch_point_t*, uint_fast8_t)
class Touch_sdl : public lgfx::ITouch {
public:
    Touch_sdl() {
        _cfg.x_min = 0;
        _cfg.x_max = 4095;
        _cfg.y_min = 0;
        _cfg.y_max = 4095;
        _cfg.bus_shared = false;
        // pin assignments are irrelevant in sim; leave at defaults.
    }

    bool init(void) override { return true; }

    void wakeup(void) override {}
    void sleep(void)  override {}

    uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
        if (count == 0 || tp == nullptr) return 0;

        // Pump SDL events so SDL_GetMouseState reflects current state.
        SDL_PumpEvents();
        int mx = 0, my = 0;
        Uint32 buttons = SDL_GetMouseState(&mx, &my);

        if (!(buttons & SDL_BUTTON(SDL_BUTTON_LEFT))) return 0;

        // Report panel coordinates directly; most user code calls
        // tft.setTouchCalibration(0, panel_width-1, 0, panel_height-1)
        // which already matches the SDL window pixel range.
        tp[0].x    = static_cast<int16_t>(mx);
        tp[0].y    = static_cast<int16_t>(my);
        tp[0].size = 1;
        tp[0].id   = 0;
        return 1;
    }
};

#endif  // defined(SDL_h_)
