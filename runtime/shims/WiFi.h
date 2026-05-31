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

// WiFiEvent_t — subset of ESP32 Arduino core's `arduino_event_id_t` covering
// the events sketches most commonly register against. Values mirror the real
// enum to keep `case ARDUINO_EVENT_WIFI_STA_CONNECTED:` compile-compatible.
typedef enum {
    ARDUINO_EVENT_WIFI_READY = 0,
    ARDUINO_EVENT_WIFI_SCAN_DONE,
    ARDUINO_EVENT_WIFI_STA_START,
    ARDUINO_EVENT_WIFI_STA_STOP,
    ARDUINO_EVENT_WIFI_STA_CONNECTED,
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
    ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE,
    ARDUINO_EVENT_WIFI_STA_GOT_IP,
    ARDUINO_EVENT_WIFI_STA_GOT_IP6,
    ARDUINO_EVENT_WIFI_STA_LOST_IP,
    ARDUINO_EVENT_MAX,
} WiFiEvent_t;

// WiFiEventInfo_t — opaque union mirroring ESP32's event-info payload.
// We expose just the structurally-required fields. Sketches that read
// specific fields (e.g. `info.wifi_sta_disconnected.reason`) get safe
// defaults from value-initialisation in the simulator.
typedef union {
    struct { uint8_t  ssid[33];  uint8_t  ssid_len; uint8_t  bssid[6];  uint8_t  channel;  uint8_t  authmode; uint16_t aid; } wifi_sta_connected;
    struct { uint8_t  ssid[33];  uint8_t  ssid_len; uint8_t  bssid[6];  uint8_t  reason;   int8_t   rssi; }                 wifi_sta_disconnected;
    struct { uint32_t status;    uint8_t  number;   uint8_t  scan_id; }                                                      wifi_scan_done;
    struct { uint32_t ip;        uint32_t netmask;  uint32_t gw; }                                                           got_ip;
} WiFiEventInfo_t;

using WiFiEventCb         = void(*)(WiFiEvent_t);
using WiFiEventFullCb     = void(*)(WiFiEvent_t, WiFiEventInfo_t);
using WiFiEventId_t       = uint32_t;

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

    // Event-callback registration — accepts both the legacy (event-only) and
    // full (event + info) signatures. Real hardware fires these on state
    // transitions; in fake mode the sim never invokes them (no event loop).
    // Stubs return a synthetic event id so sketches that store it for later
    // `removeEvent()` calls still compile.
    WiFiEventId_t    onEvent(WiFiEventCb     /*cb*/, WiFiEvent_t /*event*/ = ARDUINO_EVENT_MAX) { return 0; }
    WiFiEventId_t    onEvent(WiFiEventFullCb /*cb*/, WiFiEvent_t /*event*/ = ARDUINO_EVENT_MAX) { return 0; }
    void             removeEvent(WiFiEventId_t /*id*/) {}

    // Configuration knobs — sketches set these but the simulator can't honor
    // them meaningfully. Stubs preserve compile compatibility.
    bool             config(IPAddress /*local*/, IPAddress /*gw*/, IPAddress /*subnet*/,
                            IPAddress /*dns1*/ = IPAddress(), IPAddress /*dns2*/ = IPAddress()) { return true; }
    bool             setAutoReconnect(bool /*enable*/)            { return true; }
    bool             setAutoConnect(bool /*enable*/)              { return true; }
    bool             persistent(bool /*on*/)                      { return true; }
    bool             reconnect()                                   { return true; }

    // Blocking variant — returns the final status after up to timeout_ms.
    // In sim's fake mode `begin()` already returns WL_CONNECTED so we just
    // re-poll status() and hand it back; in fail mode it returns FAILED.
    wl_status_t      waitForConnectResult(unsigned long /*timeout_ms*/ = 60000UL) { return status(); }
};

extern WiFiClass WiFi;

#define WIFI_OFF     0
#define WIFI_STA     1
#define WIFI_AP      2
#define WIFI_AP_STA  3
