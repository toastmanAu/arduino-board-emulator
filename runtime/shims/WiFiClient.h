#pragma once
#include <stdint.h>
#include <stddef.h>
#include <memory>
#include "WString.h"
#include "Arduino.h"  // Stream base class

// Shared-ownership POSIX socket handle. Sketches often copy WiFiClient by
// value (e.g. into a vector or std::function capture); shared_ptr means the
// fd is closed exactly once when the last copy goes out of scope, matching
// the implicit RAII contract of Arduino ESP32's WiFiClient.
struct BoardghostSocket {
    int fd = -1;
    ~BoardghostSocket();
};

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
    virtual bool connected() const;
    int    available() override;
    int    read() override;
    int    read(uint8_t* buf, size_t n) override;
    size_t write(uint8_t b) override;
    size_t write(const uint8_t* buf, size_t n) override;
    void   flush() override                            {}
    // Arduino's Stream::setTimeout is milliseconds; ESP32 sketches commonly
    // set this before reads from network streams.
    void   setTimeout(uint32_t ms) override            { timeout_ms_ = ms; reapply_timeout(); }
    virtual operator bool() const                     { return connected(); }

protected:
    // Apply timeout_ms_ to the live socket via SO_RCVTIMEO/SO_SNDTIMEO. Safe
    // to call before connect (no-ops when no socket).
    void reapply_timeout();

    std::shared_ptr<BoardghostSocket> sock_;
    // Default 30s matches Arduino ESP32's default Stream::setTimeout(1000)?
    // Actually Arduino's default is 1000ms. Mirror that so behaviour matches.
    uint32_t timeout_ms_ = 1000;
    // Legacy fake-mode flag — used when BOARDGHOST_NET=fake so sketches that
    // just check connected() without doing real I/O still see "connected".
    bool fake_connected_ = false;
};
