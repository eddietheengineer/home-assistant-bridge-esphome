/*!
 * @file
 * @brief Concrete test implementation of MQTTClientComponent that stores
 *        subscription callbacks so they can be invoked manually in tests.
 */

#ifndef mqtt_test_double_hpp
#define mqtt_test_double_hpp

#include "esphome/components/mqtt/mqtt_client.h"
#include <string>

namespace esphome {
namespace mqtt {

class MqttTestDouble : public MQTTClientComponent {
 public:
  bool connected_{false};

  subscribe_callback_fn subscribe_callback_{nullptr};
  void* subscribe_context_{nullptr};
  on_connect_fn on_connect_callback_{nullptr};
  void* on_connect_context_{nullptr};
  on_disconnect_fn on_disconnect_callback_{nullptr};
  void* on_disconnect_context_{nullptr};

  bool is_connected() override { return connected_; }

  bool publish(const std::string& /*topic*/, const std::string& /*payload*/,
               uint8_t /*qos*/, bool /*retain*/) override { return true; }

  bool publish(const char* /*topic*/, const char* /*payload*/, size_t /*payload_length*/,
               uint8_t /*qos*/, bool /*retain*/) override { return true; }

  void subscribe(const std::string& /*topic*/,
                 subscribe_callback_fn callback,
                 void* context,
                 uint8_t /*qos*/) override {
    subscribe_callback_ = callback;
    subscribe_context_ = context;
  }

  void unsubscribe(const std::string& /*topic*/) override {
    subscribe_callback_ = nullptr;
    subscribe_context_ = nullptr;
  }

  void set_on_connect(on_connect_fn callback, void* context) override {
    on_connect_callback_ = callback;
    on_connect_context_ = context;
  }

  void set_on_disconnect(on_disconnect_fn callback, void* context) override {
    on_disconnect_callback_ = callback;
    on_disconnect_context_ = context;
  }

  void simulate_message(const std::string& topic, const std::string& payload) {
    if (subscribe_callback_) {
      subscribe_callback_(topic.c_str(), payload.c_str(), payload.size(), subscribe_context_);
    }
  }
};

}  // namespace mqtt
}  // namespace esphome

#endif
