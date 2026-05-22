/*!
 * @file
 * @brief Mock Sensor that records publish_state() calls via CppUTest.
 *
 * Inherits from esphome::sensor::Sensor so it can be passed to code
 * expecting Sensor pointers, while recording all publish_state() calls
 * for verification.
 *
 * Usage:
 *   #include "double/sensor_mock.hpp"
 *
 *   sensor_mock_t free_heap_sensor("free_heap");
 *   heap_monitor.init(&free_heap_sensor, nullptr, nullptr, 1000);
 *
 *   // Set expectations:
 *   mock().expectOneCall("sensor_publish_state")
 *     .withStringParameter("name", "free_heap")
 *     .withFloatParameter("value", 800000.0f);
 */

#ifndef sensor_mock_hpp
#define sensor_mock_hpp

#include <string>
#include "esphome/components/sensor/sensor.h"
#include "CppUTestExt/MockSupport.h"

/*!
 * Mock Sensor that records publish_state(float) calls via CppUTest mock
 * framework.
 */
class sensor_mock_t : public esphome::sensor::Sensor {
 public:
  explicit sensor_mock_t(const char* name = "test_sensor")
    : name_(name) {}

  void publish_state(float value) override
  {
    mock()
      .actualCall("sensor_publish_state")
      .withStringParameter("name", name_.c_str())
      .withDoubleParameter("value", (double)value);
  }

  const char* name() const { return name_.c_str(); }

 private:
  std::string name_;
};

#endif  // sensor_mock_hpp
