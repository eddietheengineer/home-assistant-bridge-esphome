/*!
 * @file
 * @brief Startup state machine implementation.
 *
 * Each state function handles entry/exit signals and the signal_run_loop
 * signal that drives ongoing work.  Transitions to the next state happen
 * when the required gate conditions are met (e.g., autodiscovery complete,
 * device ID ready, MQTT connected, etc.).
 *
 * The hierarchy is flat — all states defer to startup_state_top for signals
 * they don't handle, providing a common "root" for any unhandled signals.
 */

#include "i_bridge_services.h"
#include "geappliances_bridge_constants.h"
#include "geappliances_bridge_startup_hsm.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/mqtt/mqtt_client.h"

extern "C" {
#include "tiny_utils.h"  // element_count macro
}

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "geappliances_bridge";

// Back-pointer to the bridge services, set during HSM init.
// This allows HSM state functions to invoke bridge operations through a
// stable interface without a compile-time dependency on GeappliancesBridge.
static IBridgeServices* g_bridge_services = nullptr;

IBridgeServices* services_from_hsm(tiny_hsm_t* hsm)
{
  (void)hsm;
  return g_bridge_services;
}

void set_bridge_services(IBridgeServices* services)
{
  g_bridge_services = services;
}


// ============================================================================
// Top state — handles signals common to all startup phases
// ============================================================================

