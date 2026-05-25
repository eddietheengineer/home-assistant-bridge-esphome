/*!
 * @file
 * @brief Text sensor that exposes the auto-generated device ID from the bridge.
 *
 * This sensor reads the generated_device_id_ (e.g., "Dishwasher_ZL4200ABC_12345678")
 * from the GeappliancesBridge component.  Unlike final_device_id_, this always
 * reflects what the appliance actually reported — even if the user has overridden
 * the device_id in YAML configuration.
 */

#ifndef GEAPPLIANCES_BRIDGE_DEVICE_ID_SENSOR_H
#define GEAPPLIANCES_BRIDGE_DEVICE_ID_SENSOR_H

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include <string>

namespace esphome {
namespace geappliances_bridge {

// Forward declaration
class GeappliancesBridge;

class GeappliancesBridgeDeviceIdSensor : public text_sensor::TextSensor,
                                         public PollingComponent {
 public:
  void set_parent(GeappliancesBridge *parent) { this->parent_ = parent; }

  void update() override;
  void dump_config() override;

 protected:
  GeappliancesBridge *parent_{nullptr};
};

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // GEAPPLIANCES_BRIDGE_DEVICE_ID_SENSOR_H
