/*!
 * @file
 * @brief Stub for esphome/core/application.h — provides app() for tests.
 */

#ifndef esphome_core_application_h
#define esphome_core_application_h

namespace esphome {

class Application {
 public:
  void reboot() {}
  void safe_reboot() {}
};

inline Application App;

}  // namespace esphome

#endif  // esphome_core_application_h
