

#ifndef startup_hsm_h
#define startup_hsm_h

#include "i_bridge_services.h"

extern "C" {
#include "tiny_hsm.h"
}

enum {
  signal_run_loop = tiny_hsm_signal_user_start,
  signal_autodiscovery_complete,
  signal_device_id_complete,
  signal_mqtt_connected,
  signal_feature_bits_complete,
  signal_bridge_ready,
  signal_subscription_fallback
};

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
