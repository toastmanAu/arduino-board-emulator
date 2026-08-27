#include "sim_lvgl.h"
#include <lvgl.h>
// LVGL v9 SDL drivers: lv_sdl_window_create / lv_sdl_mouse_create are not
// pulled in by <lvgl.h> alone — include the driver headers explicitly.
// LVGL's CMake adds LVGL_ROOT_DIR to the include path, so the path relative
// to that root is src/drivers/sdl/...
#include <src/drivers/sdl/lv_sdl_window.h>
#include <src/drivers/sdl/lv_sdl_mouse.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" void sim_lvgl_attach_sdl(int width, int height) {
    // LVGL >= 9.3's SDL backend creates an SDL *renderer*, and the dummy video
    // driver provides no accelerated one — so under SDL_VIDEODRIVER=dummy (every
    // headless run: CI, --screenshot, e2e) lv_sdl_window_create() returns NULL.
    // Ask for the software renderer unless the caller has already chosen one.
    // Without this, LVGL 9.5 headless dies in the assert-spin described below;
    // LVGL 9.2 happened to survive because its SDL path did not need a renderer.
    if (const char* drv = std::getenv("SDL_VIDEODRIVER")) {
        if (std::strcmp(drv, "dummy") == 0 && std::getenv("SDL_RENDER_DRIVER") == nullptr) {
            setenv("SDL_RENDER_DRIVER", "software", 0);
        }
    }

    lv_display_t* disp = lv_sdl_window_create((int32_t)width, (int32_t)height);

    // Check the display BEFORE touching anything downstream. A NULL display here
    // is not a benign no-op: lv_indev_create warns, lv_screen_active() returns
    // NULL, and the first lv_label_set_text() trips LV_ASSERT — whose default
    // handler is an infinite loop. The observable symptom is a 60-second test
    // TIMEOUT with no useful message, which is a genuinely awful thing to debug.
    // Fail loudly instead.
    if (!disp) {
        std::fprintf(stderr,
                     "[boardghost] FATAL: lv_sdl_window_create(%d,%d) returned NULL "
                     "(SDL_VIDEODRIVER=%s SDL_RENDER_DRIVER=%s). LVGL cannot register a "
                     "display; anything drawn after this would assert-spin.\n",
                     width, height,
                     std::getenv("SDL_VIDEODRIVER")   ? std::getenv("SDL_VIDEODRIVER")   : "(unset)",
                     std::getenv("SDL_RENDER_DRIVER") ? std::getenv("SDL_RENDER_DRIVER") : "(unset)");
        return;
    }

    lv_indev_t* mouse = lv_sdl_mouse_create();
    if (mouse) {
        lv_indev_set_display(mouse, disp);
    }
    std::fprintf(stderr, "[boardghost] LVGL SDL display %dx%d ready\n", width, height);
}
