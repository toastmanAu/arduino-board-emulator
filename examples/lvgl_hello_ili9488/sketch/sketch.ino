#include <Arduino.h>
#include <LGFX_ILI9488_SDL.hpp>
#include <lvgl.h>
#include "sim_lvgl.h"
#include <unistd.h>      // _exit()
#include <cstdlib>

LGFX_ILI9488_SDL tft;
int frames = 0;
int clicks = 0;

static void btn_event(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        clicks++;
        Serial.printf("button clicked, total=%d\n", clicks);
    }
}

void setup() {
    Serial.begin(115200);
    tft.init();
    sim_set_active_display(&tft);
    tft.setRotation(1);

    lv_init();
    sim_lvgl_attach_sdl(480, 320);

    lv_obj_t* btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn, 200, 100);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, btn_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "BoardGhost");
    lv_obj_center(lbl);

    Serial.println("lvgl_hello_ili9488 started");
}

void loop() {
    lv_timer_handler();
    delay(5);
    frames++;
    if (frames % 50 == 0) Serial.printf("frame %d\n", frames);
    // Honor BOARDGHOST_FRAME_LIMIT for headless E2E (defaults to 200).
    // The launcher leaves it unset, so the demo runs until you close the
    // window (SDL_QUIT) or click "Stop" in the launcher.
    static int limit = -1;
    if (limit < 0) {
        const char* env = std::getenv("BOARDGHOST_FRAME_LIMIT");
        limit = env ? atoi(env) : 0;   // 0 = run forever
    }
    if (limit > 0 && frames >= limit) {
        Serial.println("done");
        Serial.flush();
        _exit(0);
    }
}
