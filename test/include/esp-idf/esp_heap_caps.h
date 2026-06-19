/*!
 * @file
 * @brief ESP-IDF esp_heap_caps stubs for test/simulation builds.
 */

#ifndef ESP_HEAP_CAPS_STUB_H
#define ESP_HEAP_CAPS_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MALLOC_CAP_INTERNAL 0x01
#define MALLOC_CAP_DMA 0x04

static inline void* heap_caps_realloc(void* rmem, size_t newsize, uint32_t caps) {
    (void)caps; return realloc(rmem, newsize);
}
static inline void* heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps; return malloc(size);
}
static inline void heap_caps_free(void* mem) { free(mem); }

#ifdef __cplusplus
}
#endif

#endif /* ESP_HEAP_CAPS_STUB_H */
