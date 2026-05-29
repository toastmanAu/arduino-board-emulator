// FreeRTOS.h — minimal sim shim for ESP32 sketches that reference FreeRTOS
// types or call its scheduler API. On the host we map task creation to
// std::thread and delays to std::this_thread::sleep_for. Most sketches just
// need a few type aliases and stubs to compile cleanly.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Base FreeRTOS types — all opaque on the host.
typedef uint32_t TickType_t;
typedef int      BaseType_t;
typedef unsigned UBaseType_t;
typedef void*    TaskHandle_t;

// portMAX_DELAY — wait indefinitely. Picked a sentinel that won't loop forever
// if a sketch accidentally uses it as a duration.
#ifndef portMAX_DELAY
#define portMAX_DELAY    ((TickType_t)0xFFFFFFFFUL)
#endif

#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 1000
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#endif

#ifndef pdTRUE
#define pdTRUE  1
#endif
#ifndef pdFALSE
#define pdFALSE 0
#endif
#ifndef pdPASS
#define pdPASS  1
#endif
#ifndef pdFAIL
#define pdFAIL  0
#endif

#ifdef __cplusplus
}
#endif
