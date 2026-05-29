// WProgram.h — Arduino 0.x legacy header alias for Arduino.h.
// The `Time` library and other older codebases #include "WProgram.h" when
// ARDUINO < 100; we redirect to our Arduino shim so they compile cleanly.
// Arduino.h provides strcpy_P / strlen_P / strcmp_P inline. memcpy_P is
// owned by LovyanGFX's pgmspace.h to avoid name collisions.
#pragma once
#include "Arduino.h"
