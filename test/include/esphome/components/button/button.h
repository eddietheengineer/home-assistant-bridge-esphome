/*!
 * @file
 * @brief Stub for esphome/components/button/button.h — provides Button class for tests.
 */

#ifndef esphome_components_button_button_h
#define esphome_components_button_button_h

#include <functional>
#include <vector>

namespace esphome {
namespace button {

class Button {
 public:
  virtual ~Button() {}
  virtual void press_action() = 0;

  void add_on_press_callback(std::function<void()> callback) {
    callbacks_.push_back(std::move(callback));
  }

  // Helper for tests to simulate a button press
  void press() {
    for (auto& cb : callbacks_) {
      cb();
    }
  }

 private:
  std::vector<std::function<void()>> callbacks_;
};

}  // namespace button
}  // namespace esphome

#endif  // esphome_components_button_button_h
