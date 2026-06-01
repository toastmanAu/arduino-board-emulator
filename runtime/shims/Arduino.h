#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Pull in the sim-runtime public API so user sketches don't have to
// include "sim_runtime.h" explicitly to call sim_set_active_display,
// sim_screenshot, sim_log_*, etc.
#include "sim_runtime.h"

#ifdef __cplusplus
#include "WString.h"
#endif

// ESP32 Arduino core implicitly pulls in heap_caps + FreeRTOS task/types.
// Sketches reach for heap_caps_malloc / TaskHandle_t / etc. without an
// explicit #include — match that convention.
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif
// configTime / getLocalTime — NTP/timezone helpers from the ESP32 Arduino
// core. On host we honour gmtOffset by setting TZ, ignore the NTP server,
// and let `time()` / `localtime_r()` answer with system time. Forward decl
// only; impl lives in sim_runtime.cpp.
#include <time.h>
void configTime(long gmtOffset_sec, int daylightOffset_sec,
                const char* server1,
                const char* server2 = nullptr,
                const char* server3 = nullptr);
bool getLocalTime(struct tm* info, uint32_t ms = 5000);
#ifdef __cplusplus
}
#endif

// AVR / ESP flash-memory annotations — no-ops on the host. Arduino libraries
// (Time, ArduinoJson, many others) sprinkle these on string tables.
//
// We deliberately AVOID macro-defining function names like memcpy_P, strcpy_P:
// LovyanGFX's utility/pgmspace.h declares those as real functions, and a
// preprocessor macro would mangle that declaration. If a sketch needs them,
// they're available as inline functions further down (see below).
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif
#ifndef F
#define F(s) (s)
#endif
// pgm_read_* — on real Arduino these read from flash via a special
// instruction; on host they're plain dereferences. Data-only macros, so no
// collision with LovyanGFX's pgmspace function declarations.
#ifndef pgm_read_byte
#define pgm_read_byte(addr)     (*reinterpret_cast<const uint8_t*>(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr)     (*reinterpret_cast<const uint16_t*>(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr)    (*reinterpret_cast<const uint32_t*>(addr))
#endif
#ifndef pgm_read_float
#define pgm_read_float(addr)    (*reinterpret_cast<const float*>(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr)      (*reinterpret_cast<const void* const*>(addr))
#endif

// Inline flash-string helpers. These are FUNCTIONS, not macros, so they
// don't textually mangle name-collisions in third-party headers.
// memcpy_P/memcmp_P are intentionally NOT defined here — LovyanGFX's
// utility/pgmspace.h owns those declarations in some compile contexts and
// would conflict. WProgram.h provides them for legacy libraries that need
// memcpy_P (those libraries don't also pull LovyanGFX in).
#ifdef __cplusplus
inline char*  strcpy_P(char* dest, const char* src)   { return strcpy(dest, src); }
inline size_t strlen_P(const char* s)                 { return strlen(s); }
inline int    strcmp_P(const char* a, const char* b)  { return strcmp(a, b); }
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint32_t millis(void);
uint32_t micros(void);
void     delay(uint32_t ms);
void     delayMicroseconds(uint32_t us);

// Pin levels and modes — match Arduino values.
#define LOW    0
#define HIGH   1
#define INPUT        0x0
#define OUTPUT       0x1
#define INPUT_PULLUP 0x2

// `byte` is an Arduino-core typedef — sketches use it interchangeably with
// uint8_t. `std::byte` would shadow it under C++17 so we typedef explicitly.
typedef uint8_t byte;

// Print radix constants used by Serial.print(v, BASE). Values match the
// Arduino core Print API.
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

