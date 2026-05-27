#pragma once
#include <stdint.h>
#include <stddef.h>

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
