#include "global_state_registry.h"

namespace esphome {
namespace geappliances_bridge {

GlobalStateRegistry::GlobalStateRegistry()
    : appliance_address_(0),
      gea_protocol_type_(0),
      bridge_state_(BridgeState::STARTING),
      device_id_set_(false)
{
  tiny_event_init(&device_id_ready_);
  tiny_event_init(&appliance_address_changed_);
  tiny_event_init(&gea_protocol_type_changed_);
  tiny_event_init(&bridge_state_changed_);
}

const std::string& GlobalStateRegistry::get_device_id() const {
  return device_id_;
}

uint8_t GlobalStateRegistry::get_appliance_address() const {
  return appliance_address_;
}

uint8_t GlobalStateRegistry::get_gea_protocol_type() const {
  return gea_protocol_type_;
}

BridgeState GlobalStateRegistry::get_bridge_state() const {
  return bridge_state_;
}

void GlobalStateRegistry::set_device_id(const std::string& id) {
  if (device_id_set_) {
    return;  // write-once
  }
  device_id_ = id;
  device_id_set_ = true;
  tiny_event_publish(&device_id_ready_, nullptr);
}

void GlobalStateRegistry::set_appliance_address(uint8_t addr) {
  if (appliance_address_ == addr) {
    return;
  }
  appliance_address_ = addr;
  tiny_event_publish(&appliance_address_changed_, nullptr);
}

void GlobalStateRegistry::set_gea_protocol_type(uint8_t type) {
  if (gea_protocol_type_ == type) {
    return;
  }
  gea_protocol_type_ = type;
  tiny_event_publish(&gea_protocol_type_changed_, nullptr);
}

void GlobalStateRegistry::set_bridge_state(BridgeState state) {
  if (bridge_state_ == state) {
    return;
  }
  bridge_state_ = state;
  tiny_event_publish(&bridge_state_changed_, nullptr);
}

i_tiny_event_t* GlobalStateRegistry::on_device_id_ready() {
  return &device_id_ready_.interface;
}

i_tiny_event_t* GlobalStateRegistry::on_appliance_address_changed() {
  return &appliance_address_changed_.interface;
}

i_tiny_event_t* GlobalStateRegistry::on_gea_protocol_type_changed() {
  return &gea_protocol_type_changed_.interface;
}

i_tiny_event_t* GlobalStateRegistry::on_bridge_state_changed() {
  return &bridge_state_changed_.interface;
}

}  // namespace geappliances_bridge
}  // namespace esphome
