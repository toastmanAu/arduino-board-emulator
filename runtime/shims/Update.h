#pragma once
#include <stdint.h>
#include <stddef.h>
#include "Arduino.h"  // SerialClass / Stream-ish print target
#include "WString.h"

// OTA update — no-op shim. The sim has no flash to write to, but sketches
// commonly gate features on Update.begin() / Update.end() success and print
// errors to Serial; the stubs let those code paths compile and execute
// without crashing.
class UpdateClass {
public:
    bool   begin(size_t /*size*/ = 0, int /*command*/ = 0,
                 int /*ledPin*/ = -1, uint8_t /*ledOn*/ = 0,
                 const char* /*label*/ = nullptr) { error_ = false; return true; }
    size_t writeStream(class Stream& /*data*/)    { return 0; }
    size_t write(uint8_t* /*data*/, size_t len)   { return len; }
    bool   end(bool /*evenIfRemaining*/ = false)  { return true; }
    void   abort()                                 { error_ = true; }
    bool   isFinished() const                     { return true; }
    bool   hasError() const                       { return error_; }
    uint8_t getError() const                      { return error_ ? 1 : 0; }
    const char* errorString()                     { return error_ ? "aborted" : ""; }
    void   printError(SerialClass& s)             { s.println(errorString()); }
    size_t size() const                            { return 0; }
    size_t progress() const                        { return 0; }
    size_t remaining() const                       { return 0; }
    void   runAsync(bool /*async*/)                {}
    void   setMD5(const char* /*expected*/)        {}
    String md5String()                             { return String(); }
private:
    bool   error_ = false;
};

extern UpdateClass Update;

// Update command constants — mirror ESP32 core values.
#define U_FLASH   0
#define U_SPIFFS  100
#define U_AUTH    200
