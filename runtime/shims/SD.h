#pragma once
#include "FS.h"
// ESP32 Arduino core's SD.h transitively includes SPI.h — sketches commonly
// declare `SPIClass mySpi;` after only including SD.h. Mirror that here so
// the include order doesn't matter.
#include "SPI.h"

extern fs::FS SD;

// SDClass — many sketches reach for `SD.begin(cs, spi)` with an SPIClass&
// override; match the surface area used by the ESP32 SD driver.
class SDClass {
public:
    bool begin(int /*cs*/ = -1) { return true; }
    bool begin(int /*cs*/, SPIClass& /*spi*/, uint32_t /*frequency*/ = 4000000,
               const char* /*mountpoint*/ = "/sd", uint8_t /*max_files*/ = 5,
               bool /*format_if_empty*/ = false) { return true; }
    void end() {}
};
extern SDClass SDLib;
