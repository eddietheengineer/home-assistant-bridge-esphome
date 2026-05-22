/*!
 * @file
 * @brief Mock for esp_heap_caps.h — provides heap_caps functions for tests.
 *
 * Provides mock implementations of ESP32 heap_caps functions so that
 * code depending on esp_heap_caps.h can be unit tested on non-ESP32
 * platforms.
 *
 * The actual mock implementations (which use the CppUTest framework)
 * are in test/src/esp_heap_caps_double.cpp. This header only declares
 * the functions and the MALLOC_CAP_INTERNAL constant.
 */

#ifndef esp_heap_caps_h
#define esp_heap_caps_h

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/* MALLOC_CAP_INTERNAL constant used by heap_monitor.cpp */
#define MALLOC_CAP_INTERNAL 0x00

/*!
 * Mock of esp_get_free_heap_size().
 * Returns the value set by the test via mock().expectOneCall(...).andReturnValue(...).
 */
size_t esp_get_free_heap_size(void);

/*!
 * Mock of esp_get_minimum_free_heap_size().
 * Returns the value set by the test via mock().expectOneCall(...).andReturnValue(...).
 */
size_t esp_get_minimum_free_heap_size(void);

/*!
 * Mock of heap_caps_get_total_size().
 * Returns the value set by the test via mock().expectOneCall(...).andReturnValue(...).
 */
size_t heap_caps_get_total_size(int caps);

/*!
 * Mock of heap_caps_get_free_size().
 * Returns the value set by the test via mock().expectOneCall(...).andReturnValue(...).
 */
size_t heap_caps_get_free_size(int caps);

#ifdef __cplusplus
}
#endif

#endif  // esp_heap_caps_h
