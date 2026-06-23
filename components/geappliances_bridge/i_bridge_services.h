// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Define the contract between the startup state machine and the bridge
//       so the HSM can drive startup phases without a compile-time dependency
//       on GeappliancesBridge's internals.
//
// Responsibilities:
//   - Declare every operation the startup HSM is allowed to invoke on the bridge
//   - Provide phase-transition queries (is_X_complete, is_X_failed)
//   - Provide phase-timing helpers (record_*_phase_start, is_*_timed_out)
//   - Group recurring-work dispatch methods so the running state stays clean
//
// NOT responsible for:
//   - Implementing any phase logic (GeappliancesBridge does that)
//   - Owning any state
//   - Anything not called by geappliances_bridge_startup_hsm.cpp
//
// Dependencies:
//   - bridge_mode.h (BridgeMode enum)
// =============================================================================

#pragma once

#include <cstdint>

#include "bridge_mode.h"
#include "erd_bridge_common.h"

namespace esphome {
namespace geappliances_bridge {

class IBridgeServices {
 public:
  virtual ~IBridgeServices() = default;

  // -- Autodiscovery ---------------------------------------------------------

  /// Run one tick of the autodiscovery manager.
  virtual void run_autodiscovery() = 0;
  /// Returns true once autodiscovery has found (or given up on) an appliance.
  virtual bool is_autodiscovery_complete() const = 0;
  /// Host address of the discovered appliance (valid when is_autodiscovery_complete()).
  virtual uint8_t get_discovered_host_address() const = 0;
  /// True if the discovered appliance uses the GEA2 protocol.
  virtual bool is_discovered_gea2_protocol() const = 0;

  // -- Device ID -------------------------------------------------------------

  /// Initialize device-ID reading (idempotent when already complete).
  virtual void init_device_id_reading() = 0;
  /// Returns true once the device ID has been determined.
  virtual bool is_device_id_complete() const = 0;

  // -- MQTT client adapter ---------------------------------------------------

  /// Returns true if the MQTT client adapter has been initialized.
  virtual bool is_mqtt_client_initialized() const = 0;
  /// Initialize the MQTT client adapter (idempotent).
  virtual void initialize_mqtt_client() = 0;

  // -- Feature bits ----------------------------------------------------------

  /// Begin the feature-bit reading sequence (self-driving, no further polling needed).
  virtual void start_feature_bit_reading() = 0;
  /// Returns true once all feature-bit ERDs have been read and parsed.
  virtual bool is_feature_bits_complete() const = 0;

  // -- Bridge initialization -------------------------------------------------

  /// Returns true if the ERD bridge (poll/subscribe) has been initialized.
  virtual bool is_bridge_initialized() const = 0;
  /// Initialize the ERD bridge in the mode selected by configuration.
  virtual void initialize_erd_bridge() = 0;

  // -- Operating mode --------------------------------------------------------

  virtual BridgeMode get_mode() const = 0;
  virtual subscription_state_t get_subscription_state() const = 0;
  virtual polling_state_t get_polling_state() const = 0;

  // -- Startup delay ---------------------------------------------------------

  /// Record the timestamp when the startup delay phase begins.
  virtual void record_startup_delay_start() = 0;
  /// Returns true if the startup delay has elapsed.
  virtual bool is_startup_delay_elapsed() const = 0;

  // -- Recurring tasks (called from subscription_watch / running states) ------

  /// Start custom-ERD polling bridge if conditions are met (idempotent).
  virtual void maybe_start_custom_erd_polling() = 0;
  /// Called when the subscription bridge enters the failed state; triggers
  /// fallback to polling mode in AUTO mode.
  virtual void handle_subscription_failed() = 0;
  /// Called when the polling bridge enters the failed state while running
  /// alongside a subscription bridge; cleans up the polling bridge.
  virtual void handle_polling_failed() = 0;
  /// Log any pending polling-bridge state-name transitions.
  virtual void log_poll_state_transitions() = 0;
  /// Check if the appliance-side data path has reached steady-state operation.
  /// Non-const: sets the steady-state flag and logs on first transition.
  /// Returns true only on the first call that detects steady state.
  virtual bool check_steady_state() = 0;
  /// Run one tick of all managers (autodiscovery, device-ID, feature bits).
  virtual void run_all_managers() = 0;

  // -- ERD cache MQTT publisher ----------------------------------------------

  /// Initialize the ERD cache MQTT publisher (idempotent).
  virtual void initialize_erd_cache_publisher() = 0;
  /// Returns true if the ERD cache publisher has been initialized.
  virtual bool is_erd_cache_publisher_initialized() const = 0;
};

}  // namespace geappliances_bridge
}  // namespace esphome
