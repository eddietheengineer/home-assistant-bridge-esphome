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
  (void)protocol;  /* Used only in ESP_LOGI below. */
  ESP_LOGI(TAG, "Starting device ID generation from host address 0x%02X via %s",
           this->host_address_, protocol);
  this->device_id_state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;
}

// ---------------------------------------------------------------------------
// Finalize device ID: sync fields from manager, apply fallback, notify sensors
// ---------------------------------------------------------------------------

void GeappliancesBridge::finalize_device_id_(bool sync_all)
{
  this->final_device_id_     = this->device_identity_manager_.get_device_id();
  this->generated_device_id_ = this->device_identity_manager_.get_generated_device_id();

  if (sync_all) {
    this->appliance_type_ = this->device_identity_manager_.get_appliance_type();
    this->model_number_   = this->device_identity_manager_.get_model_number();
    this->serial_number_  = this->device_identity_manager_.get_serial_number();
  }

  if (this->final_device_id_.empty()) {
    this->final_device_id_     = "Unknown_Unknown_Unknown";
    this->generated_device_id_ = this->final_device_id_;
  }

  this->notify_device_id_sensors_();
}

}  // namespace geappliances_bridge
}  // namespace esphome
