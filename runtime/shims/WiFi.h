#pragma once
#include <stdint.h>
#include "WString.h"
#include "WiFiClient.h"

// wl_status_t enum (matches ESP32 Arduino core).
typedef enum {
    WL_NO_SHIELD       = 255,
    WL_IDLE_STATUS     = 0,
    WL_NO_SSID_AVAIL   = 1,
    WL_SCAN_COMPLETED  = 2,
    WL_CONNECTED       = 3,
    WL_CONNECT_FAILED  = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED    = 6,
} wl_status_t;

// IPAddress — minimal subset matching Arduino's API.
class IPAddress {
public:
    IPAddress() : a_(0), b_(0), c_(0), d_(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : a_(a), b_(b), c_(c), d_(d) {}
    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a_, b_, c_, d_);
        return String(buf);
    }
    operator uint32_t() const { return ((uint32_t)a_ << 24) | ((uint32_t)b_ << 16) | ((uint32_t)c_ << 8) | d_; }
private:
    uint8_t a_, b_, c_, d_;
};

class WiFiClass {
public:
    wl_status_t  begin(const char* ssid = nullptr, const char* passphrase = nullptr);
    wl_status_t  begin(const String& ssid, const String& passphrase) { return begin(ssid.c_str(), passphrase.c_str()); }
    int          disconnect(bool wifioff = false);
    wl_status_t  status();
    bool         isConnected();
    IPAddress    localIP();
    IPAddress    gatewayIP();
    IPAddress    subnetMask();
    String       SSID();
    String       macAddress();
    int          RSSI();
    void         mode(int /*m*/) {}
    void         setSleep(bool /*s*/) {}
    void         setHostname(const char* /*h*/) {}
};

extern WiFiClass WiFi;

#define WIFI_OFF     0
#define WIFI_STA     1
#define WIFI_AP      2
#define WIFI_AP_STA  3
