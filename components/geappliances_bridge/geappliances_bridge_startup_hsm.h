/*!
 * @file
 * @brief Startup state machine for the GE Appliances Bridge.
 *
 * Replaces the manual switch-based phase progression with a proper
 * tiny_hsm-based hierarchical state machine.  The startup sequence is:
 *
 *   protocol_stack → autodiscovery → device_id → mqtt_client_init
 *                 → feature_bits → bridge_init → subscription_watch
 *                 → ha_discovery → running
 *
 * Each state handles its own entry/exit logic and waits for signals
 * from the managers (autodiscovery, device identity, feature bits,
 * MQTT bridge) before transitioning to the next phase.
 *
 * The "running" state is the steady-state where all recurring tasks
 * run every loop() iteration.
 */

#ifndef startup_hsm_h
#define startup_hsm_h

#include "tiny_hsm.h"

// ============================================================================
// Startup HSM signal identifiers
// ============================================================================

enum {
  signal_run_loop = tiny_hsm_signal_user_start,  // Drive ongoing work in current state
  signal_autodiscovery_complete,                  // Autodiscovery found (or gave up on) appliance
  signal_device_id_complete,                      // Device ID ready (read or pre-configured)
  signal_device_id_failed,                        // Device ID read failed, using fallback
  signal_mqtt_connected,                          // MQTT broker connection established
  signal_feature_bits_complete,                   // All feature bit ERDs read and parsed
  signal_bridge_ready,                            // MQTT bridge (poll/subscribe) initialized
  signal_subscription_fallback,                   // AUTO mode: subscription timed out, fell back to polling
};

// ============================================================================
// Startup HSM state function declarations & configuration
// ============================================================================

namespace esphome {
namespace geappliances_bridge {

// Forward declaration of the bridge class
class GeappliancesBridge;

tiny_hsm_result_t startup_state_top(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_protocol_stack(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_autodiscovery(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_device_id(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_mqtt_client_init(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_feature_bits(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_bridge_init(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_subscription_watch(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_ha_discovery(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_running(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

/// Set the back-pointer to the bridge instance so the HSM state functions
/// can access it without using container_of/offsetof on a non-POD class.
void set_bridge_instance(GeappliancesBridge* bridge);

// HSM configuration (state descriptors + hierarchy)
extern const tiny_hsm_configuration_t startup_hsm_configuration;

}  // namespace geappliances_bridge
}  // namespace esphome

#endif
