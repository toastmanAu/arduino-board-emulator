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

#ifdef __cplusplus
}
#endif
