// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Own the periodic publishing of diagnostic sensor values (ERD publish
//       rate, MQTT publish rate, ERD cache stats, MQTT disconnect stats),
//       extracted from GeappliancesBridge::loop() to reduce god-class size
//       and simplify the main loop.
//
// Responsibilities:
//   - Publish ERD publish rate and MQTT publish rate sensors every ~60s
//   - Publish ERD cache entries and update rate sensors every ~60s
//   - Publish MQTT disconnect count and duration sensors every ~60s
//   - Own timing state (last publish timestamps) for each sensor group
//
// NOT responsible for:
//   - Sensor object lifecycle (owned by ESPHome codegen / bridge)
//   - ERD cache or MQTT publisher lifecycle (owned by bridge)
//
// Dependencies:
//   - sensor::Sensor pointers (passed by reference via init())
//   - erd_cache_t (passed by reference via init())
//   - erd_cache_mqtt_publisher_t (passed by reference via init())
// =============================================================================

#pragma once

#include <cstdint>

extern "C" {
#include "erd_cache.h"
#include "erd_cache_mqtt_publisher.h"
}

#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace geappliances_bridge {

class DiagnosticSensorPublisher {
 public:
  /// Initialize with sensor pointers and cache references.
  /// @param erd_publish_rate_sensor  ERD updates per interval sensor (nullable).
  /// @param mqtt_publish_rate_sensor  MQTT publishes per interval sensor (nullable).
  /// @param erd_cache_entries_sensor  Current ERD cache entry count sensor (nullable).
  /// @param erd_cache_updates_sensor  ERD required update rate sensor (nullable).
  /// @param mqtt_disconnect_count_sensor  Total disconnect count sensor (nullable).
  /// @param mqtt_disconnect_duration_sensor  Last disconnect duration sensor (nullable).
  /// @param erd_cache  Shared ERD cache for reading stats.
  /// @param erd_cache_publisher  MQTT publisher for reading publish/disconnect stats.
  void init(
      sensor::Sensor* erd_publish_rate_sensor,
      sensor::Sensor* mqtt_publish_rate_sensor,
      sensor::Sensor* erd_cache_entries_sensor,
      sensor::Sensor* erd_cache_updates_sensor,
      sensor::Sensor* mqtt_disconnect_count_sensor,
      sensor::Sensor* mqtt_disconnect_duration_sensor,
      erd_cache_t& erd_cache,
      erd_cache_mqtt_publisher_t& erd_cache_publisher);

  /// Publish diagnostic sensor values at their configured intervals.
  /// Call from GeappliancesBridge::loop().
  void loop();

  /// Update a sensor pointer (called from bridge's set_*_sensor() methods).
  void set_erd_publish_rate_sensor(sensor::Sensor* sensor) { erd_publish_rate_sensor_ = sensor; }
  void set_mqtt_publish_rate_sensor(sensor::Sensor* sensor) { mqtt_publish_rate_sensor_ = sensor; }
  void set_erd_cache_entries_sensor(sensor::Sensor* sensor) { erd_cache_entries_sensor_ = sensor; }
  void set_erd_cache_updates_sensor(sensor::Sensor* sensor) { erd_cache_updates_sensor_ = sensor; }
  void set_mqtt_disconnect_count_sensor(sensor::Sensor* sensor) { mqtt_disconnect_count_sensor_ = sensor; }
  void set_mqtt_disconnect_duration_sensor(sensor::Sensor* sensor) { mqtt_disconnect_duration_sensor_ = sensor; }

 private:
  // Publish interval in milliseconds (~60 seconds)
  static constexpr uint32_t ERD_PUBLISH_RATE_INTERVAL_MS = 60000;

  // Timing state for each sensor group
  uint32_t last_erd_publish_rate_publish_{0};
  uint32_t last_erd_cache_stats_publish_{0};
  uint32_t last_mqtt_disconnect_stats_publish_{0};

  // Sensor pointers (nullable — set by ESPHome codegen)
  sensor::Sensor* erd_publish_rate_sensor_{nullptr};
  sensor::Sensor* mqtt_publish_rate_sensor_{nullptr};
  sensor::Sensor* erd_cache_entries_sensor_{nullptr};
  sensor::Sensor* erd_cache_updates_sensor_{nullptr};
  sensor::Sensor* mqtt_disconnect_count_sensor_{nullptr};
  sensor::Sensor* mqtt_disconnect_duration_sensor_{nullptr};

  // References to bridge state
  erd_cache_t* erd_cache_{nullptr};
  erd_cache_mqtt_publisher_t* erd_cache_publisher_{nullptr};
};

}  // namespace geappliances_bridge
}  // namespace esphome