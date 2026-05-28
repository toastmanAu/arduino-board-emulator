#include "WiFi.h"
#include "WiFiClient.h"
#include "sim_net.h"
#include <cstdio>
#include <cstring>

WiFiClass WiFi;

wl_status_t WiFiClass::begin(const char* /*ssid*/, const char* /*pass*/) {
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAKE: return WL_CONNECTED;
        case BOARDGHOST_NET_FAIL: return WL_NO_SSID_AVAIL;
        case BOARDGHOST_NET_REAL: return WL_CONNECTED;   // real-mode "associate" is a no-op; libcurl handles its own DNS
    }
    return WL_DISCONNECTED;
}

int WiFiClass::disconnect(bool /*wifioff*/) { return 1; }

wl_status_t WiFiClass::status() {
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAIL: return WL_DISCONNECTED;
        default:                  return WL_CONNECTED;
    }
}

bool WiFiClass::isConnected() { return status() == WL_CONNECTED; }

IPAddress WiFiClass::localIP()    { return IPAddress(127, 0, 0, 1); }
IPAddress WiFiClass::gatewayIP()  { return IPAddress(127, 0, 0, 1); }
IPAddress WiFiClass::subnetMask() { return IPAddress(255, 255, 255, 0); }

String WiFiClass::SSID()       { return String("boardghost-sim"); }
String WiFiClass::macAddress() { return String("DE:AD:BE:EF:00:01"); }

int WiFiClass::RSSI() {
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAIL: return 0;
        default:                  return -42;
    }
}

// --- WiFiClient ---

int WiFiClient::connect(const char* host, uint16_t port) {
    (void)host; (void)port;
    if (sim_net_mode() == BOARDGHOST_NET_FAIL) {
        connected_ = false;
        return 0;
    }
    connected_ = true;
    return 1;
}

void WiFiClient::stop() { connected_ = false; }
bool WiFiClient::connected() { return connected_; }

int WiFiClient::read(uint8_t* buf, size_t n) {
    (void)buf; (void)n;
    return 0;   // no incoming data in fake mode
}
