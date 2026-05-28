#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WString.h"

class WiFiClient {
public:
    WiFiClient() = default;
    virtual ~WiFiClient() = default;

    virtual int  connect(const char* host, uint16_t port);
    virtual int  connect(const String& host, uint16_t port) { return connect(host.c_str(), port); }
    virtual void stop();
    virtual bool connected();
    virtual int  available()                 { return 0; }
    virtual int  read()                      { return -1; }
    virtual int  read(uint8_t* buf, size_t n);
    virtual size_t write(uint8_t b)          { (void)b; return 1; }
    virtual size_t write(const uint8_t* buf, size_t n) { (void)buf; return n; }
    virtual void flush()                     {}
    virtual void setTimeout(uint32_t /*ms*/) {}
    virtual operator bool() const            { return connected_; }

protected:
    bool connected_ = false;
    // Backing storage for canned responses in fake mode etc.
};
