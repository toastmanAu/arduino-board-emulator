#pragma once
#include <stdint.h>
#include <stddef.h>

class EEPROMClass {
public:
    bool    begin(size_t size);
    void    end();

    uint8_t read(int addr);
    void    write(int addr, uint8_t val);
    bool    commit();

    // ESP32 Arduino-core helpers used by some sketches:
    template <typename T>
    T readInt() { return readInt(0); }
    int  readInt(int addr);
    void writeInt(int addr, int value);

    size_t length() const { return size_; }

private:
    size_t   size_  = 0;
    uint8_t* data_  = nullptr;
};

extern EEPROMClass EEPROM;
