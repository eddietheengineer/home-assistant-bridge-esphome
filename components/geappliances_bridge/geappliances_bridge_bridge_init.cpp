/*!
 * @file
 * @brief MQTT client adapter initialization and bridge mode management.
 *
 * initialize_mqtt_client_() runs once as soon as the device ID is ready
 * (Phase 4), before feature bit reading.  It binds the MQTT client adapter
 * to the device ID, sets up registered-ERD tracking, and configures the
 * string-ERD filter so the adapter is ready to publish ERD values immediately.
 *
 * initialize_mqtt_bridge_() runs once after feature bit reading and MQTT are
 * both ready (Phase 6).  It applies the valid-ERD filter (built from feature
 * bit results), selects the operating mode (poll / subscribe / auto), and
 * initializes the appropriate bridge HSMs.
 *
 * check_subscription_activity_() runs every loop() iteration in AUTO mode
 * and falls back to polling if no subscription publications arrive within
 * the timeout window.
 */

#include "geappliances_bridge.h"
#include "ha_discovery_config.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG = "geappliances_bridge";

// ---------------------------------------------------------------------------
// Phase 4: Initialize the MQTT client adapter (called once from loop())
// ---------------------------------------------------------------------------

void GeappliancesBridge::initialize_mqtt_client_()
{
  if (this->mqtt_client_adapter_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "Initializing MQTT client adapter with device ID: %s", this->final_device_id_.c_str());

  // For manual device_id configs where autodiscovery is skipped, set the
  // active ERD client now so run_feature_bit_reading_() can queue reads.
  if (this->active_erd_client_ == nullptr) {
    if (this->uart_ != nullptr) {
      this->active_erd_client_ = &this->erd_client_.interface;
    } else {
      this->active_erd_client_  = &this->gea2_erd_client_adapter_.interface;
      this->gea2_protocol_active_ = true;
    }
  }

  // Bind the adapter to the device ID.
  esphome_mqtt_client_adapter_init(&this->mqtt_client_adapter_, this->final_device_id_.c_str());

  // Wire up registered-ERD tracking so every ERD the device registers is
  // captured for use when filtering HA discovery to supported entities.
  this->ha_registered_erds_.clear();
  esphome_mqtt_client_adapter_set_registered_erds_out(
    &this->mqtt_client_adapter_, &this->ha_registered_erds_);

  // Build the string-type ERD set from the generated config and tell the
  // adapter so it publishes ASCII text instead of hex for those ERDs.
  this->ha_string_erds_set_.clear();
  for (uint16_t i = 0; i < ha_string_erd_count; i++) {
    this->ha_string_erds_set_.insert(ha_string_erd_ids[i]);
  }
  if (!this->ha_string_erds_set_.empty()) {
    esphome_mqtt_client_adapter_set_string_erds_filter(
      &this->mqtt_client_adapter_, &this->ha_string_erds_set_);
  }

  this->mqtt_client_adapter_initialized_ = true;
  ESP_LOGI(TAG, "MQTT client adapter initialized; feature bit ERDs will be published as they are read");
}

// ---------------------------------------------------------------------------
// Phase 4: Initialize the MQTT bridge (called once from loop())
// ---------------------------------------------------------------------------

