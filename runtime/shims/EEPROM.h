#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WString.h"

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

    // M2.D — typed read/write extensions. ESP32 Arduino's EEPROM library
    // exposes these for length-prefixed Strings and single-byte bools.
    // The String layout is: 1 byte length (0..size_-addr-1) then raw bytes.
    String readString(int addr);
    size_t writeString(int addr, const String& val);
    size_t writeString(int addr, const char* val);

    bool   readBool(int addr)              { return read(addr) != 0; }
    void   writeBool(int addr, bool val)   { write(addr, val ? 1 : 0); }

    size_t length() const { return size_; }

private:
    size_t   size_  = 0;
    uint8_t* data_  = nullptr;
};

extern EEPROMClass EEPROM;
