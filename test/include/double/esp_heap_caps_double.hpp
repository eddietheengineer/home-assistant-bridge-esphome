/*!
 * @file
 * @brief Helper for setting up esp_heap_caps mock expectations.
 *
 * Convenience functions for setting up common heap_caps mock expectations
 * in HeapMonitor tests.
 *
 * Usage:
 *   #include "double/esp_heap_caps_double.hpp"
 *
 *   expect_heap_values(800000, 500000, 520000, 400000);
 */

#ifndef esp_heap_caps_double_hpp
#define esp_heap_caps_double_hpp

#include <cstddef>
#include "CppUTestExt/MockSupport.h"

/*!
 * Set up mock expectations for all four heap functions with expected values.
 *
 * @param free_heap Expected return value for esp_get_free_heap_size()
 * @param min_free_heap Expected return value for esp_get_minimum_free_heap_size()
 * @param total_internal Expected return value for heap_caps_get_total_size()
 * @param free_internal Expected return value for heap_caps_get_free_size()
 */
inline void expect_heap_values(size_t free_heap, size_t min_free_heap,
                                size_t total_internal, size_t free_internal)
{
  mock().expectOneCall("esp_get_free_heap_size")
    .andReturnValue((unsigned long int)free_heap);
  mock().expectOneCall("esp_get_minimum_free_heap_size")
    .andReturnValue((unsigned long int)min_free_heap);
  mock().expectOneCall("heap_caps_get_total_size")
    .andReturnValue((unsigned long int)total_internal);
  mock().expectOneCall("heap_caps_get_free_size")
    .andReturnValue((unsigned long int)free_internal);
}

#endif  // esp_heap_caps_double_hpp
