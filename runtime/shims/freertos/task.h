// freertos/task.h — task creation/scheduling stubs. ESP32 sketches typically
// reference this via #include <freertos/task.h>.
#pragma once
#include <cstdint>
#include "FreeRTOS.h"
#include <thread>
#include <chrono>

#ifdef __cplusplus
extern "C" {
#endif

using TaskFunction_t = void(*)(void*);

// Create a detached host thread to run the task function. Returns the handle
// via *pxCreatedTask (we cast a heap-allocated std::thread* to TaskHandle_t).
inline BaseType_t xTaskCreate(
    TaskFunction_t pvTaskCode,
    const char*    /*pcName*/,
    uint32_t       /*usStackDepth*/,
    void*          pvParameters,
    UBaseType_t    /*uxPriority*/,
    TaskHandle_t*  pxCreatedTask)
{
    auto* t = new std::thread([pvTaskCode, pvParameters]() {
        if (pvTaskCode) pvTaskCode(pvParameters);
    });
    t->detach();
    if (pxCreatedTask) *pxCreatedTask = static_cast<TaskHandle_t>(t);
    return pdPASS;
}

// ESP32's "pinned to core" variant — we ignore the core argument on host.
inline BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t pvTaskCode,
    const char*    name,
    uint32_t       stack,
    void*          pvParameters,
    UBaseType_t    prio,
    TaskHandle_t*  pxCreatedTask,
    BaseType_t     /*xCoreID*/)
{
    return xTaskCreate(pvTaskCode, name, stack, pvParameters, prio, pxCreatedTask);
}

inline void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

inline void vTaskDelete(TaskHandle_t /*handle*/) {
    // No-op: detached threads run to completion.
}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t /*handle*/) {
    return 4096;
}

#ifdef __cplusplus
}
#endif
