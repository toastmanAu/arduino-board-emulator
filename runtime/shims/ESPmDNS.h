#pragma once
#include <stdint.h>
#include "WString.h"

// ESP32 mDNS — no-op shim. The sim has no LAN announce path; sketches that
// register a service still compile and the calls succeed silently.
class MDNSResponder {
public:
    bool begin(const char* /*hostname*/)         { return true; }
    bool begin(const String& h)                  { return begin(h.c_str()); }
    void end()                                   {}
    bool addService(const char* /*service*/,
                    const char* /*proto*/,
                    uint16_t   /*port*/)         { return true; }
    bool addService(const String& s, const String& p, uint16_t port) {
        return addService(s.c_str(), p.c_str(), port);
    }
    bool addServiceTxt(const char* /*service*/, const char* /*proto*/,
                       const char* /*key*/,     const char* /*value*/) { return true; }
    void enableArduino(uint16_t /*port*/ = 3232, bool /*auth*/ = false) {}
};

extern MDNSResponder MDNS;
