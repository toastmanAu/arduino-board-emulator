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

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int  digitalRead(uint8_t pin);

int  analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int value);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class SerialClass {
public:
    void begin(unsigned long /*baud*/) {}
    void end() {}

    size_t print(const char* s);
    size_t print(const String& s);
    size_t print(int v);
    size_t print(unsigned int v);
    size_t print(long v);
    size_t print(unsigned long v);
    size_t print(double v, int decimals = 2);
    size_t print(char c);

    size_t println();
    size_t println(const char* s);
    size_t println(const String& s);
    size_t println(int v);
    size_t println(unsigned int v);
    size_t println(long v);
    size_t println(unsigned long v);
    size_t println(double v, int decimals = 2);

    size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    size_t write(uint8_t b);
    size_t write(const uint8_t* buf, size_t n);

    int    available();
    int    read();
    void   flush();
};

extern SerialClass Serial;
extern SerialClass Serial1;
extern SerialClass Serial2;
#endif

#ifdef __cplusplus
extern "C" {
#endif
// yield() — Arduino cooperative multitasking hook; no-op in sim.
inline void yield(void) {}
#ifdef __cplusplus
}
#endif
