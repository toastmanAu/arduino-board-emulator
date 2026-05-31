#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstdio>
#include "Arduino.h"
#include "WString.h"

// Adafruit Thermal printer — no-op shim. Receipts are printed to the host
// log instead of an actual thermal head, so sketches can exercise their
// receipt-formatting code paths in the sim. Every print/feed call is mirrored
// to a sim-side log line so behaviour can be inspected via the launcher.
class Adafruit_Thermal {
public:
    explicit Adafruit_Thermal(SerialClass* uart = nullptr, uint8_t /*dtr*/ = 255)
        : uart_(uart) {}

    void   begin(uint16_t /*heatTime*/ = 120)        {}
    void   reset()                                   {}
    void   setDefault()                              {}

    void   print(const char* s)                      { log_(s); }
    void   print(const String& s)                    { log_(s.c_str()); }
    void   println()                                 { log_("\n"); }
    void   println(const char* s)                    { log_(s); log_("\n"); }
    void   println(const String& s)                  { log_(s.c_str()); log_("\n"); }
    void   println(int v)                            { char b[16]; snprintf(b,sizeof(b),"%d",v);   log_(b); log_("\n"); }
    void   println(long v)                           { char b[24]; snprintf(b,sizeof(b),"%ld",v);  log_(b); log_("\n"); }
    void   println(unsigned long v)                  { char b[24]; snprintf(b,sizeof(b),"%lu",v); log_(b); log_("\n"); }
    void   println(double v, int dec = 2)            { char b[32]; snprintf(b,sizeof(b),"%.*f",dec,v); log_(b); log_("\n"); }

    void   feed(uint8_t /*lines*/ = 1)               { log_("\n"); }
    void   feedRows(uint8_t /*rows*/)                { log_("\n"); }
    void   flush()                                   {}

    void   inverseOn()                               {}
    void   inverseOff()                              {}
    void   doubleHeightOn()                          {}
    void   doubleHeightOff()                         {}
    void   doubleWidthOn()                           {}
    void   doubleWidthOff()                          {}
    void   boldOn()                                  {}
    void   boldOff()                                 {}
    void   underlineOn(uint8_t /*weight*/ = 1)       {}
    void   underlineOff()                            {}
    void   strikeOn()                                {}
    void   strikeOff()                               {}
    void   normal()                                  {}
    void   justify(char /*c*/)                       {}
    void   setFont(char /*font*/)                    {}
    void   setSize(char /*size*/)                    {}
    void   setLineHeight(int /*val*/ = 30)           {}
    void   setCharSpacing(int /*spacing*/ = 0)       {}
    void   setBarcodeHeight(uint8_t /*val*/ = 50)    {}
    void   printBarcode(const char* /*text*/, uint8_t /*type*/)     {}
    void   printBarcodeAlt(const char* /*text*/, uint8_t /*type*/)  {}

    void   sleep()                                   {}
    void   sleepAfter(uint16_t /*s*/)                {}
    void   wake()                                    {}
    void   tab()                                     { log_("\t"); }

    bool   hasPaper()                                { return true; }

private:
    SerialClass* uart_;
    void log_(const char* s) {
        // Receipts can be very long; flush each chunk so launcher tail-follow
        // sees output line-by-line instead of buffered at exit.
        std::fputs(s, stdout);
        std::fflush(stdout);
    }
};

// Barcode types — values match Adafruit_Thermal.
#define UPC_A   0
#define UPC_E   1
#define EAN13   2
#define EAN8    3
#define CODE39  4
#define ITF     5
#define CODABAR 6
#define CODE93  7
#define CODE128 8
