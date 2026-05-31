/*!
 * @file
 * @brief Internal ERD IDs for cross-module coordination within the bridge.
 *
 * These IDs are NOT real appliance ERDs. They live exclusively inside the
 * ErdDataBus and are never forwarded to the physical GEA bus or published
 * to MQTT. Modules write and subscribe to these ERDs to communicate state
 * changes without calling each other directly.
 *
 * Range: 0xF000–0xFFFF (above the appliance ERD space of 0x0000–0xEFFF)
 */

#pragma once

#include <cstdint>

extern "C" {
#include "tiny_erd.h"
}

namespace esphome {
namespace geappliances_bridge {

enum InternalErdId : uint16_t {
  // MQTT connection FSM state (payload: MqttConnectionState cast to uint8_t)
  ERD_INTERNAL_MQTT_STATE = 0xF000,
};

}  // namespace geappliances_bridge
}  // namespace esphome
