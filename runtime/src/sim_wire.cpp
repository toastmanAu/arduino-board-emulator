#include <Wire.h>
#include "sim_runtime.h"
#include <cstdio>

TwoWire Wire;

void TwoWire::begin()                  { sim_log("Wire.begin"); }
void TwoWire::begin(int sda, int scl)  {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Wire.begin sda=%d scl=%d", sda, scl);
    sim_log(buf);
}
void TwoWire::end()                    { sim_log("Wire.end"); }

void TwoWire::beginTransmission(uint8_t address) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Wire.beginTransmission 0x%02X", address);
    sim_log(buf);
}

uint8_t TwoWire::endTransmission(bool stop) {
    sim_log(stop ? "Wire.endTransmission stop" : "Wire.endTransmission no-stop");
    return 0;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Wire.requestFrom 0x%02X n=%u", address, quantity);
    sim_log(buf);
    return 0;
}

size_t TwoWire::write(uint8_t b) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Wire.write 0x%02X", b);
    sim_log(buf);
    return 1;
}

size_t TwoWire::write(const uint8_t* data, size_t n) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Wire.write n=%zu", n);
    sim_log(buf);
    (void)data;
    return n;
}

int TwoWire::available() { return 0; }
int TwoWire::read()      { return -1; }
