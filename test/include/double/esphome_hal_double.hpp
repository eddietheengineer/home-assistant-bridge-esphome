/*!
 * @file
 * @brief ESPHome HAL test double.
 *
 * Provides mock implementations of esphome::millis() and ESP_LOG* macros
 * so that code depending on ESPHome HAL can be unit tested.
 *
 * Usage:
 *   #include "double/esphome_hal_double.hpp"
 *
 *   // Set the simulated time.
 *   esphome_hal_double_set_millis(1000);
 *
 *   // Expect a log call.
 *   mock().expectOneCall("ESP_LOGI").withStringParameter("tag", "my_tag");
 */

#ifndef esphome_hal_double_hpp
#define esphome_hal_double_hpp

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Set the simulated millis() value.
 */
void esphome_hal_double_set_millis(uint32_t ms);

/*!
 * Get the current simulated millis() value.
 */
uint32_t esphome_hal_double_get_millis(void);

#ifdef __cplusplus
}
#endif

/* Override the esphome::millis() function. */
namespace esphome {
inline uint32_t millis() {
  return esphome_hal_double_get_millis();
}
}

/* Override ESP_LOG* macros to be no-ops in production code.
 * Test files that want to mock logs should include CppUTest first
 * and then use the mock versions. */
#define ESP_LOG_TAG "test"
#define ESP_LOGE(tag, ...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)0)
#define ESP_LOGI(tag, ...) ((void)0)
#define ESP_LOGD(tag, ...) ((void)0)
#define ESP_LOGV(tag, ...) ((void)0)
#define ESP_LOGCONFIG(tag, ...) ((void)0)

#endif  // esphome_hal_double_hpp
