#include <Arduino.h>
#include "sim_runtime.h"

// Forward declarations of the user's sketch entry points.
// Defined by the user's preprocessed .ino.
extern void setup();
extern void loop();

int main(int argc, char** argv) {
    sim_runtime_init(argc, argv);
    setup();
    while (!sim_should_quit()) {
        loop();
        sim_pump_events();
    }
    sim_runtime_shutdown();
    return 0;
}
