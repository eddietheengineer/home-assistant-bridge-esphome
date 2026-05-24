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

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "geappliances_bridge";

// ---------------------------------------------------------------------------
// Startup: kick off feature-bit reading sequence
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_feature_bit_reading_()
{
  // Guard: don't re-initialize if already running or done.
  if (this->feature_bit_manager_.is_complete() || this->feature_bit_manager_.is_failed() ||
      this->feature_bit_manager_.is_parse_pending()) {
    return;
  }
  ESP_LOGI(TAG, "Reading device info ERDs for MQTT publish, then appliance API feature bits...");
  this->feature_bit_manager_.init(
      this->autodiscovery_manager_.get_active_erd_client(),
      this->autodiscovery_manager_.get_host_address(),
      &this->mqtt_client_adapter_.interface,
      this->mqtt_client_adapter_initialized_);
}

}  // namespace geappliances_bridge
}  // namespace esphome