// Arduino ctype helpers — thin wrappers around <ctype.h> with the
// PascalCase names Arduino sketches use (isDigit, isAlpha, etc.).
#include <ctype.h>
static inline int isAlpha(int c)        { return isalpha(c); }
static inline int isAlphaNumeric(int c) { return isalnum(c); }
static inline int isAscii(int c)        { return (c & ~0x7f) == 0; }
static inline int isControl(int c)      { return iscntrl(c); }
static inline int isDigit(int c)        { return isdigit(c); }
static inline int isGraph(int c)        { return isgraph(c); }
static inline int isHexadecimalDigit(int c) { return isxdigit(c); }
static inline int isLowerCase(int c)    { return islower(c); }
static inline int isPrintable(int c)    { return isprint(c); }
static inline int isPunct(int c)        { return ispunct(c); }
static inline int isSpace(int c)        { return isspace(c); }
static inline int isUpperCase(int c)    { return isupper(c); }
static inline int isWhitespace(int c)   { return c == ' ' || c == '\t'; }

// esp_random — ESP32 hardware RNG. The sim uses rand() seeded by sim_main.
uint32_t esp_random(void);

// ESP32 LEDC PWM API — back the sim with no-ops. Sketches use these for
// piezo buzzer pitches and backlight; the sim has no audio output and the
// backlight is fixed-on, so calls are silent. Keeps tone-playing setup() code
// from failing to compile.
void  ledcSetup(uint8_t channel, double freq, uint8_t resolution_bits);
void  ledcAttachPin(uint8_t pin, uint8_t channel);
void  ledcDetachPin(uint8_t pin);
void  ledcWrite(uint8_t channel, uint32_t duty);
void  ledcWriteTone(uint8_t channel, double freq);
void  ledcWriteNote(uint8_t channel, uint8_t note, uint8_t octave);
uint32_t ledcRead(uint8_t channel);

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int  digitalRead(uint8_t pin);

int  analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int value);

#ifdef __cplusplus
}
#endif

// UART config flags — ESP32 Arduino core constants. Values mirror the real
// enum so equality comparisons in sketch code stay valid; the sim ignores them.
#define SERIAL_5N1 0x8000010
#define SERIAL_6N1 0x8000014
#define SERIAL_7N1 0x8000018
#define SERIAL_8N1 0x800001c
#define SERIAL_5N2 0x8000030
#define SERIAL_6N2 0x8000034
#define SERIAL_7N2 0x8000038
#define SERIAL_8N2 0x800003c

#ifdef __cplusplus
class SerialClass {
public:
    void begin(unsigned long /*baud*/) {}
    // ESP32 HardwareSerial overload: baud, config, rx, tx, [invert].
    void begin(unsigned long /*baud*/, uint32_t /*config*/,
               int8_t /*rxPin*/ = -1, int8_t /*txPin*/ = -1,
               bool /*invert*/ = false, unsigned long /*timeout_ms*/ = 20000UL) {}
    void end() {}

    size_t print(const char* s);
    size_t print(const String& s);
    size_t print(int v);
    size_t print(unsigned int v);
    size_t print(long v);
    size_t print(unsigned long v);
    size_t print(double v, int decimals = 2);
    size_t print(char c);
    // Radix overloads — e.g. Serial.print(0xff, HEX) renders "ff".
    size_t print(int v, int base);
    size_t print(unsigned int v, int base);
    size_t print(long v, int base);
    size_t print(unsigned long v, int base);

    size_t println();
    size_t println(const char* s);
    size_t println(const String& s);
    size_t println(int v);
    size_t println(unsigned int v);
    size_t println(long v);
    size_t println(unsigned long v);
    size_t println(double v, int decimals = 2);
    size_t println(int v, int base);
    size_t println(unsigned int v, int base);
    size_t println(long v, int base);
    size_t println(unsigned long v, int base);

    size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    // virtual so HardwareSerial can route I/O through real backends
    // (FIFOs for QR-scanner-style inputs, files for printer-style outputs)
    // while the global `Serial` instance keeps using stdin/stdout.
    virtual size_t write(uint8_t b);
    virtual size_t write(const uint8_t* buf, size_t n);

    virtual int    available();
    virtual int    read();
    virtual int    peek() { return -1; }
    virtual ~SerialClass() = default;
    void   flush();
    void   setDebugOutput(bool /*enable*/) {}
};

