#include "diagnostic_sensor_publisher.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace geappliances_bridge {

void DiagnosticSensorPublisher::init(
    sensor::Sensor* erd_publish_rate_sensor,
    sensor::Sensor* mqtt_publish_rate_sensor,
    sensor::Sensor* erd_cache_entries_sensor,
    sensor::Sensor* erd_cache_updates_sensor,
    sensor::Sensor* mqtt_disconnect_count_sensor,
    sensor::Sensor* mqtt_disconnect_duration_sensor,
    erd_cache_t& erd_cache,
    erd_cache_mqtt_publisher_t& erd_cache_publisher)
{
  this->erd_publish_rate_sensor_ = erd_publish_rate_sensor;
  this->mqtt_publish_rate_sensor_ = mqtt_publish_rate_sensor;
  this->erd_cache_entries_sensor_ = erd_cache_entries_sensor;
  this->erd_cache_updates_sensor_ = erd_cache_updates_sensor;
  this->mqtt_disconnect_count_sensor_ = mqtt_disconnect_count_sensor;
  this->mqtt_disconnect_duration_sensor_ = mqtt_disconnect_duration_sensor;
  this->erd_cache_ = &erd_cache;
  this->erd_cache_publisher_ = &erd_cache_publisher;
}

void DiagnosticSensorPublisher::loop() {
  // Publish ERD publish rate and MQTT publish rate sensors every ~60 seconds.
  if (this->erd_publish_rate_sensor_ != nullptr || this->mqtt_publish_rate_sensor_ != nullptr) {
    uint32_t now = esphome::millis();
    if (now - this->last_erd_publish_rate_publish_ >= ERD_PUBLISH_RATE_INTERVAL_MS) {
      if (this->erd_publish_rate_sensor_ != nullptr) {
        uint32_t count = erd_cache_mqtt_publisher_get_publish_rate(this->erd_cache_publisher_);
        this->erd_publish_rate_sensor_->publish_state(static_cast<float>(count));
      }
      if (this->mqtt_publish_rate_sensor_ != nullptr) {
        uint32_t count = erd_cache_get_required_update_rate(this->erd_cache_);
        this->mqtt_publish_rate_sensor_->publish_state(static_cast<float>(count));
      }
      this->last_erd_publish_rate_publish_ = now;
    }
  }

  // Publish cache stats sensors every ~60 seconds.
  if (this->erd_cache_entries_sensor_ != nullptr || this->erd_cache_updates_sensor_ != nullptr) {
    uint32_t now = esphome::millis();
    if (now - this->last_erd_cache_stats_publish_ >= ERD_PUBLISH_RATE_INTERVAL_MS) {
      if (this->erd_cache_entries_sensor_ != nullptr) {
        this->erd_cache_entries_sensor_->publish_state(
          static_cast<float>(erd_cache_get_count(this->erd_cache_)));
      }
      if (this->erd_cache_updates_sensor_ != nullptr) {
        this->erd_cache_updates_sensor_->publish_state(
          static_cast<float>(erd_cache_get_update_rate(this->erd_cache_)));
      }
      this->last_erd_cache_stats_publish_ = now;
    }
  }

  // Publish MQTT disconnect sensors every ~60 seconds.
  if (this->mqtt_disconnect_count_sensor_ != nullptr || this->mqtt_disconnect_duration_sensor_ != nullptr) {
    uint32_t now = esphome::millis();
    if (now - this->last_mqtt_disconnect_stats_publish_ >= ERD_PUBLISH_RATE_INTERVAL_MS) {
      if (this->mqtt_disconnect_count_sensor_ != nullptr) {
        this->mqtt_disconnect_count_sensor_->publish_state(
          static_cast<float>(erd_cache_mqtt_publisher_get_disconnect_count(this->erd_cache_publisher_)));
      }
      if (this->mqtt_disconnect_duration_sensor_ != nullptr) {
        this->mqtt_disconnect_duration_sensor_->publish_state(
          static_cast<float>(erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(this->erd_cache_publisher_)));
      }
      this->last_mqtt_disconnect_stats_publish_ = now;
    }
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome