#pragma once
#include <stdint.h>
#include <memory>
#include "WString.h"

namespace boardghost_internal { class MDnsImpl; }

// ESP32 ESPmDNS shim.
//
// When avahi-daemon is running and `avahi-publish-service` / `avahi-publish`
// are on PATH (true on most desktop Linux), this shim spawns those CLI tools
// to publish a real LAN-visible service. Other devices on the network can
// then resolve <hostname>.local and discover advertised services via standard
// mDNS browsers.
//
// Set BOARDGHOST_MDNS=off to disable (useful in CI / headless / no-avahi).
// Each begin()/addService() returns true even when avahi isn't installed —
// the sketch's "I'm reachable" flow doesn't break in unconfigured envs;
// you just lose the network announce.
class MDNSResponder {
public:
    MDNSResponder();
    ~MDNSResponder();

    MDNSResponder(const MDNSResponder&) = delete;
    MDNSResponder& operator=(const MDNSResponder&) = delete;

    bool begin(const char* hostname);
    bool begin(const String& h)                  { return begin(h.c_str()); }
    void end();
    bool addService(const char* service, const char* proto, uint16_t port);
    bool addService(const String& s, const String& p, uint16_t port) {
        return addService(s.c_str(), p.c_str(), port);
    }
    // TXT records are accepted but not yet propagated to the published
    // service — avahi-publish-service takes them positionally and re-publish
    // every addServiceTxt would be expensive. Most sketches use these as
    // metadata; consumers can still connect via the host:port advertised.
    bool addServiceTxt(const char* /*service*/, const char* /*proto*/,
                       const char* /*key*/,     const char* /*value*/) { return true; }
    // Advertise the _arduino._tcp service so arduino-cli / the IDE network
    // port discovers this sketch as an OTA target. Real impl in sim_mdns.cpp.
    void enableArduino(uint16_t port = 3232, bool auth = false);

private:
    std::unique_ptr<boardghost_internal::MDnsImpl> impl_;
};

extern MDNSResponder MDNS;