void GeappliancesBridge::initialize_mqtt_bridge_()
{
  if (!this->mqtt_client_adapter_initialized_ || this->mqtt_bridge_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "Initializing MQTT bridge");

  // Apply the valid-ERD filter when appliance API parsing is enabled and
  // produced results. An empty set would silently suppress all publishes.
  if (this->appliance_api_parsing_ && this->appliance_api_valid_list_ready_ &&
      !this->appliance_api_valid_erds_.empty()) {
    esphome_mqtt_client_adapter_set_valid_erds_filter(
      &this->mqtt_client_adapter_, &this->appliance_api_valid_erds_);
    ESP_LOGI(TAG, "Appliance API parsing enabled: publishing filtered to %zu valid ERDs",
             this->appliance_api_valid_erds_.size());
  }

  // Select operating mode.
  bool        use_polling = false;
  const char* mode_name   = "unknown";

  if (this->gea2_protocol_active_) {
    use_polling = true;
    mode_name   = "polling (GEA2 - subscriptions not supported)";
  } else if (this->mode_ == BRIDGE_MODE_POLL) {
    use_polling = true;
    mode_name   = "polling";
  } else if (this->mode_ == BRIDGE_MODE_SUBSCRIBE) {
    use_polling = false;
    mode_name   = "subscription";
  } else if (this->mode_ == BRIDGE_MODE_AUTO) {
    use_polling                          = false;
    mode_name                            = "auto (starting with subscription)";
    this->subscription_mode_active_      = true;
    this->subscription_activity_detected_ = false;
    this->subscription_start_time_       = millis();
  }

  ESP_LOGI(TAG, "Using %s mode with polling interval: %u ms", mode_name, this->polling_interval_ms_);

  // Initialize the appropriate bridge(s).
  if (use_polling) {
    mqtt_bridge_polling_init(
      &this->mqtt_bridge_polling_,
      &this->timer_group_,
      this->active_erd_client_,
      &this->mqtt_client_adapter_.interface,
      this->polling_interval_ms_,
      this->polling_only_publish_on_change_);
    this->configure_polling_optional_lists_();
  } else {
    mqtt_bridge_init(
      &this->mqtt_bridge_,
      &this->timer_group_,
      this->active_erd_client_,
      &this->mqtt_client_adapter_.interface,
      this->host_address_);

    if (!this->custom_erds_vec_.empty()) {
      this->custom_erd_subscription_seen_erds_.clear();
      this->custom_erd_subscription_last_activity_ = millis();
      ESP_LOGI(TAG, "Custom ERD polling (%zu ERD(s)) will start after subscription settles",
               this->custom_erds_vec_.size());
    }
  }

  this->mqtt_bridge_initialized_ = true;
  ESP_LOGI(TAG, "MQTT bridge initialized successfully");

  // Defer HA device discovery until ERD registration has settled.
  if (this->generate_device_config_) {
    this->ha_discovery_pending_       = true;
    this->ha_discovery_last_activity_ = millis();
    ESP_LOGI(TAG, "HA discovery deferred: will publish after ERD discovery completes "
                  "(polling mode) or %u s quiet window (subscription mode)",
             HA_DISCOVERY_QUIET_MS / 1000);
  }
}

// ---------------------------------------------------------------------------
// Configure optional polling lists after bridge init
// ---------------------------------------------------------------------------

void GeappliancesBridge::configure_polling_optional_lists_()
{
  // Set the API-parsed list before any events fire. state_identify_appliance
  // only checks api_parsed_list in signal_read_completed, so setting it here
  // (synchronously, before any events) is safe.
  if (this->appliance_api_parsing_ && this->appliance_api_valid_list_ready_ &&
      !this->appliance_api_valid_erds_vec_.empty()) {
    this->mqtt_bridge_polling_.api_parsed_list       = this->appliance_api_valid_erds_vec_.data();
    this->mqtt_bridge_polling_.api_parsed_list_count =
      static_cast<uint16_t>(this->appliance_api_valid_erds_vec_.size());
    ESP_LOGI(TAG, "Polling with API-parsed list of %u ERDs (discovery skipped)",
             this->mqtt_bridge_polling_.api_parsed_list_count);
  }

  if (!this->custom_erds_vec_.empty()) {
    this->mqtt_bridge_polling_.custom_erd_list       = this->custom_erds_vec_.data();
    this->mqtt_bridge_polling_.custom_erd_list_count =
      static_cast<uint16_t>(this->custom_erds_vec_.size());
    ESP_LOGI(TAG, "Polling with %u custom ERD(s)", this->mqtt_bridge_polling_.custom_erd_list_count);
  }
}

