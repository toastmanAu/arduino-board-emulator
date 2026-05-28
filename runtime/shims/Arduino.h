#pragma once
#include <stdint.h>
#include <stddef.h>

// Pull in the sim-runtime public API so user sketches don't have to
// include "sim_runtime.h" explicitly to call sim_set_active_display,
// sim_screenshot, sim_log_*, etc.
#include "sim_runtime.h"

#ifdef __cplusplus
#include "WString.h"
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
