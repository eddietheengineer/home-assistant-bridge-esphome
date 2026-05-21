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

static const char* const TAG = "geappliances_bridge";

// ---------------------------------------------------------------------------
// Startup: begin device ID generation (or skip if already configured)
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_device_id_generation_()
{
  // Initialize the DeviceIdentityManager with current context
  this->device_identity_manager_.init(
      this->configured_device_id_,
      this->active_erd_client_,
      this->host_address_);

  // If a device_id is pre-configured, the manager marks itself complete
  // immediately (see DeviceIdentityManager::init with non-empty configured_id).
  // Sync legacy members for backward compatibility with dump_config() etc.
  if (this->device_identity_manager_.is_complete()) {
    this->device_id_state_ = this->device_identity_manager_.get_state();
    this->final_device_id_ = this->device_identity_manager_.get_device_id();
    this->start_feature_bit_reading_();
    return;
  }

  const char* protocol = this->gea2_protocol_active_ ? "GEA2" : "GEA3";
  ESP_LOGI(TAG, "Starting device ID generation from host address 0x%02X via %s",
           this->host_address_, protocol);
  this->device_id_state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
}

// ---------------------------------------------------------------------------
// ERD queue helper
// ---------------------------------------------------------------------------

bool GeappliancesBridge::try_read_erd_with_retry_(tiny_erd_t erd, const char* erd_name)
{
  if (tiny_gea3_erd_client_read(this->active_erd_client_, &this->pending_request_id_,
                                 this->host_address_, erd)) {
    ESP_LOGD(TAG, "Reading %s ERD 0x%04X", erd_name, erd);
    this->device_id_state_  = DEVICE_ID_STATE_IDLE;  // wait for response
    this->queue_retry_count_ = 0;
    return true;
  }

  this->queue_retry_count_++;
  if (this->queue_retry_count_ >= MAX_QUEUE_RETRIES) {
    ESP_LOGE(TAG, "Failed to read %s after %u retries, giving up", erd_name, MAX_QUEUE_RETRIES);
    this->device_id_state_ = DEVICE_ID_STATE_FAILED;
    return false;
  }
  if (this->queue_retry_count_ % LOG_EVERY_N_RETRIES == 0) {
    ESP_LOGW(TAG, "Failed to queue %s read, retrying... (attempt %u)",
             erd_name, this->queue_retry_count_);
  }
  return false;
}

// ---------------------------------------------------------------------------
// String utilities
// ---------------------------------------------------------------------------

std::string GeappliancesBridge::bytes_to_string_(const uint8_t* data, size_t size)
{
  if (data == nullptr || size == 0) {
    return "";
  }
  std::string result;
  result.reserve(size);
  for (size_t i = 0; i < size; i++) {
    if (data[i] == 0x00) break;  // stop at null terminator
    result += static_cast<char>(data[i]);
  }
  return result;
}

std::string GeappliancesBridge::sanitize_for_mqtt_topic_(const std::string& input)
{
  std::string result;
  result.reserve(input.length());
  for (char c : input) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (c == '+' || c == '#' || c == '\0' || c == ' ' || c == '/' || c == '$' ||
        uc < 0x20 || uc > 0x7E) {
      result += '_';
    } else {
      result += c;
    }
  }
  return result;
}

}  // namespace geappliances_bridge
}  // namespace esphome