tiny_hsm_result_t startup_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)data;
  (void)hsm;

  // The top state is the root of the hierarchy.  Any signal not consumed
  // by a child state will bubble up here and be deferred (ignored).
  switch (signal) {
    case tiny_hsm_signal_entry:
    case tiny_hsm_signal_exit:
      // No common entry/exit logic needed.
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 1: Protocol Stack — drive GEA2/GEA3 hardware
//
// This is the initial state.  It transitions to autodiscovery as soon as
// the first loop() call arrives (the protocol stack is always running).
// ============================================================================

tiny_hsm_result_t startup_state_protocol_stack(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)svc;
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      // Transition to the startup delay state.  The delay gives the appliance
      // board time to stabilize before we start broadcasting.
      tiny_hsm_transition(hsm, startup_state_startup_delay);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 1.5: Startup Delay — wait for appliance board to stabilize
//
// Waits AUTODISCOVERY_STARTUP_DELAY_MS (5 seconds) before transitioning to
// autodiscovery.  This gives the appliance board time to boot and be ready
// to respond to broadcast requests.
// ============================================================================

tiny_hsm_result_t startup_state_startup_delay(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      svc->record_startup_delay_start();
      ESP_LOGI(TAG, "Startup: %u second stabilization delay",
               static_cast<unsigned>(AUTODISCOVERY_STARTUP_DELAY_MS / 1000));
      break;

    case signal_run_loop:
      if (svc->is_startup_delay_elapsed()) {
        tiny_hsm_transition(hsm, startup_state_autodiscovery);
      }
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 2: Autodiscovery — find appliance on bus
//
// The AutodiscoveryManager is fully self-driving: it owns its own timers
// and event subscriptions.  The HSM calls start() on entry, then checks
// for completion on each loop iteration and via the completion signal.
// If no board responds, the manager keeps retrying indefinitely — this
// state will not transition until a valid board address is found.
// ============================================================================

tiny_hsm_result_t startup_state_autodiscovery(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Autodiscovery phase");
      // Kick off the self-driving autodiscovery manager.
      svc->run_autodiscovery();
      break;

    case signal_run_loop:
      // Manager is self-driving — just check if it completed.
      if (svc->is_autodiscovery_complete()) {
        ESP_LOGI(TAG, "Autodiscovery complete (host=0x%02X, protocol=%s)",
                 svc->get_discovered_host_address(),
                 svc->is_discovered_gea2_protocol() ? "GEA2" : "GEA3");
        tiny_hsm_transition(hsm, startup_state_device_id);
      }
      break;

    case signal_autodiscovery_complete:
      // External signal from autodiscovery callback — transition only if
      // a board was actually discovered (not on failure/no-response).
      if (svc->is_autodiscovery_complete()) {
        tiny_hsm_transition(hsm, startup_state_device_id);
      }
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 3: Device ID — read appliance identity ERDs
//
// Runs the DeviceIdentityManager each loop iteration.  Transitions to
// mqtt_client_init when the device ID is ready (read or pre-configured).
// ============================================================================

tiny_hsm_result_t startup_state_device_id(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      svc->init_device_id_reading();
      // If a device_id is pre-configured and the manager completes synchronously
      // during init(), transition immediately.
      if (svc->is_device_id_complete()) {
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      }
      break;

    case signal_run_loop:
      // Manager is fully self-driving — just check if it completed.
      if (svc->is_device_id_complete()) {
        tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      }
      break;

    case signal_device_id_complete:
      tiny_hsm_transition(hsm, startup_state_mqtt_client_init);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 4: MQTT Client Init — initialize the MQTT client adapter
//
// Initializes the MQTT client adapter with the device ID, then transitions
// to feature_bits.  This phase is fast — it doesn't wait for MQTT connection.
// ============================================================================

tiny_hsm_result_t startup_state_mqtt_client_init(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      if (!svc->is_mqtt_client_initialized()) {
        svc->initialize_mqtt_client();
      }
      svc->start_feature_bit_reading();
      tiny_hsm_transition(hsm, startup_state_feature_bits);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 5: Feature Bits — read appliance API feature bit ERDs
//
// Runs the FeatureBitManager each loop iteration.  Transitions to
// bridge_init when feature bits are complete AND MQTT is connected.
// ============================================================================

tiny_hsm_result_t startup_state_feature_bits(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Feature bits phase");
      svc->record_feature_bits_phase_start();
      break;

    case signal_run_loop:
      {
      if (svc->is_feature_bits_phase_timed_out()) {
        ESP_LOGW(TAG, "Feature bits phase timed out, continuing without feature filtering");
        svc->mark_feature_bits_timed_out();
      }

      svc->run_feature_bits();

      bool feature_bits_done = svc->is_feature_bits_complete();
      bool mqtt_connected = (mqtt::global_mqtt_client != nullptr &&
                             mqtt::global_mqtt_client->is_connected());

      if (feature_bits_done && mqtt_connected) {
        tiny_hsm_transition(hsm, startup_state_bridge_init);
      }
      }
      break;

    case signal_mqtt_connected:
      if (svc->is_feature_bits_complete()) {
        tiny_hsm_transition(hsm, startup_state_bridge_init);
      }
      break;

    case signal_feature_bits_complete:
      {
        bool mqtt_connected = (mqtt::global_mqtt_client != nullptr &&
                               mqtt::global_mqtt_client->is_connected());
        if (mqtt_connected) {
          tiny_hsm_transition(hsm, startup_state_bridge_init);
        }
      }
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 6: Bridge Init — initialize the MQTT bridge (poll or subscribe)
//
// Waits for MQTT connection, then initializes the appropriate bridge.
// Transitions to subscription_watch on completion.
// ============================================================================

tiny_hsm_result_t startup_state_bridge_init(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: Bridge init phase");
      break;

    case signal_run_loop:
      if (!svc->is_bridge_initialized() &&
          svc->is_autodiscovery_complete() &&
          mqtt::global_mqtt_client != nullptr &&
          mqtt::global_mqtt_client->is_connected()) {
        ESP_LOGI(TAG, "Device ID ready and MQTT connected, initializing MQTT bridge");
        svc->initialize_mqtt_bridge();
        tiny_hsm_transition(hsm, startup_state_subscription_watch);
      }
      break;

    case signal_mqtt_connected:
      if (!svc->is_bridge_initialized() && svc->is_autodiscovery_complete()) {
        ESP_LOGI(TAG, "MQTT connected, initializing MQTT bridge");
        svc->initialize_mqtt_bridge();
        tiny_hsm_transition(hsm, startup_state_subscription_watch);
      }
      break;

    case signal_bridge_ready:
      tiny_hsm_transition(hsm, startup_state_subscription_watch);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 7: Subscription Watch — AUTO mode subscription watchdog
//
// In AUTO mode, monitors subscription activity and falls back to polling
// if no activity is detected within the timeout.  In poll/subscribe modes,
// this phase is a no-op and transitions immediately.
// ============================================================================

tiny_hsm_result_t startup_state_subscription_watch(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      if (svc->get_mode() != BRIDGE_MODE_AUTO) {
        svc->maybe_start_custom_erd_polling();
        tiny_hsm_transition(hsm, startup_state_ha_discovery);
      }
      break;

    case signal_run_loop:
      if (svc->get_mode() == BRIDGE_MODE_AUTO && svc->is_subscription_mode_active()) {
        svc->check_subscription_activity();
      }
      svc->maybe_start_custom_erd_polling();
      svc->log_poll_state_transitions();

      if (svc->get_mode() != BRIDGE_MODE_AUTO || !svc->is_subscription_mode_active()) {
        tiny_hsm_transition(hsm, startup_state_ha_discovery);
      }
      break;

    case signal_subscription_fallback:
      tiny_hsm_transition(hsm, startup_state_ha_discovery);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 8: HA Discovery — publish Home Assistant entity configs
//
// Runs the HaDiscoveryManager each loop iteration.  Transitions to
// heap_monitor once HA discovery is complete or not needed.
// ============================================================================

tiny_hsm_result_t startup_state_ha_discovery(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Startup: HA discovery phase");
      break;

    case signal_run_loop:
      svc->run_ha_discovery();
      tiny_hsm_transition(hsm, startup_state_running);
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Phase 9: Running — steady-state operation
//
// All recurring tasks run every loop() iteration.  This is the terminal
// state of the startup sequence.
// ============================================================================

tiny_hsm_result_t startup_state_running(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  IBridgeServices* svc = services_from_hsm(hsm);
  (void)data;

  switch (signal) {
    case tiny_hsm_signal_entry:
      ESP_LOGI(TAG, "Bridge is now in steady-state operation");
      break;

    case signal_run_loop:
      svc->run_all_managers();

      if (svc->get_mode() == BRIDGE_MODE_AUTO && svc->is_subscription_mode_active()) {
        svc->check_subscription_activity();
      }
      svc->maybe_start_custom_erd_polling();
      svc->log_poll_state_transitions();
      svc->run_ha_discovery();
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// HSM configuration — state descriptors with parent hierarchy
// ============================================================================

static const tiny_hsm_state_descriptor_t startup_hsm_state_descriptors[] = {
  { .state = startup_state_top,              .parent = nullptr },
  { .state = startup_state_protocol_stack,   .parent = startup_state_top },
  { .state = startup_state_startup_delay,    .parent = startup_state_top },
  { .state = startup_state_autodiscovery,    .parent = startup_state_top },
  { .state = startup_state_device_id,        .parent = startup_state_top },
  { .state = startup_state_mqtt_client_init, .parent = startup_state_top },
  { .state = startup_state_feature_bits,     .parent = startup_state_top },
  { .state = startup_state_bridge_init,      .parent = startup_state_top },
  { .state = startup_state_subscription_watch, .parent = startup_state_top },
  { .state = startup_state_ha_discovery,     .parent = startup_state_top },
  { .state = startup_state_running,          .parent = startup_state_top },
};

const tiny_hsm_configuration_t startup_hsm_configuration = {
  .states = startup_hsm_state_descriptors,
  .state_count = element_count(startup_hsm_state_descriptors)
};

}  // namespace geappliances_bridge
}  // namespace esphome
