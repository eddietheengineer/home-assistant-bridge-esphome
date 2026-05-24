/*
 * @file
 * @brief Stub for esphome/components/text_sensor/text_sensor.h — provides TextSensor class for tests.
 */

#ifndef esphome_components_text_sensor_text_sensor_h
#define esphome_components_text_sensor_text_sensor_h

#include <string>

namespace esphome {
namespace text_sensor {

class TextSensor {
 public:
  virtual ~TextSensor() {}
  virtual void publish_state(const std::string& value) { (void)value; }
};

}  // namespace text_sensor
}  // namespace esphome

#endif  // esphome_components_text_sensor_text_sensor_h
