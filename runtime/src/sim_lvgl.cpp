#include "sim_lvgl.h"
#include <lvgl.h>
// LVGL v9 SDL drivers: lv_sdl_window_create / lv_sdl_mouse_create are not
// pulled in by <lvgl.h> alone — include the driver headers explicitly.
// LVGL's CMake adds LVGL_ROOT_DIR to the include path, so the path relative
// to that root is src/drivers/sdl/...
#include <src/drivers/sdl/lv_sdl_window.h>
#include <src/drivers/sdl/lv_sdl_mouse.h>
#include <cstdio>

extern "C" void sim_lvgl_attach_sdl(int width, int height) {
    lv_display_t* disp  = lv_sdl_window_create((int32_t)width, (int32_t)height);
    lv_indev_t*   mouse = lv_sdl_mouse_create();
    if (mouse && disp) {
        lv_indev_set_display(mouse, disp);
    }
    std::fprintf(stderr, "[boardghost] LVGL SDL display %dx%d ready\n", width, height);
}
