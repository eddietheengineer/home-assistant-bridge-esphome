/*!
 * @file
 * @brief Bridge startup initializers and ongoing bridge lifecycle management.
 *
 * MODULE GOAL: Own every one-time initialization step for the MQTT client
 * adapter and the bridge HSMs, plus feature-bit reading startup and the
 * AUTO-mode subscription-activity watchdog.
 *
 * start_feature_bit_reading_() is called by the startup HSM to kick off
 * the feature-bit ERD reads once the MQTT client adapter is ready.
 *
 * initialize_mqtt_client_() runs once as soon as the device ID is ready
 * (Phase 4), before feature bit reading.  It binds the MQTT client adapter
 * to the device ID, sets up the ERD registry (registered-ERD tracking and
 * string-ERD type detection), and passes the registry to the adapter so it
 * is ready to publish ERD values immediately.
 *
 * initialize_erd_bridge_() runs once after feature bit reading and MQTT are
 * both ready (Phase 6).  It applies the valid-ERD filter to the registry
 * (built from feature bit results), selects the operating mode
 * (poll / subscribe / auto), and initializes the appropriate bridge HSMs.
 *
 * check_subscription_activity_() runs every loop() iteration in AUTO mode
 * and falls back to polling if no subscription publications arrive within
 * the timeout window.
 */

#include "geappliances_bridge.h"
#include "appliance_api_feature_lists.h"
#include "geappliances_bridge_constants.h"
#include "geappliances_bridge_startup_hsm.h"
#include "esphome/core/log.h"
#include "tiny_gea_constants.h"