// ---------------------------------------------------------------------------
// Start custom ERD polling bridge (deferred: called after subscription settles)
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_custom_erd_polling_()
{
  if (this->custom_erd_polling_active_ || this->custom_erds_vec_.empty()) {
    return;
  }
  // Use the pre-known host address so the custom ERD bridge goes directly to
  // state_polling without broadcasting to 0xFF or re-reading feature ERDs.
  // The subscription bridge has already identified the appliance; there is no
  // need to re-identify here.
  mqtt_bridge_polling_init_at_address(
    &this->custom_erd_bridge_,
    &this->timer_group_,
    this->active_erd_client_,
    &this->mqtt_client_adapter_.interface,
    this->polling_interval_ms_,
    this->polling_only_publish_on_change_,
    this->host_address_,
    this->custom_erds_vec_.data(),
    static_cast<uint16_t>(this->custom_erds_vec_.size()));
  this->custom_erd_polling_active_ = true;
  ESP_LOGI(TAG, "Started custom-only ERD polling (%zu ERD(s)) after subscription settled",
           this->custom_erds_vec_.size());
}

void GeappliancesBridge::maybe_start_custom_erd_polling_()
{
  if (this->custom_erd_polling_active_ || this->custom_erds_vec_.empty() ||
      !this->mqtt_bridge_initialized_) {
    return;
  }

  bool in_subscription_mode = (this->mode_ == BRIDGE_MODE_SUBSCRIBE) ||
                              (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_);
  if (!in_subscription_mode) {
    return;
  }

  bool subscription_confirmed = (this->mode_ == BRIDGE_MODE_SUBSCRIBE) ||
                                this->subscription_activity_detected_;
  if (!subscription_confirmed) {
    return;
  }

  if (millis() - this->custom_erd_subscription_last_activity_ < HA_DISCOVERY_QUIET_MS) {
    return;
  }

  this->start_custom_erd_polling_();
}

// ---------------------------------------------------------------------------
// AUTO mode: subscription-activity watchdog
// ---------------------------------------------------------------------------

void GeappliancesBridge::check_subscription_activity_()
{
  if (this->subscription_activity_detected_) {
    return;
  }

  // Unsigned subtraction wraps correctly on the ~49-day millis() rollover.
  uint32_t elapsed = millis() - this->subscription_start_time_;
  if (elapsed < SUBSCRIPTION_TIMEOUT_MS) {
    return;
  }

  ESP_LOGW(TAG, "No subscription activity detected after %u seconds, falling back to polling mode",
           SUBSCRIPTION_TIMEOUT_MS / 1000);

  // Tear down the subscription bridge.
  mqtt_bridge_destroy(&this->mqtt_bridge_);

  // Tear down the custom ERD polling bridge (if active alongside subscription).
  // The full polling bridge initialized below will poll custom ERDs via
  // configure_polling_optional_lists_(), so it does not need to stay active.
  if (this->custom_erd_polling_active_) {
    mqtt_bridge_polling_destroy(&this->custom_erd_bridge_);
    this->custom_erd_polling_active_ = false;
  }

  // Stand up the polling bridge.
  mqtt_bridge_polling_init(
    &this->mqtt_bridge_polling_,
    &this->timer_group_,
    this->active_erd_client_,
    &this->mqtt_client_adapter_.interface,
    this->polling_interval_ms_,
    this->polling_only_publish_on_change_);
  this->configure_polling_optional_lists_();
  this->subscription_mode_active_ = false;

  // In polling mode, HA discovery is gated on polling_list_complete; no timer
  // reset is needed here beyond the conservative safety-net update below.
  if (this->ha_discovery_pending_ && !this->ha_discovery_published_) {
    this->ha_discovery_last_activity_ = millis();
  }

  ESP_LOGI(TAG, "Successfully switched to polling mode");
}

}  // namespace geappliances_bridge
}  // namespace esphome
