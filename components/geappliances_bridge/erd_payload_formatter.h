// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Provide standalone formatting helpers for ERD MQTT topics and payloads.
//
// Responsibilities:
//   - build_erd_topic() — construct "geappliances/{device_id}/erd/0xXXXX/value"
//   - format_erd_payload() — hex for binary ERDs, ASCII for string-typed ERDs
//
// NOT responsible for:
//   - MQTT publishing (handled by MqttSideStateMachine / adapter)
//   - ERD validation (handled by ErdRegistry)
//
// Dependencies:
//   - ErdRegistry (for is_string_type lookup)
// =============================================================================

#ifndef GEAPPLIANCES_BRIDGE_ERD_PAYLOAD_FORMATTER_H
#define GEAPPLIANCES_BRIDGE_ERD_PAYLOAD_FORMATTER_H

#include <cstdint>
#include <cctype>
#include <string>

#include "erd_registry.h"
#include "tiny_erd.h"

namespace esphome {
namespace geappliances_bridge {

// Returns the MQTT value topic for an ERD.
// e.g. "geappliances/{device_id}/erd/0x1234/value"
std::string build_erd_topic(const std::string& device_id, tiny_erd_t erd);

// Returns the MQTT payload string for an ERD value.
// Uses ErdRegistry::is_string_type() to choose hex vs. ASCII encoding.
// registry may be nullptr, in which case hex encoding is always used.
std::string format_erd_payload(tiny_erd_t erd,
                               const uint8_t* value,
                               uint8_t size,
                               ErdRegistry* registry);

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // GEAPPLIANCES_BRIDGE_ERD_PAYLOAD_FORMATTER_H
