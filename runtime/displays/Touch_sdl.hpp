#pragma once
#include <LovyanGFX.hpp>
#include <cstdlib>
#include <cstdint>
#include <vector>

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

        // Auto-calibration: sketches that call tft.calibrateTouch() block
        // forever in headless sim mode (no taps come in). Setting
        // BOARDGHOST_AUTO_TOUCH_CAL=1 makes getTouchRaw cycle through the
        // 4 corners on successive calls so the calibration completes.
        if (const char* v = std::getenv("BOARDGHOST_AUTO_TOUCH_CAL")) {
            auto_cal_ = (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y');
        }
        // Scripted touches for headless testing. Format:
        //   BOARDGHOST_SIM_TOUCHES="t1ms:x,y;t2ms:x,y;..."
        // Each entry fires a single ~80ms press at (x,y) starting at t_ms
        // after process start. Example "2000:160,125;4000:160,200" taps two
        // buttons in sequence.
        if (const char* v = std::getenv("BOARDGHOST_SIM_TOUCHES")) {
            const char* p = v;
            while (*p) {
                long t = std::strtol(p, (char**)&p, 10);
                if (*p != ':') break;
                ++p;
                long x = std::strtol(p, (char**)&p, 10);
                if (*p != ',') break;
                ++p;
                long y = std::strtol(p, (char**)&p, 10);
                scripted_.push_back({(uint32_t)t, (int16_t)x, (int16_t)y});
                if (*p == ';') ++p;
                else break;
            }
        }
        // start_ms_ initialised lazily in getTouchRaw (SDL_Init may not be
        // called yet when static instance ctors run).
    }

    bool init(void) override { return true; }

    void wakeup(void) override {}
    void sleep(void)  override {}

    uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
        if (count == 0 || tp == nullptr) return 0;

        // Scripted touch injection — only active after auto-calibration
        // completes (otherwise the scripted taps interfere with the
        // calibration state machine). A tap lasts ~80ms across consecutive
        // getTouchRaw calls so LGFX's debounce sees a stable press.
        if (!scripted_.empty() && !auto_cal_) {
            // Lazy-init start_ms_ on first call — static ctors run before
            // SDL_Init so SDL_GetTicks() in our ctor would return 0.
            if (start_ms_ == 0) start_ms_ = SDL_GetTicks();
            uint32_t now = SDL_GetTicks() - start_ms_;
            // Advance past finished entries.
            while (scripted_idx_ < scripted_.size()
                   && now > scripted_[scripted_idx_].when_ms + 80) {
                ++scripted_idx_;
            }
            if (scripted_idx_ < scripted_.size()) {
                const auto& s = scripted_[scripted_idx_];
                if (now >= s.when_ms && now <= s.when_ms + 80) {
                    tp[0].x = s.x;
                    tp[0].y = s.y;
                    tp[0].size = 1;
                    tp[0].id = 0;
                    return 1;
                }
            }
        }

        if (auto_cal_) {
            // LGFX's calibrate_touch needs, per corner: many consecutive
            // "press at this corner" reads (8 iterations × 2 reads each,
            // both within 20px) then a "release" read before advancing.
            // Cycle: PRESS_READS reads pressed + RELEASE_READS reads
            // released, per corner, 4 corners total. After all 4 corners
            // finish we self-disable so the main loop sees normal "no touch"
            // behavior (otherwise the sketch would interpret every loop
            // iteration as a corner tap).
            constexpr int PRESS_READS   = 32;
            constexpr int RELEASE_READS = 4;
            constexpr int CYCLE = PRESS_READS + RELEASE_READS;
            constexpr int TOTAL_PHASES = 4 * CYCLE;
            if (auto_cal_phase_ >= TOTAL_PHASES) {
                auto_cal_ = false;
                return 0;
            }
            const int16_t xs[4] = {           0, (int16_t)_cfg.x_max,
                                    (int16_t)_cfg.x_max,           0 };
            const int16_t ys[4] = {           0,           0,
                                    (int16_t)_cfg.y_max, (int16_t)_cfg.y_max };
            int corner = (auto_cal_phase_ / CYCLE) % 4;
            int in_cycle = auto_cal_phase_ % CYCLE;
            bool press = in_cycle < PRESS_READS;
            auto_cal_phase_++;
            if (!press) return 0;
            tp[0].x    = xs[corner];
            tp[0].y    = ys[corner];
            tp[0].size = 1;
            tp[0].id   = 0;
            return 1;
        }

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

private:
    bool auto_cal_ = false;
    int  auto_cal_phase_ = 0;
    struct ScriptedTap { uint32_t when_ms; int16_t x; int16_t y; };
    std::vector<ScriptedTap> scripted_;
    size_t   scripted_idx_ = 0;
    uint32_t start_ms_     = 0;
};

#endif  // defined(SDL_h_)
