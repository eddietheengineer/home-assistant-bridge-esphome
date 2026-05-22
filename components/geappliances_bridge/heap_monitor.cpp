/*!
 * @file
 * @brief Heap monitoring manager implementation.
 */

#include "heap_monitor.h"
#include "esphome/core/hal.h"

#ifdef USE_ESP32
#include "esp_heap_caps.h"
#endif

namespace esphome {
namespace geappliances_bridge {

void HeapMonitor::init(sensor::Sensor* free_heap, sensor::Sensor* min_free_heap,
                       sensor::Sensor* fragmentation,
                       uint32_t update_interval_ms)
{
  this->free_heap_sensor_     = free_heap;
  this->min_free_heap_sensor_ = min_free_heap;
  this->heap_fragmentation_sensor_ = fragmentation;
  this->update_interval_ms_   = update_interval_ms;
  this->last_update_          = 0;
}

void HeapMonitor::run()
{
  uint32_t now = millis();
  if (now - this->last_update_ < this->update_interval_ms_) {
    return;
  }
  this->last_update_ = now;

#ifdef USE_ESP32
  if (this->free_heap_sensor_ != nullptr) {
    this->free_heap_sensor_->publish_state(
        static_cast<float>(esp_get_free_heap_size()));
  }
  if (this->min_free_heap_sensor_ != nullptr) {
    this->min_free_heap_sensor_->publish_state(
        static_cast<float>(esp_get_minimum_free_heap_size()));
  }
  if (this->heap_fragmentation_sensor_ != nullptr) {
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_heap  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    float usage_percent = (total_heap > 0)
        ? (100.0f - (100.0f * free_heap / total_heap)) : 0.0f;
    this->heap_fragmentation_sensor_->publish_state(usage_percent);
  }
#endif
}

}  // namespace geappliances_bridge
}  // namespace esphome