namespace esphome {
namespace geappliances_bridge {
static std::set<tiny_erd_t> erd_cache_to_set(erd_cache_t* cache)
{
  std::set<tiny_erd_t> erds;
  uint16_t iterator = 0;
  while (true) {
    erd_cache_entry_t* entry = erd_cache_get_next_entry(cache, &iterator);
    if (!entry) break;
    erds.insert(entry->erd);
  }
  return erds;
}


// ---------------------------------------------------------------------------
// Startup: kick off feature-bit reading sequence
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_feature_bit_reading_()
{
  // Guard: don't re-initialize if the manager has already started.
  // Any READING_* state means the manager is actively processing,
  // PARSING/COMPLETE mean it's past the reading phase.
  // Note: the feature_bit_reading_started_ flag prevents re-init while the first read is in-flight.
  FeatureBitState state = this->feature_bit_manager_.get_state();
  if (state != FEATURE_BIT_STATE_READING_0008) {
    return;
  }
  // Additional guard: if start() was already called and the first read
  // is in-flight, the manager is still in READING_0008 but read_queued_
  // is true. We can't check read_queued_ directly (it's private), but
  // we track whether we've already kicked off feature bit reading via
  // the feature_bit_reading_started_ flag.
  if (this->feature_bit_reading_started_) {
    return;
  }

  // Guard: only proceed once a valid ERD client is available. If the client
  // is null (e.g. autodiscovery not yet complete), leave the flag unset so
  // the next loop() call retries rather than getting permanently stuck.
  i_tiny_gea3_erd_client_t* erd_client = this->autodiscovery_manager_.get_active_erd_client();
  if (erd_client == nullptr) {
    return;
  }

  // Set the flag only after confirming init() will succeed, so that a
  // transient null-client on an earlier call does not permanently block retry.
  this->feature_bit_reading_started_ = true;

  ESP_LOGI(TAG, "Reading device info ERDs for MQTT publish, then appliance API feature bits...");
  this->feature_bit_manager_.init(
      erd_client,
      this->autodiscovery_manager_.get_host_address(),
      &this->timer_group_);
  this->feature_bit_manager_.start();
}

// ---------------------------------------------------------------------------
// Phase 4: Initialize the MQTT client adapter (called once from loop())
// ---------------------------------------------------------------------------

void GeappliancesBridge::initialize_mqtt_client_()
{
  if (this->mqtt_client_adapter_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "Initializing MQTT client adapter with device ID: %s",
           this->device_identity_manager_.get_device_id().c_str());

  // For manual device_id configs where autodiscovery is skipped (gea2_uart only,
  // no GEA3 uart), mark the protocol as GEA2 so run_protocol_stack_() enables
  // the GEA2 tight loop even before autodiscovery runs.
  if (this->autodiscovery_manager_.get_active_erd_client() == nullptr) {
    if (this->uart_ == nullptr) {
      this->gea2_protocol_active_ = true;
    }
  }

  // Bind the adapter to the device ID.
  esphome_mqtt_client_adapter_init(&this->mqtt_client_adapter_,
                                   this->device_identity_manager_.get_device_id().c_str());

  // Wire up the ERD registry: clears any stale registrations and sets up
  // the MQTT adapter with a single pointer for valid-ERD filtering and
  // registered-ERD tracking.
  this->erd_registry_.clear_registered_erds();
  esphome_mqtt_client_adapter_set_erd_registry(
    &this->mqtt_client_adapter_, &this->erd_registry_);

  this->mqtt_client_adapter_initialized_ = true;
  ESP_LOGI(TAG, "MQTT client adapter initialized; feature bit ERDs will be published as they are read");
}

// ---------------------------------------------------------------------------
// Phase 6: Initialize the ERD bridge (called once from loop())
// ---------------------------------------------------------------------------

void GeappliancesBridge::initialize_erd_bridge_()
{
  if (!this->mqtt_client_adapter_initialized_ || this->erd_bridge_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "Initializing ERD bridge");

  // Apply the valid-ERD filter when appliance API parsing is enabled and
  // produced results. An empty set is ignored by the registry so all ERDs
  // continue to be published in that case.
  if (this->appliance_api_parsing_ &&
      this->feature_bit_manager_.get_state() == FEATURE_BIT_STATE_COMPLETE &&
      !this->feature_bit_manager_.get_valid_erds().empty()) {
    this->erd_registry_.set_valid_erds(this->feature_bit_manager_.get_valid_erds());
    ESP_LOGI(TAG, "Appliance API parsing enabled: publishing filtered to %zu valid ERDs",
             this->feature_bit_manager_.get_valid_erds().size());
  }

  // Select operating mode.
  bool        use_polling = false;

  const char* mode_name = "unknown";
  if (this->autodiscovery_manager_.is_gea2_protocol()) {
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

  (void)mode_name;

  ESP_LOGI(TAG, "Using %s mode with polling interval: %u ms", mode_name, this->polling_interval_ms_);

  // Initialize the appropriate bridge(s).
  if (use_polling) {
    erd_bridge_poll_init(
      &this->erd_bridge_poll_,
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->polling_interval_ms_,
      this->autodiscovery_manager_.get_host_address(),
      this->device_identity_manager_.get_appliance_type(),
      nullptr,
      0,
      &this->erd_cache_);
    // Wire the discovery-complete callback so the startup HSM waits for
    // ERD discovery to finish before transitioning to steady-state.
    this->erd_bridge_poll_.on_discovery_complete = +[](void* ctx) {
      auto* bridge = reinterpret_cast<GeappliancesBridge*>(ctx);
      bridge->ha_discovery_manager_.set_registered_erds(erd_cache_to_set(&bridge->erd_cache_));
      erd_write_bridge_set_host_address(&bridge->erd_write_bridge_, bridge->autodiscovery_manager_.get_host_address());
      tiny_hsm_send_signal(&bridge->startup_hsm_, signal_bridge_ready, nullptr);
    };
    this->erd_bridge_poll_.on_discovery_complete_context = this;
    this->polling_bridge_initialized_ = true;
    this->configure_polling_optional_lists_();
  }

  // Initialize the subscription bridge for non-polling modes (subscribe, auto).
  // In polling mode (GEA2 or explicit poll), subscriptions are not used, but
  // the bridge is still initialized above for custom ERD subscription support.
  if (!use_polling) {
    erd_bridge_subscribe_init(
      &this->erd_bridge_subscribe_,
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->autodiscovery_manager_.get_host_address(),
      &this->erd_cache_);
    this->subscription_bridge_initialized_ = true;

    // Subscription bridge has no discovery phase — signal the startup HSM
    // immediately so it can transition to subscription_watch.
    tiny_hsm_send_signal(&this->startup_hsm_, signal_bridge_ready, nullptr);
  }
  // Initialize the write bridge. It starts with the broadcast address so that
  // write requests are dropped until the appliance is identified.
  erd_write_bridge_init(
    &this->erd_write_bridge_,
    &this->timer_group_,
    this->autodiscovery_manager_.get_active_erd_client(),
    &this->mqtt_client_adapter_.interface,
    tiny_gea_broadcast_address);
  this->write_bridge_initialized_ = true;

  this->erd_bridge_initialized_ = true;
  ESP_LOGI(TAG, "ERD bridge initialized successfully");

  // Defer HA device discovery until ERD registration has settled.
  if (this->generate_device_config_) {
    this->ha_discovery_manager_.init(
        this->ha_discovery_base_url_,
        this->device_identity_manager_.get_device_id(),
        this->device_identity_manager_.get_model_number(),
        this->device_identity_manager_.get_serial_number(),
        erd_cache_to_set(&this->erd_cache_),
        true);
    this->ha_discovery_manager_.set_mqtt_adapter(&this->mqtt_client_adapter_);
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
  if (this->appliance_api_parsing_ &&
      this->feature_bit_manager_.get_state() == FEATURE_BIT_STATE_COMPLETE &&
      !this->feature_bit_manager_.get_valid_erds_vec().empty()) {
    this->erd_bridge_poll_.api_parsed_list       = this->feature_bit_manager_.get_valid_erds_vec().data();
    this->erd_bridge_poll_.api_parsed_list_count =
      static_cast<uint16_t>(this->feature_bit_manager_.get_valid_erds_vec().size());
    ESP_LOGI(TAG, "Polling with API-parsed list of %u ERDs (ERDs will be probed before polling)",
             this->erd_bridge_poll_.api_parsed_list_count);
  }

  if (!this->custom_erds_vec_.empty()) {
    this->erd_bridge_poll_.custom_erd_list       = this->custom_erds_vec_.data();
    this->erd_bridge_poll_.custom_erd_list_count =
      static_cast<uint16_t>(this->custom_erds_vec_.size());
    ESP_LOGI(TAG, "Polling with %u custom ERD(s)", this->erd_bridge_poll_.custom_erd_list_count);
  }
}

// ---------------------------------------------------------------------------
// Start custom ERD polling bridge (deferred: called after subscription settles)
// ---------------------------------------------------------------------------
// When in subscription mode with custom ERDs, this starts a polling bridge
// that polls only the custom ERDs alongside the subscription bridge.
// The subscription bridge continues to handle all standard ERDs, while the
// polling bridge handles custom ERDs that may not be covered by subscription.
// The polling list is allocated to the exact size needed.
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_custom_erd_polling_()
{
  if (this->custom_erds_vec_.empty()) {
    return;
  }
  // Do NOT destroy the subscription bridge - it continues to handle all
  // standard ERD publications. The polling bridge runs alongside it, only
  // polling the custom ERDs that may not be covered by subscription.
  // Both bridges subscribe to the same ERD client activity event, but they
  // handle different event types (subscription vs read_completed).

  ESP_LOGI(TAG, "Started custom ERD polling (%zu ERD(s)) alongside subscription bridge",
           this->custom_erds_vec_.size());

  // Initialize a polling bridge with the custom ERDs as the api_parsed_list.
  // This skips discovery states and goes straight to polling with an exact-size list.
    erd_bridge_poll_init(
      &this->erd_bridge_poll_,
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->polling_interval_ms_,
      this->autodiscovery_manager_.get_host_address(),
      this->device_identity_manager_.get_appliance_type(),
      this->custom_erds_vec_.data(),
      static_cast<uint16_t>(this->custom_erds_vec_.size()),
      &this->erd_cache_);
  this->custom_erd_polling_started_ = true;
  this->polling_bridge_initialized_ = true;
}

void GeappliancesBridge::maybe_start_custom_erd_polling_()
{
  if (this->custom_erds_vec_.empty() ||
      !this->erd_bridge_initialized_ ||
      this->custom_erd_polling_started_) {
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
  erd_bridge_subscribe_destroy(&this->erd_bridge_subscribe_);
  this->subscription_bridge_initialized_ = false;

  // Destroy any existing polling bridge (e.g., from custom ERD polling)
  // before re-initializing to avoid leaking heap allocations.
  if (this->custom_erd_polling_started_) {
    erd_bridge_poll_destroy(&this->erd_bridge_poll_);
    this->custom_erd_polling_started_ = false;
    this->polling_bridge_initialized_ = false;
  }

  // Stand up the polling bridge.
    erd_bridge_poll_init(
      &this->erd_bridge_poll_,
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->polling_interval_ms_,
      this->autodiscovery_manager_.get_host_address(),
      this->device_identity_manager_.get_appliance_type(),
      nullptr,
      0,
      &this->erd_cache_);
  this->polling_bridge_initialized_ = true;
  // Wire the discovery-complete callback so HA discovery registration
  this->erd_bridge_poll_.on_discovery_complete = +[](void* ctx) {
    auto* bridge = reinterpret_cast<GeappliancesBridge*>(ctx);
    bridge->ha_discovery_manager_.set_registered_erds(erd_cache_to_set(&bridge->erd_cache_));
    erd_write_bridge_set_host_address(&bridge->erd_write_bridge_, bridge->autodiscovery_manager_.get_host_address());
    tiny_hsm_send_signal(&bridge->startup_hsm_, signal_bridge_ready, nullptr);
  };
  this->erd_bridge_poll_.on_discovery_complete_context = this;
  this->configure_polling_optional_lists_();
  this->subscription_mode_active_ = false;

  // Signal the startup HSM that subscription fallback has occurred.
  tiny_hsm_send_signal(&this->startup_hsm_, signal_subscription_fallback, nullptr);

  ESP_LOGI(TAG, "Successfully switched to polling mode");
}

}  // namespace geappliances_bridge
}  // namespace esphome
