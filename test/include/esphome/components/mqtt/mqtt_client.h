/*!
 * @file
 * @brief Stub for esphome/components/mqtt/mqtt_client.h — provides MQTTClientComponent for tests.
 */

#ifndef esphome_components_mqtt_mqtt_client_h
#define esphome_components_mqtt_mqtt_client_h

#include <string>
#include <functional>
#include <cstdint>

namespace esphome {
namespace mqtt {

class MQTTClientComponent {
 public:
  virtual ~MQTTClientComponent() {}
  virtual bool is_connected() = 0;
  virtual void publish(const std::string& topic, const std::string& payload,
                       uint8_t qos, bool retain) = 0;
  virtual void subscribe(const std::string& topic,
                         std::function<void(const std::string&, const std::string&)> callback,
                         uint8_t qos) = 0;

  static MQTTClientComponent* global_mqtt_client;
};

/* Namespace-level global for code that accesses esphome::mqtt::global_mqtt_client directly */
inline MQTTClientComponent* global_mqtt_client = nullptr;

}  // namespace mqtt
}  // namespace esphome

#endif  // esphome_components_mqtt_mqtt_client_h
