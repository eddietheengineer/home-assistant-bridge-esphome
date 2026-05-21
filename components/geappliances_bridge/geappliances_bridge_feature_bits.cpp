/*!
 * @file
 * @brief Appliance API feature-bit reading and parsing.
 *
 * Reads ERDs 0x0092–0x0097 and 0x0109–0x010D from the appliance after
 * autodiscovery completes.  These ERDs report which appliance-API features
 * are supported and are used to build the filtered ERD list for polling mode
 * and to gate HA discovery to only supported entities.
 *
 * Driven by the startup HSM (startup_state_mqtt_client_init and
 * startup_state_feature_bits) which uses the FeatureBitManager internally.
 */

#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "appliance_api_feature_lists.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG = "geappliances_bridge";

// ---------------------------------------------------------------------------
// Startup: kick off feature-bit reading sequence
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_feature_bit_reading_()
{
  if (this->feature_bit_state_ != FEATURE_BIT_STATE_IDLE) {
    return;
  }
  ESP_LOGI(TAG, "Reading device info ERDs for MQTT publish, then appliance API feature bits...");
  this->feature_bit_manager_.init(this->active_erd_client_, this->host_address_,
                                   &this->mqtt_client_adapter_.interface,
                                   this->mqtt_client_adapter_initialized_);
  this->feature_bit_state_ = FEATURE_BIT_STATE_READING_0008;
}

// ---------------------------------------------------------------------------
// Sync legacy members from FeatureBitManager (called after routing callbacks)
// ---------------------------------------------------------------------------

void GeappliancesBridge::sync_feature_bit_legacy_members_()
{
  // Sync state from manager.
  this->feature_bit_state_ = this->feature_bit_manager_.get_state();

  // Sync feature bit ERD data from manager.
  const auto& erd_data = this->feature_bit_manager_.get_erd_data();
  this->feature_bit_erd_0092_size_ = erd_data.erd_0092_size;
  if (erd_data.erd_0092_size > 0)
    memcpy(this->feature_bit_erd_0092_, erd_data.erd_0092, erd_data.erd_0092_size);
  this->feature_bit_erd_0093_size_ = erd_data.erd_0093_size;
  if (erd_data.erd_0093_size > 0)
    memcpy(this->feature_bit_erd_0093_, erd_data.erd_0093, erd_data.erd_0093_size);
  this->feature_bit_erd_0094_size_ = erd_data.erd_0094_size;
  if (erd_data.erd_0094_size > 0)
    memcpy(this->feature_bit_erd_0094_, erd_data.erd_0094, erd_data.erd_0094_size);
  this->feature_bit_erd_0095_size_ = erd_data.erd_0095_size;
  if (erd_data.erd_0095_size > 0)
    memcpy(this->feature_bit_erd_0095_, erd_data.erd_0095, erd_data.erd_0095_size);
  this->feature_bit_erd_0096_size_ = erd_data.erd_0096_size;
  if (erd_data.erd_0096_size > 0)
    memcpy(this->feature_bit_erd_0096_, erd_data.erd_0096, erd_data.erd_0096_size);
  this->feature_bit_erd_0097_size_ = erd_data.erd_0097_size;
  if (erd_data.erd_0097_size > 0)
    memcpy(this->feature_bit_erd_0097_, erd_data.erd_0097, erd_data.erd_0097_size);
  this->feature_bit_erd_0109_size_ = erd_data.erd_0109_size;
  if (erd_data.erd_0109_size > 0)
    memcpy(this->feature_bit_erd_0109_, erd_data.erd_0109, erd_data.erd_0109_size);
  this->feature_bit_erd_010A_size_ = erd_data.erd_010A_size;
  if (erd_data.erd_010A_size > 0)
    memcpy(this->feature_bit_erd_010A_, erd_data.erd_010A, erd_data.erd_010A_size);
  this->feature_bit_erd_010B_size_ = erd_data.erd_010B_size;
  if (erd_data.erd_010B_size > 0)
    memcpy(this->feature_bit_erd_010B_, erd_data.erd_010B, erd_data.erd_010B_size);
  this->feature_bit_erd_010C_size_ = erd_data.erd_010C_size;
  if (erd_data.erd_010C_size > 0)
    memcpy(this->feature_bit_erd_010C_, erd_data.erd_010C, erd_data.erd_010C_size);
  this->feature_bit_erd_010D_size_ = erd_data.erd_010D_size;
  if (erd_data.erd_010D_size > 0)
    memcpy(this->feature_bit_erd_010D_, erd_data.erd_010D, erd_data.erd_010D_size);

  // If parsing completed, sync the valid ERD lists (only once, on transition).
  if (this->feature_bit_manager_.is_valid_list_ready() && !this->appliance_api_valid_list_ready_) {
    this->appliance_api_valid_erds_ = this->feature_bit_manager_.get_valid_erds();
    this->appliance_api_valid_erds_vec_ = this->feature_bit_manager_.get_valid_erds_vec();
    this->appliance_api_valid_list_ready_ = true;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
