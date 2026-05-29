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

// wifi_auth_mode_t — matches ESP32 Arduino core's WiFi authentication enum.
typedef enum {
    WIFI_AUTH_OPEN              = 0,
    WIFI_AUTH_WEP               = 1,
    WIFI_AUTH_WPA_PSK           = 2,
    WIFI_AUTH_WPA2_PSK          = 3,
    WIFI_AUTH_WPA_WPA2_PSK      = 4,
    WIFI_AUTH_WPA2_ENTERPRISE   = 5,
    WIFI_AUTH_WPA3_PSK          = 6,
    WIFI_AUTH_WPA2_WPA3_PSK     = 7,
    WIFI_AUTH_WAPI_PSK          = 8,
    WIFI_AUTH_MAX               = 9,
} wifi_auth_mode_t;

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

    // M2.D — scan API. Stub returns 0 (no networks seen) in all modes; per-index
    // accessors return safe defaults. Sketches that iterate the scan results
    // simply find zero entries and skip the loop.
    int16_t          scanNetworks(bool /*async*/ = false, bool /*show_hidden*/ = false,
                                  bool /*passive*/ = false, uint32_t /*max_ms_per_chan*/ = 300) { return 0; }
    int              RSSI(uint8_t /*idx*/)            { return 0; }
    int32_t          channel(uint8_t /*idx*/)         { return 0; }
    wifi_auth_mode_t encryptionType(uint8_t /*idx*/)  { return WIFI_AUTH_OPEN; }
    String           SSID(uint8_t /*idx*/)            { return String(); }
    String           BSSIDstr(uint8_t /*idx*/)        { return String(); }
    void             scanDelete()                     {}
};

extern WiFiClass WiFi;

#define WIFI_OFF     0
#define WIFI_STA     1
#define WIFI_AP      2
#define WIFI_AP_STA  3
