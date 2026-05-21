/*!
 * @file
 * @brief Heap monitoring manager for the GEA bridge.
 *
 * Handles periodic heap sensor updates, extracted from the GeappliancesBridge
 * god class to reduce its member count and improve separation of concerns.
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#ifdef USE_ESP32
#include "esp_heap_caps.h"
#endif

namespace esphome {
namespace geappliances_bridge {

/*!
 * Manages periodic heap monitoring sensor updates.
 *
 * Updates free heap, minimum free heap, and heap fragmentation sensors
 * at a configurable interval (default 60 seconds).
 */
class HeapMonitor {
 public:
  static constexpr uint32_t DEFAULT_UPDATE_INTERVAL_MS = 60000;  // 60 seconds

  /*!
   * Initialize the heap monitor with sensor references.
   * Null sensors are acceptable - they will simply be skipped during updates.
   */
  void init(sensor::Sensor* free_heap, sensor::Sensor* min_free_heap,
            sensor::Sensor* fragmentation,
            uint32_t update_interval_ms = DEFAULT_UPDATE_INTERVAL_MS);

  /*!
   * Called from the main loop(). Performs a heap sensor update if enough
   * time has elapsed since the last update.
   */
  void run();

 private:
  sensor::Sensor* free_heap_sensor_{nullptr};
  sensor::Sensor* min_free_heap_sensor_{nullptr};
  sensor::Sensor* heap_fragmentation_sensor_{nullptr};
  uint32_t last_update_{0};
  uint32_t update_interval_ms_{DEFAULT_UPDATE_INTERVAL_MS};
};

}  // namespace geappliances_bridge
}  // namespace esphome
