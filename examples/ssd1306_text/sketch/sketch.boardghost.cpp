#line 1 "/home/phill/arduino-board-emulator/examples/ssd1306_text/sketch/sketch.ino"
#include <Arduino.h>
#include <LGFX_SSD1306_SDL.hpp>
#include <cstdlib>
#include <unistd.h>   // _exit()

LGFX_SSD1306_SDL oled;
int frame = 0;

#line 9 "/home/phill/arduino-board-emulator/examples/ssd1306_text/sketch/sketch.ino"
void setup();
#line 17 "/home/phill/arduino-board-emulator/examples/ssd1306_text/sketch/sketch.ino"
void loop();
#line 9 "/home/phill/arduino-board-emulator/examples/ssd1306_text/sketch/sketch.ino"
void setup() {
    Serial.begin(115200);
    oled.init();
    oled.fillScreen(TFT_BLACK);
    oled.setTextColor(TFT_WHITE);
    Serial.println("ssd1306_text started");
}

void loop() {
    oled.fillScreen(TFT_BLACK);
    oled.setCursor(0, 0);
    oled.printf("Frame %d", frame);
    oled.display();
    Serial.printf("frame %d\n", frame);
    frame++;
    delay(50);
    if (frame >= 5) {
        Serial.println("done");
        // Flush stdio buffers before _exit() since _exit() bypasses
        // the atexit/destructor chain that normally flushed them.
        // Use _exit() rather than std::exit() to skip the C++ static
        // destructor chain: LGFX_SSD1306_SDL is a file-scope global whose
        // Panel_sdl destructor calls _list_monitor.remove(), but that
        // std::list<> static is destroyed before the LGFX object in the
        // reverse-construction order, causing SIGSEGV.
        Serial.flush();
        _exit(0);
    }
}

