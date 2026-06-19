/*!
 * @file
 * @brief Stub for esphome/core/hal.h — provides millis() mock for tests.
 *
 * This file is placed in the test include path so that source files
 * including "esphome/core/hal.h" get the test double instead of the
 * real ESPHome header.
 */

#ifndef esphome_core_hal_h
#define esphome_core_hal_h

#include "double/esphome_hal_double.hpp"

/* In ESPHome, millis() is available in global namespace.
 * esphome::millis() is provided by esphome_hal_double.hpp. */
inline uint32_t millis() {
  return esphome_hal_double_get_millis();
}

/* Stub delay(0) for tests — yields to main loop in real ESPHome. */
inline void delay(uint32_t ms) {
  (void)ms;
}

namespace esphome {
using ::delay;
}  // namespace esphome

#endif  // esphome_core_hal_h
