#pragma once
#include <stdint.h>
#include <stddef.h>

class SPISettings {
public:
    SPISettings() = default;
    SPISettings(uint32_t /*clock*/, uint8_t /*bit_order*/, uint8_t /*mode*/) {}
};

class SPIClass {
public:
    void    begin();
    // ESP32 overload — explicit SCK/MISO/MOSI/SS pins.
    void    begin(int8_t /*sck*/, int8_t /*miso*/, int8_t /*mosi*/,
                  int8_t /*ss*/ = -1) {}
    void    end();
    void    beginTransaction(SPISettings);
    void    endTransaction();
    uint8_t transfer(uint8_t b);
    uint16_t transfer16(uint16_t b);
    void    transferBytes(const uint8_t* data, uint8_t* out, size_t n);

    void    setBitOrder(uint8_t)   {}
    void    setDataMode(uint8_t)   {}
    void    setClockDivider(uint8_t) {}
    void    setFrequency(uint32_t) {}
};

extern SPIClass SPI;

#define SPI_HAS_TRANSACTION 1
#define MSBFIRST 1
#define LSBFIRST 0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
