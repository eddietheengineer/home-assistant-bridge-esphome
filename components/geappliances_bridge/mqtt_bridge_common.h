// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Provide shared timing constants, HSM signal identifiers, and utility
//       templates used by both mqtt_bridge.cpp and mqtt_bridge_polling.cpp.
//
// Responsibilities:
//   - Declare signal enum values shared by both bridge implementations
//   - Define timing constants (retry_delay, resubscribe_delay, etc.)
//   - Provide arm_timer / disarm_timer / erd_set / handle_write_result helpers
//
// NOT responsible for:
//   - Any bridge state or lifecycle logic
//   - Anything not shared between both bridge implementations
//
// Dependencies:
//   - mqtt_bridge.h, mqtt_bridge_polling.h, tiny_utils.h, tiny_gea_constants.h
// =============================================================================

#pragma once

/*!
 * @file
 * @brief Shared signals, timing constants, and utility templates used by both
 *        the subscription bridge (mqtt_bridge.cpp) and the polling bridge
 *        (mqtt_bridge_polling.cpp).
 *
 * All functions are either template functions (implicitly inline) or declared
 * `inline` so that each translation unit gets its own copy without ODR violations.
 */

extern "C" {
#include "mqtt_bridge.h"
#include "mqtt_bridge_polling.h"
#include "tiny_utils.h"
#include "tiny_gea_constants.h"
}

#include <set>

// ============================================================================
// Shared timing constants
// ============================================================================

enum {
  resubscribe_delay = 1000,
  subscription_retention_period = 30 * 1000,
  retry_delay = 100,
  appliance_lost_timeout = 60000
};

// ============================================================================
// Shared signal identifiers
// ============================================================================

enum {
  signal_start = tiny_hsm_signal_user_start,
  signal_timer_expired,
  signal_polling_timer_expired,
  signal_subscription_failed,
  signal_subscription_added_or_retained,
  signal_subscription_host_came_online,
  signal_subscription_publication_received,
  signal_read_failed,
  signal_read_completed,
  signal_mqtt_disconnected,
  signal_appliance_lost,
  signal_write_requested
};

// ============================================================================
// Shared utility templates
// ============================================================================

template<typename T>
static void arm_timer(T* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start(
    self->timer_group, &self->timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<T*>(context)->hsm, signal_timer_expired, nullptr);
    });
}

template<typename T>
static void disarm_timer(T* self)
{
  tiny_timer_stop(self->timer_group, &self->timer);
}

template<typename T>
static std::set<tiny_erd_t>& erd_set(T* self)
{
  return *reinterpret_cast<std::set<tiny_erd_t>*>(self->erd_set);
}

static inline void handle_write_result(
  i_mqtt_client_t* mqtt_client,
  const tiny_gea3_erd_client_on_activity_args_t* args)
{
  if (args->type == tiny_gea3_erd_client_activity_type_write_completed) {
    mqtt_client_update_erd_write_result(mqtt_client, args->write_completed.erd, true, 0);
  } else if (args->type == tiny_gea3_erd_client_activity_type_write_failed) {
    mqtt_client_update_erd_write_result(mqtt_client, args->write_failed.erd, false, args->write_failed.reason);
  }
}

template<typename T>
static void setup_write_request_subscription(T* self, i_mqtt_client_t* mqtt_client)
{
  tiny_event_subscription_init(
    &self->mqtt_write_request_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<T*>(context);
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(_args);
      tiny_hsm_send_signal(&self->hsm, signal_write_requested, args);
    });
  tiny_event_subscribe(mqtt_client_on_write_request(mqtt_client), &self->mqtt_write_request_subscription);
}

template<typename T>
static void setup_disconnect_subscription(T* self, i_mqtt_client_t* mqtt_client)
{
  tiny_event_subscription_init(
    &self->mqtt_disconnect_subscription, self, +[](void* context, const void*) {
      auto self = reinterpret_cast<T*>(context);
      // Do NOT clear erd_set here. Clearing it on every transient MQTT
      // reconnect causes two problems:
      //   1. Heap fragmentation: N std::set tree nodes freed and reallocated
      //      per reconnect cycle.
      //   2. Growing polling list (api_parsed mode): state_polling re-adds
      //      all N api_parsed ERDs on each reconnect (since erd_set is empty),
      //      growing polling_list_count by N every time.
      // erd_set is cleared in state_add_common_erds (full-discovery path only),
      // which is the correct time to reset the polling list.
      tiny_hsm_send_signal(&self->hsm, signal_mqtt_disconnected, nullptr);
    });
  tiny_event_subscribe(mqtt_client_on_mqtt_disconnect(mqtt_client), &self->mqtt_disconnect_subscription);
}
