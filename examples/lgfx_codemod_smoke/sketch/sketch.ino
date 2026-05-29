// M2.E codemod smoke test: a sketch with a hardware LGFX setup file that
// boardghost transparently rewrites for sim execution.
#include "lvgfx_setup.h"

static LGFX tft;
static int frame = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("lgfx_codemod_smoke: setup");
    pinMode(LCD_BL, OUTPUT);  // references a #define from the user's setup file
    tft.init();
    tft.fillScreen(0xF800);  // red
    sim_set_active_display(&tft);
    Serial.println("lgfx_codemod_smoke: tft ready");
}

void loop() {
    if (++frame > 200) {
        Serial.println("lgfx_codemod_smoke: done");
        exit(0);
    }
    delay(10);
}
