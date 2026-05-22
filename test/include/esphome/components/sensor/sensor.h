/*!
 * @file
 * @brief Stub for esphome/components/sensor/sensor.h — provides Sensor class for tests.
 */

#ifndef esphome_components_sensor_sensor_h
#define esphome_components_sensor_sensor_h

#include <string>
#include <cstdint>

namespace esphome {
namespace sensor {

class Sensor {
 public:
  virtual ~Sensor() {}
  virtual void publish_state(float value) { (void)value; }
  virtual void publish_state(const std::string& value) { (void)value; }
};

}  // namespace sensor
}  // namespace esphome

#endif  // esphome_components_sensor_sensor_h
