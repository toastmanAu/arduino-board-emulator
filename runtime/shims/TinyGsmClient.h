#pragma once
#include <cstdint>
#include "WString.h"
#include "WiFiClient.h"

// Minimal stub for TinyGsm. Real lib has many modem-specific subclasses
// (A7670, SIM800, etc.) — they all share the same TinyGsm public surface,
// so a single stub is sufficient to compile sketches.
class TinyGsm {
public:
    template <typename T>
    TinyGsm(T&) {}
    TinyGsm() {}

    bool   init(const char* /*pin*/ = nullptr)        { return false; }
    bool   restart()                                   { return false; }
    bool   testAT(uint32_t /*timeout*/ = 10000)        { return false; }
    bool   isNetworkConnected()                        { return false; }
    bool   isGprsConnected()                           { return false; }
    bool   waitForNetwork(uint32_t /*timeout*/ = 60000){ return false; }
    bool   gprsConnect(const char* /*apn*/, const char* /*user*/ = nullptr, const char* /*pw*/ = nullptr) { return false; }
    bool   gprsDisconnect()                            { return false; }

    String getModemInfo()  { return String("BoardGhost-TinyGsm-stub"); }
    String getOperator()   { return String(""); }
    String getIMEI()       { return String("000000000000000"); }
    int    getSignalQuality() { return 0; }
};

class TinyGsmClient : public WiFiClient {
public:
    template <typename T>
    TinyGsmClient(T& /*modem*/) {}
    TinyGsmClient() = default;
};
