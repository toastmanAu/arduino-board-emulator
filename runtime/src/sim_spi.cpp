#include <SPI.h>
#include "sim_runtime.h"
#include <cstdio>

SPIClass SPI;

namespace {
void log_hex(const char* prefix, uint8_t b) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s 0x%02X", prefix, b);
    sim_log(buf);
}
}

void SPIClass::begin()                                { sim_log("SPI.begin"); }
void SPIClass::end()                                  { sim_log("SPI.end"); }
void SPIClass::beginTransaction(SPISettings)          { sim_log("SPI.beginTransaction"); }
void SPIClass::endTransaction()                       { sim_log("SPI.endTransaction"); }

uint8_t SPIClass::transfer(uint8_t b) {
    log_hex("SPI.transfer", b);
    return 0;
}

uint16_t SPIClass::transfer16(uint16_t b) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "SPI.transfer16 0x%04X", b);
    sim_log(buf);
    return 0;
}

void SPIClass::transferBytes(const uint8_t* data, uint8_t* out, size_t n) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "SPI.transferBytes n=%zu", n);
    sim_log(buf);
    if (out && data) {
        // No-op: simulate slave that returns zeros.
        for (size_t i = 0; i < n; ++i) out[i] = 0;
    }
    (void)data;
}
