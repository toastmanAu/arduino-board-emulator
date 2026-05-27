#pragma once
#include <stdint.h>
#include <stddef.h>

class TwoWire {
public:
    void  begin();
    void  begin(int sda, int scl);
    void  end();

    void   beginTransmission(uint8_t address);
    uint8_t endTransmission(bool stop = true);
    uint8_t requestFrom(uint8_t address, uint8_t quantity);

    size_t write(uint8_t b);
    size_t write(const uint8_t* data, size_t n);
    int    available();
    int    read();

    void   setClock(uint32_t) {}
};

extern TwoWire Wire;
