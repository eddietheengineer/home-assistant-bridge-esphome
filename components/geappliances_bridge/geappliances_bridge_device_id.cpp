/*!
 * @file
 * @brief Device ID generation from appliance identity ERDs.
 *
 * Reads ERDs 0x0008 (appliance type), 0x0001 (model number), and 0x0002
 * (serial number) after feature-bit reading completes and assembles a unique,
 * MQTT-topic-safe device identifier string.  If a device_id is already
 * configured in YAML, the read sequence is skipped and that value is used
 * directly.
 *
 * Driven by the startup HSM (startup_state_device_id) which uses the
 * DeviceIdentityManager internally.
 */

#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include <cstring>
#include <inttypes.h>

// Forward declaration (generated from appliance API data)
std::string appliance_type_to_string(uint8_t appliance_type);

namespace esphome {
namespace geappliances_bridge {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif
static const char* const TAG = "geappliances_bridge";
#ifdef __clang__
#pragma clang diagnostic pop
#endif

}  // namespace geappliances_bridge
}  // namespace esphome
