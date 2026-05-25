/*!
 * @file
 * @brief Implementation of the auto-generated device ID text sensor.
 */

#include "geappliances_bridge_device_id_sensor.h"
#include "../geappliances_bridge.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif
static const char *const TAG = "geappliances_bridge";
#ifdef __clang__
#pragma clang diagnostic pop
#endif

void GeappliancesBridgeDeviceIdSensor::update()
{
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Device ID sensor has no parent bridge");
    return;
  }

  // Use the public getter on the parent bridge
  std::string device_id = this->parent_->get_generated_device_id();
  
  if (device_id.empty()) {
    ESP_LOGW(TAG, "Device ID not yet available");
    return;
  }
  
  this->publish_state(device_id);
}

void GeappliancesBridgeDeviceIdSensor::dump_config()
{
  ESP_LOGCONFIG(TAG, "  Device ID Sensor:");
  LOG_TEXT_SENSOR("  ", "Device ID", this);
  if (this->parent_ != nullptr) {
    const std::string &device_id = this->parent_->get_generated_device_id();
    if (!device_id.empty()) {
      ESP_LOGCONFIG(TAG, "    Current Value: %s", device_id.c_str());
    } else {
      ESP_LOGCONFIG(TAG, "    Current Value: (not yet available)");
    }
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
