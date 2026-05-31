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
 * initialize_mqtt_bridge_() runs once after feature bit reading and MQTT are
 * both ready (Phase 6).  It applies the valid-ERD filter to the registry
 * (built from feature bit results), selects the operating mode
 * (poll / subscribe / auto), and initializes the appropriate bridge HSMs.
 *
 * check_subscription_activity_() runs every loop() iteration in AUTO mode
 * and falls back to polling if no subscription publications arrive within
 * the timeout window.
 */

#include "geappliances_bridge.h"
#include "ha_discovery_config.h"
#include "appliance_api_feature_lists.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "geappliances_bridge";

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

  // Wire up the ERD registry: clears any stale registrations and populates
  // string-type ERDs from the generated config so the adapter publishes
  // ASCII text instead of hex for those ERDs.  All three responsibilities
  // (valid-ERD filtering, string-type detection, registered-ERD tracking)
  // are owned by the registry and accessed through a single pointer.
  this->erd_registry_.clear_registered_erds();
  this->erd_registry_.init_string_erds(ha_string_erd_ids, ha_string_erd_count);
  esphome_mqtt_client_adapter_set_erd_registry(
    &this->mqtt_client_adapter_, &this->erd_registry_);

  this->mqtt_client_adapter_initialized_ = true;
  ESP_LOGI(TAG, "MQTT client adapter initialized; feature bit ERDs will be published as they are read");

  // ── Construct new FSM-based modules ────────────────────────────────────────
  // All modules are constructed now that the adapter is initialized.

  // Global state registry
  global_registry_ = std::make_unique<GlobalStateRegistry>();
  global_registry_->set_device_id(this->device_identity_manager_.get_device_id());
  global_registry_->set_appliance_address(this->autodiscovery_manager_.get_host_address());
  global_registry_->set_gea_protocol_type(this->gea2_protocol_active_ ? 2 : 3);

  // Shared state structures
  erd_state_table_ = std::make_unique<ErdStateTable>();
  write_queue_ = std::make_unique<WriteQueue>();

  // Construct mode-appropriate handlers
  if (mode_ == BRIDGE_MODE_SUBSCRIBE || mode_ == BRIDGE_MODE_AUTO) {
    subscription_handler_ = std::make_unique<SubscriptionHandler>(
      &this->erd_client_.interface, erd_state_table_.get(), &this->timer_group_);
  }
  if (mode_ == BRIDGE_MODE_POLL || mode_ == BRIDGE_MODE_AUTO) {
    polling_handler_ = std::make_unique<PollingHandler>(
      &this->erd_client_.interface, erd_state_table_.get(),
      this->polling_interval_ms_, this->polling_only_publish_on_change_);
  }
  write_handler_ = std::make_unique<WriteHandler>(&this->erd_client_.interface, write_queue_.get());

  // Appliance-side FSM (takes ownership of the handlers)
  appliance_fsm_ = std::make_unique<ApplianceSideStateMachine>(
    erd_state_table_.get(), global_registry_.get(), write_queue_.get(),
    &this->erd_client_.interface);

  // Write router (requires adapter to be initialized)
  write_router_ = std::make_unique<WriteRouter>(&this->mqtt_client_adapter_.interface, write_queue_.get());

  // MQTT-side FSM
  mqtt_fsm_ = std::make_unique<MqttSideStateMachine>(
    erd_state_table_.get(), global_registry_.get(), &this->erd_registry_,
    &this->mqtt_client_adapter_, write_router_.get());
}

// ---------------------------------------------------------------------------
// Phase 4: Initialize the MQTT bridge (called once from loop())
// ---------------------------------------------------------------------------

void GeappliancesBridge::initialize_mqtt_bridge_()
{
  if (!this->mqtt_client_adapter_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "Initializing MQTT bridge");

  // Apply the valid-ERD filter when appliance API parsing is enabled and
  // produced results. An empty set is ignored by the registry so all ERDs
  // continue to be published in that case.
  if (this->appliance_api_parsing_ && this->feature_bit_manager_.get_state() == FEATURE_BIT_STATE_COMPLETE &&\
      !this->feature_bit_manager_.get_valid_erds().empty()) {
    this->erd_registry_.set_valid_erds(this->feature_bit_manager_.get_valid_erds());
    ESP_LOGI(TAG, "Appliance API parsing enabled: publishing filtered to %zu valid ERDs",
             this->feature_bit_manager_.get_valid_erds().size());
  }

  // Select operating mode.
  const char* mode_name = "unknown";

  if (this->autodiscovery_manager_.is_gea2_protocol()) {
    mode_name = "polling (GEA2 - subscriptions not supported)";
  } else if (this->mode_ == BRIDGE_MODE_POLL) {
    mode_name = "polling";
  } else if (this->mode_ == BRIDGE_MODE_SUBSCRIBE) {
    mode_name = "subscription";
  } else if (this->mode_ == BRIDGE_MODE_AUTO) {
    mode_name = "auto (starting with subscription)";
    this->subscription_mode_active_ = true;
    this->subscription_activity_detected_ = false;
    this->subscription_start_time_ = millis();
  }

  ESP_LOGI(TAG, "Using %s mode with polling interval: %u ms", mode_name, this->polling_interval_ms_);
  (void)mode_name; // Suppress -Wunused-but-set-variable (ESP_LOGI may not reference it in all builds)

  // Defer HA device discovery until ERD registration has settled.
  if (this->generate_device_config_) {
    this->ha_discovery_manager_.init(
        this->ha_discovery_base_url_,
        this->device_identity_manager_.get_device_id(),
        this->device_identity_manager_.get_model_number(),
        this->device_identity_manager_.get_serial_number(),
        this->erd_registry_.registered_erds(),
        true);
    this->ha_discovery_manager_.set_registered_erds(this->erd_registry_.registered_erds());
    this->ha_discovery_manager_.set_mqtt_adapter(&this->mqtt_client_adapter_);
    ESP_LOGI(TAG, "HA discovery deferred: will publish after ERD discovery completes "
                  "(polling mode) or %u s quiet window (subscription mode)",
             HA_DISCOVERY_QUIET_MS / 1000);
  }

  this->mqtt_bridge_initialized_ = true;
}

// ---------------------------------------------------------------------------
// Configure optional polling lists after bridge init
// ---------------------------------------------------------------------------

void GeappliancesBridge::configure_polling_optional_lists_()
{
  // No-op: polling list configuration is now handled by the new FSM architecture.
}

// ---------------------------------------------------------------------------
// Start custom ERD polling bridge (deferred: called after subscription settles)
// ---------------------------------------------------------------------------

void GeappliancesBridge::start_custom_erd_polling_()
{
  // No-op: custom ERD polling is now handled by the new FSM architecture.
}

void GeappliancesBridge::maybe_start_custom_erd_polling_()
{
  // No-op: custom ERD polling is now handled by the new FSM architecture.
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

  this->subscription_mode_active_ = false;

  // Signal the startup HSM that subscription fallback has occurred.
  tiny_hsm_send_signal(&this->startup_hsm_, signal_subscription_fallback, nullptr);

  ESP_LOGI(TAG, "Successfully switched to polling mode");
}

}  // namespace geappliances_bridge
}  // namespace esphome
