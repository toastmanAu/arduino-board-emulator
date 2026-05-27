#line 1 "/home/phill/arduino-board-emulator/examples/hello_serial/sketch/sketch.ino"
#include <Arduino.h>

int counter = 0;

#line 5 "/home/phill/arduino-board-emulator/examples/hello_serial/sketch/sketch.ino"
void setup();
#line 10 "/home/phill/arduino-board-emulator/examples/hello_serial/sketch/sketch.ino"
void loop();
#line 5 "/home/phill/arduino-board-emulator/examples/hello_serial/sketch/sketch.ino"
void setup() {
    Serial.begin(115200);
    Serial.println("Hello from BoardGhost");
}

void loop() {
    Serial.print("tick ");
    Serial.println(counter++);
    delay(50);
    if (counter >= 5) {
        // Exit cleanly so the E2E script can observe a known final state.
        Serial.println("done");
        std::exit(0);
    }
}

