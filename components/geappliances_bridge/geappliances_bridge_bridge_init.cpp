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
#include "erd_poll_list_builder.h"
#include "erd_cache.h"

namespace esphome {
namespace geappliances_bridge {
static void erd_cache_to_array(erd_cache_t* cache, tiny_erd_t* out, uint16_t* count)
{
  *count = 0;
  uint16_t iterator = 0;
  while (*count < ERD_CACHE_CAPACITY) {
    erd_cache_entry_t* entry = erd_cache_get_next_entry(cache, &iterator);
    if (!entry) break;
    out[(*count)++] = entry->erd;
  }
}
// ---------------------------------------------------------------------------
// Polling bridge discovery-complete callback (shared by all three init paths)
// ---------------------------------------------------------------------------

void GeappliancesBridge::on_poll_discovery_complete_()
{
  tiny_erd_t erds[ERD_CACHE_CAPACITY];
  uint16_t count = 0;
  erd_cache_to_array(&this->erd_cache_, erds, &count);
  this->ha_discovery_manager_.set_registered_erds(erds, count);
  erd_write_bridge_set_host_address(&this->erd_write_bridge_, this->autodiscovery_manager_.get_host_address());
  tiny_hsm_send_signal(&this->startup_hsm_, signal_bridge_ready, nullptr);
}


// ---------------------------------------------------------------------------
// Build the poll list using the erd_poll_list_builder module
// ---------------------------------------------------------------------------

ErdPollListResult build_poll_list_(GeappliancesBridge* bridge)
{
  ErdPollListConfig config;
  config.mode = bridge->mode_;
  config.subscription_capable = !bridge->autodiscovery_manager_.is_gea2_protocol();
  config.subscription_active = bridge->subscription_mode_active_;
  config.appliance_api_parsing = bridge->appliance_api_parsing_;
  config.feature_bit_valid_erds = bridge->feature_bit_manager_.get_valid_erd_count() ? bridge->feature_bit_manager_.valid_erds_ : nullptr;
  config.feature_bit_valid_erds_count = bridge->feature_bit_manager_.get_valid_erd_count();
  config.custom_erds = &bridge->custom_erds_vec_;
  config.appliance_type = bridge->device_identity_manager_.get_appliance_type();
  return build_erd_poll_list(config);
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
  if (state != FEATURE_BIT_STATE_READING_0092) {
    return;
  }
  // Additional guard: if start() was already called and the first read
  // is in-flight, the manager is still in READING_0092 but read_queued_
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

  ESP_LOGI(TAG, "Reading appliance API feature bits...");
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
      this->feature_bit_manager_.get_valid_erd_count() > 0) {
    this->erd_registry_.set_valid_erds(this->feature_bit_manager_.valid_erds_,
                                       this->feature_bit_manager_.get_valid_erd_count());
    ESP_LOGI(TAG, "Appliance API parsing enabled: publishing filtered to %u valid ERDs",
             this->feature_bit_manager_.get_valid_erd_count());
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

  // Wire the discovery-complete callback BEFORE initializing the bridge,
  // so the HSM cannot fire the callback before it's set (race condition
  // when discovery completes synchronously on first entry).
  this->erd_bridge_poll_.on_discovery_complete = +[](void* ctx) {
    reinterpret_cast<GeappliancesBridge*>(ctx)->on_poll_discovery_complete_();
  };
  this->erd_bridge_poll_.on_discovery_complete_context = this;

  // Initialize the appropriate bridge(s).
  if (use_polling) {
    auto result = build_poll_list_(this);
    this->poll_probe_list_ = result.erds;
    ESP_LOGI(TAG, "Poll list: %s (%zu ERDs)", result.description.c_str(),
             this->poll_probe_list_.size());
    erd_bridge_poll_init(
      &this->erd_bridge_poll_,
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->polling_interval_ms_,
      this->autodiscovery_manager_.get_host_address(),
      this->device_identity_manager_.get_appliance_type(),
      this->poll_probe_list_.data(),
      static_cast<uint16_t>(this->poll_probe_list_.size()),
      &this->erd_cache_);
    erd_cache_set_only_publish_onchange(&this->erd_cache_, this->polling_only_publish_on_change_);
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
    tiny_erd_t erds[ERD_CACHE_CAPACITY];
    uint16_t count = 0;
    erd_cache_to_array(&this->erd_cache_, erds, &count);
    this->ha_discovery_manager_.init(
        this->ha_discovery_base_url_,
        this->device_identity_manager_.get_device_id(),
        this->device_identity_manager_.get_model_number(),
        this->device_identity_manager_.get_serial_number(),
        erds, count,
        true);
    this->ha_discovery_manager_.set_mqtt_adapter(&this->mqtt_client_adapter_);
    ESP_LOGI(TAG, "HA discovery deferred: will publish after ERD discovery completes "
                  "(polling mode) or %u s quiet window (subscription mode)",
             HA_DISCOVERY_QUIET_MS / 1000);
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

  auto result = build_poll_list_(this);
  this->poll_probe_list_ = result.erds;
  ESP_LOGI(TAG, "Custom ERD polling list: %s (%zu ERDs)", result.description.c_str(),
           this->poll_probe_list_.size());

  // Wire the discovery-complete callback BEFORE initializing the bridge,
  // so the HSM cannot fire the callback before it's set.
  this->erd_bridge_poll_.on_discovery_complete = +[](void* ctx) {
    reinterpret_cast<GeappliancesBridge*>(ctx)->on_poll_discovery_complete_();
  };
  this->erd_bridge_poll_.on_discovery_complete_context = this;

  erd_bridge_poll_init(
      &this->erd_bridge_poll_,
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->polling_interval_ms_,
      this->autodiscovery_manager_.get_host_address(),
      this->device_identity_manager_.get_appliance_type(),
      this->poll_probe_list_.data(),
      static_cast<uint16_t>(this->poll_probe_list_.size()),
      &this->erd_cache_);
  erd_cache_set_only_publish_onchange(&this->erd_cache_, this->polling_only_publish_on_change_);
  this->polling_bridge_initialized_ = true;
  this->custom_erd_polling_started_ = true;
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
  // Wait for the quiet window to elapse before starting custom ERD polling.
  // This gives the subscription bridge time to publish its ERDs, so we can
  // avoid redundant polling of ERDs already covered by subscription.

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
  // Wire the discovery-complete callback BEFORE initializing the bridge,
  // so the HSM cannot fire the callback before it's set (race condition
  // when discovery completes synchronously on first entry).
  this->erd_bridge_poll_.on_discovery_complete = +[](void* ctx) {
    reinterpret_cast<GeappliancesBridge*>(ctx)->on_poll_discovery_complete_();
  };
  this->erd_bridge_poll_.on_discovery_complete_context = this;

  this->subscription_mode_active_ = false;

  auto result = build_poll_list_(this);
  this->poll_probe_list_ = result.erds;
  ESP_LOGI(TAG, "Poll list: %s (%zu ERDs)", result.description.c_str(),
           this->poll_probe_list_.size());

  erd_bridge_poll_init(
      &this->erd_bridge_poll_,
      &this->timer_group_,
      this->autodiscovery_manager_.get_active_erd_client(),
      this->polling_interval_ms_,
      this->autodiscovery_manager_.get_host_address(),
      this->device_identity_manager_.get_appliance_type(),
      this->poll_probe_list_.data(),
      static_cast<uint16_t>(this->poll_probe_list_.size()),
      &this->erd_cache_);
  this->polling_bridge_initialized_ = true;
  erd_cache_set_only_publish_onchange(&this->erd_cache_, this->polling_only_publish_on_change_);

  // Signal the startup HSM that subscription fallback has occurred.
  tiny_hsm_send_signal(&this->startup_hsm_, signal_subscription_fallback, nullptr);

  ESP_LOGI(TAG, "Successfully switched to polling mode");
}

}  // namespace geappliances_bridge
}  // namespace esphome
