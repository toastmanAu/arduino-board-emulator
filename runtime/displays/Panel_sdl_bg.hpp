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
#include <cstring>
#include <cstdint>
#include <vector>

class Panel_sdl_bg : public lgfx::Panel_sdl {
public:
    Panel_sdl_bg() {
        // Optional screen-coordinate scripted touches: same format as
        // BOARDGHOST_SIM_TOUCHES but the coords are interpreted as final
        // screen pixels (auto-rotated for the panel). Lets users write
        // intuitive (x, y) values without computing the inverse of the
        // hardware rotation manually.
        if (const char* v = std::getenv("BOARDGHOST_SIM_TOUCHES_SCREEN")) {
            const char* p = v;
            while (*p) {
                long t = std::strtol(p, (char**)&p, 10);
                if (*p != ':') break;
                ++p;
                long x = std::strtol(p, (char**)&p, 10);
                if (*p != ',') break;
                ++p;
                long y = std::strtol(p, (char**)&p, 10);
                screen_taps_.push_back({(uint32_t)t, (int16_t)x, (int16_t)y});
                if (*p == ';') ++p;
                else break;
            }
        }
    }

    uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
        // When sim-side touch injection is active, force identity calibration
        // so simulated coords aren't transformed by the user's hardware-tuned
        // setTouchCalibrate() matrix. AUTO_TOUCH_CAL is included because a
        // sketch typically follows calibrateTouch() with a setTouchCalibrate()
        // call that loads a saved hardware matrix from EEPROM — that matrix
        // expects raw ADC values in 0..4095, but our live-mouse pickup
        // produces pixel-space coords. Forcing identity on every read makes
        // the pixel-space coords pass through unchanged (rotation is still
        // applied downstream by convertRawXY).
        if (std::getenv("BOARDGHOST_SIM_TOUCHES")
            || std::getenv("BOARDGHOST_AUTO_TOUCH_CAL")
            || !screen_taps_.empty())
        {
            float identity[6] = {1, 0, 0, 0, 1, 0};
            setCalibrateAffine(identity);
        }

        // Screen-coord scripted touches: emit a press within an 80ms window
        // after each scheduled time. We pre-derotate based on the panel's
        // current rotation state so convertRawXY (which runs after this) cancels
        // out and the user's lcd.getTouch() sees the original screen coords.
        if (!screen_taps_.empty()) {
            if (start_ms_ == 0) start_ms_ = SDL_GetTicks();
            uint32_t now = SDL_GetTicks() - start_ms_;
            while (screen_idx_ < screen_taps_.size()
                   && now > screen_taps_[screen_idx_].when_ms + 80) {
                ++screen_idx_;
            }
            if (screen_idx_ < screen_taps_.size()) {
                const auto& s = screen_taps_[screen_idx_];
                if (now >= s.when_ms && now <= s.when_ms + 80) {
                    auto r = compute_effective_rotation();
                    int16_t rx = s.x;
                    int16_t ry = s.y;
                    // Inverse of convertRawXY's rotation step.
                    bool vflip = (1 << r) & 0b10010110;
                    if (r) {
                        if (vflip)   ry = (int16_t)((_height - 1) - ry);
                        if (r & 2)   rx = (int16_t)((_width  - 1) - rx);
                        if (r & 1)   std::swap(rx, ry);
                    }
                    tp[0].x = rx;
                    tp[0].y = ry;
                    tp[0].size = 1;
                    tp[0].id = 0;
                    return 1;
                }
            }
        }

        if (touch()) {
            auto n = touch()->getTouchRaw(tp, count);
            if (n) return n;
        } else {
            auto n = lgfx::Panel_sdl::getTouchRaw(tp, count);
            if (n) return n;
        }

        // Live mouse fallback: when nothing else produced a touch this read,
        // sample SDL_GetMouseState. The mouse coord SDL returns is in window
        // pixels; we scale it back to panel pixels using SDL_GetWindowSize so
        // user-resized windows (or HiDPI) still hit the right framebuffer
        // location. Then inverse-rotate so the pixel under the cursor is what
        // the sketch's lcd.getTouch() returns, regardless of panel rotation
        // (and regardless of any hardware cal matrix — see identity-force).
        if (count > 0 && tp != nullptr) {
            SDL_PumpEvents();
            int mx = 0, my = 0;
            Uint32 buttons = SDL_GetMouseState(&mx, &my);
            if (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) {
                if (auto* win = SDL_GetMouseFocus()) {
                    int ww = 0, wh = 0;
                    SDL_GetWindowSize(win, &ww, &wh);
                    if (ww > 0 && _width > 0)  mx = (int)((int64_t)mx * _width  / ww);
                    if (wh > 0 && _height > 0) my = (int)((int64_t)my * _height / wh);
                }
                inverse_rotate(mx, my, tp[0]);
                return 1;
            }
        }
        return 0;
    }

    // Public so tests can pin the rotation math without driving SDL.
    // Order mirrors the screen_taps branch above — it's the validated inverse
    // of convertRawXY's rotation step. Don't reorder without re-deriving.
    void inverse_rotate(int mx, int my, lgfx::touch_point_t& out) const {
        auto r = compute_effective_rotation();
        int16_t rx = (int16_t)mx;
        int16_t ry = (int16_t)my;
        bool vflip = (1 << r) & 0b10010110;
        if (r) {
            if (vflip)  ry = (int16_t)((_height - 1) - ry);
            if (r & 2)  rx = (int16_t)((_width  - 1) - rx);
            if (r & 1)  std::swap(rx, ry);
        }
        out.x = rx;
        out.y = ry;
        out.size = 1;
        out.id = 0;
    }

private:
    struct ScreenTap { uint32_t when_ms; int16_t x; int16_t y; };
    std::vector<ScreenTap> screen_taps_;
    size_t   screen_idx_ = 0;
    uint32_t start_ms_   = 0;

    uint_fast8_t compute_effective_rotation() const {
        auto r = _internal_rotation;
        if (touch()) {
            auto offset = touch()->config().offset_rotation;
            r = ((r + offset) & 3) | ((r & 4) ^ (offset & 4));
        }
        return r;
    }
};
