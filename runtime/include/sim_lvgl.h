#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Registers LVGL's SDL display + indev drivers and opens an SDL window of the
// given size. Must be called AFTER `lv_init()` and AFTER `sim_runtime_init()`.
//
// width/height are panel dimensions. The driver uses LV_COLOR_DEPTH (16) buffers.
void sim_lvgl_attach_sdl(int width, int height);

#ifdef __cplusplus
}
#endif
