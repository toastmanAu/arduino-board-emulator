// Small instance definitions for header-only no-op shims that still need
// a single translation unit to host their `extern`-declared singletons
// (MDNS, Update). Kept in one file to avoid sim_xxxx.cpp sprawl per shim.

#include "ESPmDNS.h"
#include "Update.h"
#include "Arduino.h"

#include <cstdio>
#include <cstdlib>

MDNSResponder MDNS;
UpdateClass   Update;
ESPClass      ESP;

// ESP.restart() on real hardware reboots the chip. In the sim we exit with a
// distinctive status code so the launcher can show "sketch requested restart"
// rather than treating it as a crash. Sketches commonly chain restart() to
// recovery / OTA paths where the only sensible local behaviour is to stop.
void ESPClass::restart() {
    std::fputs("[BoardGhost] ESP.restart() called — exiting sim\n", stdout);
    std::fflush(stdout);
    std::exit(75);  // matches BSDsysexits EX_TEMPFAIL — "transient, retry"
}