extern SerialClass    Serial;
// Serial1/Serial2 stand in for the secondary UARTs; they're HardwareSerial-
// typed so reads don't poll stdin (which would let any unrelated terminal
// input pollute an external-peripheral protocol).
class HardwareSerial;
extern HardwareSerial Serial1;
extern HardwareSerial Serial2;

// Stream — Arduino's abstract base for byte-oriented IO (Serial, WiFiClient,
// File, etc. all derive from it on real hardware). The shim version exposes
// the read/write/available subset that Arduino library code reaches for, with
// default no-op bodies. Concrete shims (WiFiClient, etc.) override what they
// support. Update.writeStream(Stream&) and similar signatures take this type.
class Stream {
public:
    virtual ~Stream() = default;
    virtual int    available()                          { return 0; }
    virtual int    read()                               { return -1; }
    virtual int    read(uint8_t* /*buf*/, size_t /*n*/) { return 0; }
    virtual int    peek()                               { return -1; }
    virtual size_t write(uint8_t /*b*/)                 { return 0; }
    virtual size_t write(const uint8_t* /*buf*/, size_t n) { return n; }
    virtual void   flush()                              {}
    virtual void   setTimeout(uint32_t /*ms*/)          {}
};

// ESP — chip-control singleton. Only restart() is reached by the sketches we
// care about so far; the sim exits with a recognizable status so launchers
// can distinguish a deliberate restart from a crash.
class ESPClass {
public:
    [[noreturn]] void restart();  // implementation in sim_misc.cpp
    uint32_t getFreeHeap() const         { return 200 * 1024; }
    uint32_t getMinFreeHeap() const      { return 150 * 1024; }
    uint32_t getMaxAllocHeap() const     { return 100 * 1024; }
    uint32_t getHeapSize() const         { return 320 * 1024; }
    uint32_t getCpuFreqMHz() const       { return 240; }
    uint64_t getEfuseMac() const         { return 0x0123456789abULL; }
    void     deepSleep(uint64_t /*us*/)  {}
};

extern ESPClass ESP;

// ESP32 Arduino core's `HardwareSerial` exposes the same Stream API as
// SerialClass plus a port-indexed constructor (`HardwareSerial(1)`, etc.).
// Sketches commonly declare `HardwareSerial qr(1);` for an external UART
// peripheral (QR scanner, thermal printer, GPS, modem). The sim has no
// physical UART, so reads always report "no data" — preventing infinite
// loops where the sketch polls a missing scanner forever. Writes go to the
// sim log so the protocol bytes the sketch sends are visible.
// HardwareSerial: real bidirectional UART backing.
//
// Read direction (sketch <-- peripheral, e.g. QR scanner on UART2):
//   The shim opens a FIFO at <project>/.boardghost/uart-N-in.fifo (creating
//   it if needed). Anything you write to that FIFO from another shell shows
//   up as bytes the sketch reads via available()/read(). Use
//   `boardghost uart inject --port N <text>` (or `printf '...' > .fifo`)
//   to drive a scanner workflow.
//
// Write direction (sketch --> peripheral, e.g. thermal printer on UART1):
//   All bytes the sketch writes get appended to
//   <project>/.boardghost/uart-N-out.bin so you can `tail -f` it to see
//   what the sketch is sending — including binary protocols like the
//   thermal printer's ESC/POS commands.
class HardwareSerial : public SerialClass {
public:
    explicit HardwareSerial(int uart_nr = 0) : uart_nr_(uart_nr) {}
    int  port() const { return uart_nr_; }

    int    available() override;
    int    read() override;
    int    peek() override;
    int    read(uint8_t* buf, size_t n);    // ESP32 extension; not in upstream Stream
    size_t write(uint8_t b) override;
    size_t write(const uint8_t* buf, size_t n) override;

private:
    int uart_nr_;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif
// yield() — Arduino cooperative multitasking hook; no-op in sim.
inline void yield(void) {}
#ifdef __cplusplus
}
#endif
