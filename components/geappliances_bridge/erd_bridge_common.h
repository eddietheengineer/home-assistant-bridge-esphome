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

#include <string.h>
#include <cstdint>

extern "C" {
#include "tiny_erd.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"
#include "tiny_utils.h"
#include "tiny_gea_constants.h"
}

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
// Fixed-capacity ERD set — replaces std::set<tiny_erd_t> to eliminate heap
// node allocations.  Sorted array with linear search; O(n) lookups and
// inserts (n is small: bounded by probe list or subscription ERDs).
// ============================================================================

#define ERD_SET_CAPACITY 645  // matches POLLING_LIST_MAX_SIZE

typedef struct {
  tiny_erd_t data[ERD_SET_CAPACITY];
  uint16_t count;
} erd_set_t;

static inline void erd_set_init(erd_set_t* self)
{
  self->count = 0;
}

static inline bool erd_set_contains(erd_set_t* self, tiny_erd_t erd)
{
  for (uint16_t i = 0; i < self->count; i++) {
    if (self->data[i] == erd) return true;
    if (self->data[i] > erd) return false;  /* sorted, so not present */
  }
  return false;
}

static inline bool erd_set_insert(erd_set_t* self, tiny_erd_t erd)
{
  if (erd_set_contains(self, erd)) return false;
  if (self->count >= ERD_SET_CAPACITY) return false;
  self->data[self->count++] = erd;
  /* Insertion-sort to keep the array ordered. */
  int j = (int)self->count - 1;
  while (j > 0 && self->data[j - 1] > self->data[j]) {
    tiny_erd_t tmp = self->data[j];
    self->data[j] = self->data[j - 1];
    self->data[j - 1] = tmp;
    j--;
  }
  return true;
}

static inline void erd_set_clear(erd_set_t* self)
{
  self->count = 0;
}

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
static erd_set_t& erd_set(T* self)
{
  return self->erd_set;
}
