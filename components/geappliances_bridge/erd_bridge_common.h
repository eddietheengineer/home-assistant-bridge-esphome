// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Provide shared timing constants, HSM signal identifiers, and utility
//       templates used by both erd_bridge_subscribe.cpp and erd_bridge_poll.cpp.
//
// Responsibilities:
//   - Declare signal enum values shared by both bridge implementations
//   - Define timing constants (retry_delay, resubscribe_delay, etc.)
//   - Provide arm_timer / disarm_timer / erd_set helpers
//
// NOT responsible for:
//   - Any bridge state or lifecycle logic
//   - Anything not shared between both bridge implementations
//   - Any MQTT interaction (bridges write to erd_cache only)
//
// Dependencies:
//   - erd_bridge_subscribe.h, erd_bridge_poll.h, tiny_utils.h, tiny_gea_constants.h
// =============================================================================

#pragma once

/*!
 * @file
 * @brief Shared signals, timing constants, and utility templates used by both
 *        the subscription bridge (erd_bridge_subscribe.cpp) and the polling bridge
 *        (erd_bridge_poll.cpp).
 *
 * All functions are either template functions (implicitly inline) or declared
 * `inline` so that each translation unit gets its own copy without ODR violations.
 */

extern "C" {
#include "erd_bridge_subscribe.h"
#include "erd_bridge_poll.h"
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
  signal_appliance_lost
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

