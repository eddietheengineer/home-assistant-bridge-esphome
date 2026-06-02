// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Provide standalone formatting helpers for ERD MQTT topics and payloads.
//
// Responsibilities:
//   - build_erd_topic() — construct "geappliances/{device_id}/erd/0xXXXX/value"
//   - format_erd_payload() — hex-encode all ERD values (no string/ASCII branch)
//
// NOT responsible for:
//   - MQTT publishing (handled by MqttSideStateMachine / adapter)
//   - ERD validation (handled by ErdRegistry)
//
// Dependencies: none (standalone, no ErdRegistry dependency)
// =============================================================================

#ifndef GEAPPLIANCES_BRIDGE_ERD_PAYLOAD_FORMATTER_H
#define GEAPPLIANCES_BRIDGE_ERD_PAYLOAD_FORMATTER_H

#include <cstdint>
#include <string>

#include "tiny_erd.h"

namespace esphome {
namespace geappliances_bridge {

// Returns the MQTT value topic for an ERD.
// e.g. "geappliances/{device_id}/erd/0x1234/value"
std::string build_erd_topic(const std::string& device_id, tiny_erd_t erd);

// Returns the MQTT payload string for an ERD value.
// All ERD values are hex-encoded (no ASCII/string branch).
std::string format_erd_payload(const uint8_t* value, uint8_t size);

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // GEAPPLIANCES_BRIDGE_ERD_PAYLOAD_FORMATTER_H
