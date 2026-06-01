#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include "Arduino.h"  // SerialClass / Stream
#include "WString.h"

// OTA update shim. Real implementation in sim_update.cpp writes the firmware
// bytes to a file under <project>/.boardghost/ so the user can inspect the
// upload — what arrived, what size, MD5 vs expected — without having to
// flash a real device. Defaults to <project>/.boardghost/ota-firmware.bin;
// BOARDGHOST_OTA_PATH overrides if you want to write somewhere specific.
//
// Begin/write/end/abort all return values that match ESP32 OTA semantics so
// the sketch's success/failure branches behave the same as on real hardware:
//   - begin()    fails if a previous upload didn't end() cleanly
//   - write()    returns bytes actually written; mismatch trips hasError()
//   - end(force) closes the file and verifies size + MD5 if set
//   - abort()    closes and unlinks the partial file
class UpdateClass {
public:
    UpdateClass() = default;

    // Real ESP32 API uses `command` to pick between U_FLASH (firmware) and
    // U_SPIFFS (data partition). We treat both the same — both write to the
    // OTA file — but record the command for sketches that read it back.
    bool begin(size_t size = 0, int command = 0,
               int ledPin = -1, uint8_t ledOn = 0,
               const char* label = nullptr);

    size_t writeStream(class Stream& data);
    size_t write(uint8_t* data, size_t len);
    bool   end(bool evenIfRemaining = false);
    void   abort();

    bool   isFinished() const    { return finished_; }
    bool   hasError()   const    { return error_; }
    uint8_t getError()  const    { return error_ ? 1 : 0; }
    const char* errorString();
    void   printError(SerialClass& s) { s.println(errorString()); }

    size_t size()      const     { return expected_size_; }
    size_t progress()  const     { return written_; }
    size_t remaining() const {
        return expected_size_ > written_ ? expected_size_ - written_ : 0;
    }

    void   runAsync(bool /*async*/) {}
    // setMD5 pre-arms verification: end() will hash what we wrote and compare
    // against this. If the strings don't match, end() returns false and
    // hasError() / errorString() reflect the mismatch.
    void   setMD5(const char* expected);
    String md5String();

private:
    int          fd_              = -1;
    std::string  path_;
    std::string  expected_md5_;
    std::string  actual_md5_;
    std::string  error_msg_;
    size_t       expected_size_   = 0;
    size_t       written_         = 0;
    bool         finished_        = false;
    bool         error_           = false;
    int          command_         = 0;
};

extern UpdateClass Update;

// Update command constants — mirror ESP32 core values.
#define U_FLASH   0
#define U_SPIFFS  100
#define U_AUTH    200
