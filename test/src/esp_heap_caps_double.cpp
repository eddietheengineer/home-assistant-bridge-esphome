/*!
 * @file
 * @brief Mock implementations of ESP32 heap_caps functions.
 *
 * Each function records an actual call with the CppUTest mock framework
 * and returns the value set by the test via andReturnValue().
 *
 * On 64-bit platforms size_t == unsigned long, so we use
 * returnUnsignedLongIntValueOrDefault().
 */

#include "esp_heap_caps.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {

size_t esp_get_free_heap_size(void)
{
  return mock()
    .actualCall("esp_get_free_heap_size")
    .returnUnsignedLongIntValueOrDefault(0);
}

size_t esp_get_minimum_free_heap_size(void)
{
  return mock()
    .actualCall("esp_get_minimum_free_heap_size")
    .returnUnsignedLongIntValueOrDefault(0);
}

size_t heap_caps_get_total_size(int caps)
{
  (void)caps;
  return mock()
    .actualCall("heap_caps_get_total_size")
    .returnUnsignedLongIntValueOrDefault(0);
}

size_t heap_caps_get_free_size(int caps)
{
  (void)caps;
  return mock()
    .actualCall("heap_caps_get_free_size")
    .returnUnsignedLongIntValueOrDefault(0);
}

}
