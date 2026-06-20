/*!
 * @file
 * @brief Stub for esphome/components/button/button.h — provides Button class for tests.
 */

#ifndef esphome_components_button_button_h
#define esphome_components_button_button_h

#include <functional>

namespace esphome {
namespace button {

class Button {
 public:
  virtual ~Button() {}
  using on_press_callback_t = std::function<void()>;
  virtual void add_on_press_callback(on_press_callback_t callback) {
    this->on_press_ = std::move(callback);
  }
  virtual void press() {
    if (this->on_press_) this->on_press_();
  }
 protected:
  on_press_callback_t on_press_;
};

}  // namespace button
}  // namespace esphome

#endif  // esphome_components_button_button_h
