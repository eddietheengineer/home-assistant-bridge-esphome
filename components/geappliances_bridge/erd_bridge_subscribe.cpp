/*!
 * @file
 * @brief ERD subscription bridge implementation.
 *
 * Manages the GEA3 ERD subscription lifecycle: subscribing, retaining the
 * subscription every 30 s, and publishing received ERD values to the ERD
 * cache.  The polling bridge lives in erd_bridge_poll.cpp; shared signals and
 * utility templates are in erd_bridge_common.h.
 */

#include "erd_bridge_common.h"
#include "erd_cache.h"
#include "esphome/core/log.h"

using namespace std;

static const char* const TAG __attribute__((unused)) = "erd_bridge_subscribe";

// ============================================================================
// Subscription bridge
// ============================================================================

static tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_subscribing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

static tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_subscribe_t* self = container_of(erd_bridge_subscribe_t, hsm, hsm);

  switch(signal) {
    case signal_subscription_publication_received: {
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      auto erd = args->subscription_publication_received.erd;

      if(erd_set(self).find(erd) == erd_set(self).end()) {
        erd_set(self).insert(erd);
      }

      erd_cache_update(self->erd_cache, erd,
        reinterpret_cast<const uint8_t*>(args->subscription_publication_received.data),
        args->subscription_publication_received.data_size);
    } break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_subscribing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_subscribe_t* self = container_of(erd_bridge_subscribe_t, hsm, hsm);
  (void)data;

  switch(signal) {
    case signal_subscription_host_came_online:
      // The appliance host restarted — its ERD set may have changed, so clear
      // the local tracking set so that all ERDs are re-registered when new
      // subscription publications arrive.  The ERD cache is NOT cleared — it
      // may be shared with the polling bridge, and stale entries are harmless
      // (they occupy slots but are overwritten when new publications arrive).
      erd_set(self).clear();
      __attribute__((fallthrough));
    case tiny_hsm_signal_entry:
      // Intentionally fall through to the subscribe case below.
      __attribute__((fallthrough));
    case signal_subscription_failed:
    case signal_timer_expired:
      if(!tiny_gea3_erd_client_subscribe(self->erd_client, self->erd_host_address)) {
        arm_timer(self, resubscribe_delay);
      }
      break;

    case signal_subscription_added_or_retained:
      tiny_hsm_transition(hsm, state_subscribed);
      break;

    case tiny_hsm_signal_exit:
      disarm_timer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static void arm_periodic_timer(erd_bridge_subscribe_t* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start_periodic(
    self->timer_group, &self->timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<erd_bridge_subscribe_t*>(context)->hsm, signal_timer_expired, nullptr);
    });
}

static tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_subscribe_t* self = container_of(erd_bridge_subscribe_t, hsm, hsm);
  (void)data;
  (void)self;

  switch(signal) {
    case tiny_hsm_signal_entry:
      arm_periodic_timer(self, subscription_retention_period);
      break;

    case signal_timer_expired:
      tiny_gea3_erd_client_retain_subscription(self->erd_client, self->erd_host_address);
      break;

    case signal_subscription_host_came_online:
      tiny_hsm_transition(hsm, state_subscribing);
      break;

    case tiny_hsm_signal_exit:
      disarm_timer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static const tiny_hsm_state_descriptor_t sub_hsm_state_descriptors[] = {
  { .state = sub_state_top, .parent = nullptr },
  { .state = state_subscribing, .parent = sub_state_top },
  { .state = state_subscribed, .parent = sub_state_top }
};
static const tiny_hsm_configuration_t sub_hsm_configuration = {
  .states = sub_hsm_state_descriptors,
  .state_count = element_count(sub_hsm_state_descriptors)
};

void erd_bridge_subscribe_init(
  erd_bridge_subscribe_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint8_t address,
  erd_cache_t* cache)
{
  self->timer_group = timer_group;
  self->erd_client = erd_client;
  self->erd_host_address = address;
  self->erd_cache = cache;
  self->erd_set = reinterpret_cast<void*>(new set<tiny_erd_t>());

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<erd_bridge_subscribe_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      if(args->address != self->erd_host_address) {
        return;
      }

      switch(args->type) {
        case tiny_gea3_erd_client_activity_type_subscription_added_or_retained:
          tiny_hsm_send_signal(&self->hsm, signal_subscription_added_or_retained, nullptr);
          break;

        case tiny_gea3_erd_client_activity_type_subscription_publication_received:
          tiny_hsm_send_signal(&self->hsm, signal_subscription_publication_received, args);
          break;

        case tiny_gea3_erd_client_activity_type_subscription_host_came_online:
          tiny_hsm_send_signal(&self->hsm, signal_subscription_host_came_online, nullptr);
          break;

        case tiny_gea3_erd_client_activity_type_subscribe_failed:
          tiny_hsm_send_signal(&self->hsm, signal_subscription_failed, nullptr);
          break;
        case tiny_gea3_erd_client_activity_type_write_completed:
        case tiny_gea3_erd_client_activity_type_write_failed:
          break;
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  tiny_hsm_init(&self->hsm, &sub_hsm_configuration, state_subscribing);
}

void erd_bridge_subscribe_destroy(erd_bridge_subscribe_t* self)
{
  // Guard against destroy() being called on a never-initialized struct (e.g.
  // in test teardowns that always call both bridge and polling destroy).
  if (!self->timer_group) {
    return;
  }

  // Stop the resubscribe timer so it cannot fire after the bridge is torn down.
  // tiny_timer_stop() is idempotent: safe to call even if the timer is not active.
  tiny_timer_stop(self->timer_group, &self->timer);

  // Remove all event subscriptions before freeing heap state.
  //
  // erd_bridge_subscribe_init() subscribes one event callback that references
  // this struct: erd_client_activity_subscription.  If it remains registered
  // after destroy(), any subsequent event fires the HSM which dereferences
  // self->erd_set (freed below) — a use-after-free that corrupts the heap.
  tiny_event_unsubscribe(
    tiny_gea3_erd_client_on_activity(self->erd_client),
    &self->erd_client_activity_subscription);

  delete reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
  self->erd_set = nullptr;
}

