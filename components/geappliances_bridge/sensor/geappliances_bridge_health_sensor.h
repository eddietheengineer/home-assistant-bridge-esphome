/*!
 * @file
 * @brief Number sensor that exposes bridge health metrics.
 *
 * Three sensor types are supported:
 *   - pending_mqtt_updates: Number of ERD updates waiting in the MQTT adapter's
 *     pending queue (indicates MQTT backlog pressure).
 *   - median_inter_read_ms: Median time (ms) between consecutive polling reads
 *     in the most recent completed polling cycle.
 *   - polling_cycle_time_ms: Total time (ms) from the first read to the last
 *     read in the most recent completed polling cycle.
 */

#ifndef GEAPPLIANCES_BRIDGE_HEALTH_SENSOR_H
#define GEAPPLIANCES_BRIDGE_HEALTH_SENSOR_H

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include <string>

namespace esphome {
namespace geappliances_bridge {

// Forward declaration
class GeappliancesBridge;

enum BridgeHealthSensorType {
  SENSOR_TYPE_PENDING_UPDATES,
  SENSOR_TYPE_POLLING_CYCLE_TIME_MS,
  SENSOR_TYPE_POLLING_CYCLE_COUNT,
};

class GeappliancesBridgeHealthSensor : public sensor::Sensor,
                                       public PollingComponent {
 public:
  void set_parent(GeappliancesBridge *parent) { this->parent_ = parent; }
  void set_sensor_type(BridgeHealthSensorType type) { this->sensor_type_ = type; }

  void update() override;
  void dump_config() override;

 protected:
  GeappliancesBridge *parent_{nullptr};
  BridgeHealthSensorType sensor_type_{SENSOR_TYPE_PENDING_UPDATES};
};

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // GEAPPLIANCES_BRIDGE_HEALTH_SENSOR_H
