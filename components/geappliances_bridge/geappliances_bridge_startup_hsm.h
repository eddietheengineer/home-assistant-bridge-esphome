/*!
 * @file
 * @brief Startup state machine for the GE Appliances Bridge.
 *
 * Replaces the manual switch-based phase progression with a proper
 * tiny_hsm-based hierarchical state machine.  The startup sequence is:
 *
 *   protocol_stack → autodiscovery → device_id → mqtt_client_init
 *                 → feature_bits → bridge_init → subscription_watch
 *                 → running
 *
 * Each state handles its own entry/exit logic and waits for signals
 * from the managers (autodiscovery, device identity, feature bits,
 * ERD bridge) before transitioning to the next phase.
 *
 * The "running" state is the steady-state where all recurring tasks
 * run every loop() iteration.
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Drive the ordered startup phase sequence from protocol initialization
//       to steady-state running.
//
// Responsibilities:
//   - Own the tiny_hsm state machine for all startup phases
//   - Transition between phases when each manager signals completion
//   - Enforce per-phase timeout guards
//   - Call IBridgeServices to trigger bridge actions at phase boundaries
//
// NOT responsible for:
//   - Implementing any phase's work (delegates to managers via IBridgeServices)
//   - Owning component instances or configuration state
//   - Any steady-state work beyond the "running" phase entry
//
// Dependencies:
//   - tiny_hsm
//   - IBridgeServices
// =============================================================================

#ifndef startup_hsm_h
#define startup_hsm_h

#include "i_bridge_services.h"

extern "C" {
#include "tiny_hsm.h"
}

// ============================================================================
// Startup HSM signal identifiers
// ============================================================================

enum {
  signal_run_loop = tiny_hsm_signal_user_start,  // Drive ongoing work in current state
  signal_autodiscovery_complete,                  // Autodiscovery found (or gave up on) appliance
  signal_device_id_complete,                      // Device ID ready (read or pre-configured)
  signal_mqtt_connected,                          // MQTT broker connection established
  signal_feature_bits_complete,                   // All feature bit ERDs read and parsed
  signal_bridge_ready,                            // ERD bridge (poll/subscribe) initialized
  signal_subscription_fallback                   // AUTO mode: subscription timed out, fell back to polling
};

// ============================================================================
// Startup HSM state function declarations & configuration
// ============================================================================

namespace esphome {
namespace geappliances_bridge {
// Wrapper struct that embeds the HSM and holds the bridge services pointer.
// Uses container_of pattern (same as erd_write_bridge_t, erd_bridge_poll_t)
// to recover the wrapper from the HSM pointer in state functions.
typedef struct {
  tiny_hsm_t hsm;
  IBridgeServices* services;
} startup_hsm_wrapper_t;

tiny_hsm_result_t startup_state_top(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_protocol_stack(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

tiny_hsm_result_t startup_state_startup_delay(
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

tiny_hsm_result_t startup_state_running(
  tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
/// Recover the IBridgeServices pointer from the embedded HSM using container_of.
IBridgeServices* services_from_hsm(tiny_hsm_t* hsm);

/// Initialize the startup HSM wrapper with the given bridge services and initial state.
void startup_hsm_wrapper_init(startup_hsm_wrapper_t* self, IBridgeServices* services,
  tiny_hsm_state_t initial);

/// Destroy the startup HSM wrapper (unsubscribes event subscriptions).
void startup_hsm_wrapper_destroy(startup_hsm_wrapper_t* self);

// HSM configuration (state descriptors + hierarchy)
extern const tiny_hsm_configuration_t startup_hsm_configuration;

}  // namespace geappliances_bridge
}  // namespace esphome

#endif
