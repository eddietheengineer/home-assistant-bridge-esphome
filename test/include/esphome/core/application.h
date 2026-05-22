/*!
 * @file
 * @brief Stub for esphome/core/application.h — provides app() for tests.
 */

#ifndef esphome_core_application_h
#define esphome_core_application_h

namespace esphome {

class Application {
 public:
  static Application& get_app();
};

inline Application& app() { return Application::get_app(); }

}  // namespace esphome

#endif  // esphome_core_application_h
