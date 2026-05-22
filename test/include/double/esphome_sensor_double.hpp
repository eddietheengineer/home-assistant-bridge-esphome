/*!
 * @file
 * @brief ESPHome sensor::Sensor test double.
 *
 * Mocks the ESPHome sensor::Sensor class so that HeapMonitor and other
 * sensor-dependent modules can be unit tested without ESPHome headers.
 *
 * Captures publish_state() calls for verification via CppUTest mock framework.
 */

#ifndef esphome_sensor_double_hpp
#define esphome_sensor_double_hpp

#include <string>
#include "CppUTestExt/MockSupport.h"

/*!
 * Minimal stand-in for esphome::sensor::Sensor.
 *
 * Each method delegates to the CppUTest mock framework so callers can
 * set expectations with mock().expectOneCall(...).
 */
class SensorDouble {
 public:
  explicit SensorDouble(const std::string& name = "test_sensor")
    : name_(name) {}

  void publish_state(float value)
  {
    mock()
      .actualCall("sensor_publish_state")
      .withStringParameter("name", name_.c_str())
      .withFloatParameter("value", value);
  }

  void publish_state(const std::string& value)
  {
    mock()
      .actualCall("sensor_publish_state")
      .withStringParameter("name", name_.c_str())
      .withStringParameter("value", value.c_str());
  }

  const std::string& name() const { return name_; }

 private:
  std::string name_;
};

#endif  // esphome_sensor_double_hpp
