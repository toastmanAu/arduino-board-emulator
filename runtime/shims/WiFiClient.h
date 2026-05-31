#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WString.h"
#include "Arduino.h"  // Stream base class

// WiFiClient inherits from Stream to match ESP32 Arduino core. Code paths like
// `Update.writeStream(*wifiClient)` rely on the upcast working through the
// shared `Stream&` signature.
class WiFiClient : public Stream {
public:
    WiFiClient() = default;
    ~WiFiClient() override = default;

    virtual int  connect(const char* host, uint16_t port);
    virtual int  connect(const String& host, uint16_t port) { return connect(host.c_str(), port); }
    virtual void stop();
    virtual bool connected();
    int    available() override                       { return 0; }
    int    read() override                            { return -1; }
    int    read(uint8_t* buf, size_t n) override;
    size_t write(uint8_t b) override                  { (void)b; return 1; }
    size_t write(const uint8_t* buf, size_t n) override { (void)buf; return n; }
    void   flush() override                            {}
    void   setTimeout(uint32_t /*ms*/) override        {}
    virtual operator bool() const                     { return connected_; }

protected:
    bool connected_ = false;
};
