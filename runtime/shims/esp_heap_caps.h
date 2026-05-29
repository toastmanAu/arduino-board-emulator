// esp_heap_caps.h — ESP-IDF's "heap with capabilities" allocator.
// On real ESP32 these route allocations to internal SRAM, PSRAM, DMA-capable
// memory, etc. On the host, all caps map to plain malloc/realloc/free —
// nothing differentiates the heaps in a simulator. The cap flag is ignored.
#pragma once
#include <stddef.h>
#include <stdlib.h>

// MALLOC_CAP_* — capability bitmask. Sketches OR these together when allocating.
// Values match ESP-IDF for source compatibility, but the simulator ignores them.
#define MALLOC_CAP_EXEC             (1 << 0)
#define MALLOC_CAP_32BIT            (1 << 1)
#define MALLOC_CAP_8BIT             (1 << 2)
#define MALLOC_CAP_DMA              (1 << 3)
#define MALLOC_CAP_PID2             (1 << 4)
#define MALLOC_CAP_PID3             (1 << 5)
#define MALLOC_CAP_PID4             (1 << 6)
#define MALLOC_CAP_PID5             (1 << 7)
#define MALLOC_CAP_PID6             (1 << 8)
#define MALLOC_CAP_PID7             (1 << 9)
#define MALLOC_CAP_SPIRAM           (1 << 10)
#define MALLOC_CAP_INTERNAL         (1 << 11)
#define MALLOC_CAP_DEFAULT          (1 << 12)
#define MALLOC_CAP_IRAM_8BIT        (1 << 13)
#define MALLOC_CAP_RETENTION        (1 << 14)
#define MALLOC_CAP_RTCRAM           (1 << 15)
#define MALLOC_CAP_INVALID          (1 << 31)

#ifdef __cplusplus
extern "C" {
#endif

inline void* heap_caps_malloc(size_t size, uint32_t /*caps*/)              { return malloc(size); }
inline void* heap_caps_calloc(size_t n, size_t size, uint32_t /*caps*/)    { return calloc(n, size); }
inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t /*caps*/)  { return realloc(ptr, size); }
inline void  heap_caps_free(void* ptr)                                     { free(ptr); }

// Stubs for the "introspection" calls — sketches sometimes check these.
inline size_t heap_caps_get_free_size(uint32_t /*caps*/)               { return 4 * 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(uint32_t /*caps*/)      { return 4 * 1024 * 1024; }
inline size_t heap_caps_get_total_size(uint32_t /*caps*/)              { return 8 * 1024 * 1024; }

#ifdef __cplusplus
}
#endif
