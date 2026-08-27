#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstdio>
#include <cstring>
#include "Arduino.h"
#include "WString.h"

// Adafruit Thermal printer. Sketches construct `Adafruit_Thermal printer(&QR204)`
// where QR204 is a HardwareSerial(N) wired to a TTL thermal head. We forward
// every print/println/feed/format call to that UART so the bytes land in
// <project>/.boardghost/uart-N-out.bin (the UART capture file from sim_uart.cpp).
// You can `tail -f` that file to see receipts accumulate live, including ESC/POS
// formatting commands. Receipts are also mirrored to stdout for quick scanning.
//
// ESC/POS subset emitted: bold/underline/inverse/justify/feed/size. Enough that
// receipts look right when piped to a real thermal printer for offline replay.
class Adafruit_Thermal {
public:
    explicit Adafruit_Thermal(SerialClass* uart = nullptr, uint8_t /*dtr*/ = 255)
        : uart_(uart) {}

    void   begin(uint16_t /*heatTime*/ = 120)        { esc_("\x1B\x40", 2); /* ESC @ — reset */ }
    void   reset()                                   { esc_("\x1B\x40", 2); }
    // ESC 7 n1 n2 n3 — print-head heat config (dots, heat time, heat interval).
    // Real Adafruit_Thermal exposes this; begin() calls it internally with these
    // same defaults. Sketches that want to tune print darkness must call it
    // explicitly, because begin()'s single argument is a FIRMWARE VERSION flag,
    // not a heat setting. Emitted here so sim transcripts match the wire.
    void   setHeatConfig(uint8_t dots = 11, uint8_t time = 120, uint8_t interval = 40)
                                                     { const char b[5] = { 0x1B, '7', (char)dots, (char)time, (char)interval };
                                                       esc_(b, 5); }
    void   setDefault()                              { esc_("\x1B\x40", 2); }

    void   print(const char* s)                      { emit_(s); }
    void   print(const String& s)                    { emit_(s.c_str()); }
    void   println()                                 { emit_("\n"); }
    void   println(const char* s)                    { emit_(s); emit_("\n"); }
    void   println(const String& s)                  { emit_(s.c_str()); emit_("\n"); }
    void   println(int v)                            { char b[16]; snprintf(b,sizeof(b),"%d",v);   emit_(b); emit_("\n"); }
    void   println(long v)                           { char b[24]; snprintf(b,sizeof(b),"%ld",v);  emit_(b); emit_("\n"); }
    void   println(unsigned long v)                  { char b[24]; snprintf(b,sizeof(b),"%lu",v); emit_(b); emit_("\n"); }
    void   println(double v, int dec = 2)            { char b[32]; snprintf(b,sizeof(b),"%.*f",dec,v); emit_(b); emit_("\n"); }

    void   feed(uint8_t lines = 1)                   { for (uint8_t i = 0; i < lines; ++i) emit_("\n"); }
    void   feedRows(uint8_t /*rows*/)                { emit_("\n"); }
    void   flush()                                   { if (uart_) uart_->flush(); }

    // ESC/POS formatting commands — real printers respond to these inline;
    // the sim's capture file records them so a replay against a real printer
    // produces the same receipt.
    void   inverseOn()                               { esc_("\x1D\x42\x01", 3); }
    void   inverseOff()                              { esc_("\x1D\x42\x00", 3); }
    void   doubleHeightOn()                          { esc_("\x1B\x21\x10", 3); }
    void   doubleHeightOff()                         { esc_("\x1B\x21\x00", 3); }
    void   doubleWidthOn()                           { esc_("\x1B\x21\x20", 3); }
    void   doubleWidthOff()                          { esc_("\x1B\x21\x00", 3); }
    void   boldOn()                                  { esc_("\x1B\x45\x01", 3); }
    void   boldOff()                                 { esc_("\x1B\x45\x00", 3); }
    void   underlineOn(uint8_t weight = 1)           { uint8_t b[3]={0x1B,0x2D,weight}; esc_((const char*)b,3); }
    void   underlineOff()                            { esc_("\x1B\x2D\x00", 3); }
    void   strikeOn()                                { esc_("\x1B\x47\x01", 3); }
    void   strikeOff()                               { esc_("\x1B\x47\x00", 3); }
    void   normal()                                  { esc_("\x1B\x21\x00", 3); }
    void   justify(char c) {
        uint8_t code = (c=='C'||c=='c') ? 1 : (c=='R'||c=='r') ? 2 : 0;
        uint8_t b[3]={0x1B,0x61,code}; esc_((const char*)b,3);
    }
    void   setFont(char /*font*/)                    {}
    void   setSize(char s) {
        // Adafruit Thermal sizes: 'S' (small), 'M', 'L'.
        uint8_t code = (s=='L'||s=='l') ? 0x11 : (s=='M'||s=='m') ? 0x01 : 0x00;
        uint8_t b[3]={0x1D,0x21,code}; esc_((const char*)b,3);
    }
    void   setLineHeight(int /*val*/ = 30)           {}
    void   setCharSpacing(int /*spacing*/ = 0)       {}
    void   setBarcodeHeight(uint8_t /*val*/ = 50)    {}
    void   printBarcode(const char* text, uint8_t /*type*/) {
        if (text) { emit_("[BARCODE:"); emit_(text); emit_("]\n"); }
    }
    void   printBarcodeAlt(const char* text, uint8_t /*type*/) { printBarcode(text, 0); }

    void   sleep()                                   {}
    void   sleepAfter(uint16_t /*s*/)                {}
    void   wake()                                    {}
    void   tab()                                     { emit_("\t"); }

    bool   hasPaper()                                { return true; }

private:
    SerialClass* uart_;
    // emit_: forward through the wrapped UART (so it lands in
    // uart-N-out.bin) AND mirror to stdout for at-a-glance debugging.
    void emit_(const char* s) {
        if (!s) return;
        size_t n = std::strlen(s);
        if (uart_ && n > 0) uart_->write((const uint8_t*)s, n);
        std::fputs(s, stdout);
        std::fflush(stdout);
    }
    // esc_: raw bytes (incl. zeros) through the UART; not mirrored to
    // stdout since formatting commands aren't human-meaningful.
    void esc_(const char* bytes, size_t n) {
        if (uart_ && n > 0) uart_->write((const uint8_t*)bytes, n);
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
