#include "subscription_handler.h"
#include <cstring>
#include <cstddef>
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

// Pointer to the currently active SubscriptionHandler instance.
// Set in start() and used by the HSM state functions to avoid UB
// container_of pointer arithmetic on non-standard-layout types.
static SubscriptionHandler* s_active_handler = nullptr;

// HSM state descriptors
extern "C" {
#include "tiny_utils.h"
}

tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
tiny_hsm_result_t state_subscribing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

static const tiny_hsm_state_descriptor_t sub_hsm_states[] = {
  { .state = sub_state_top, .parent = nullptr },
  { .state = state_subscribing, .parent = sub_state_top },
  { .state = state_subscribed, .parent = sub_state_top },
};
static const tiny_hsm_configuration_t sub_hsm_config = {
  .states = sub_hsm_states,
  .state_count = sizeof(sub_hsm_states) / sizeof(sub_hsm_states[0]),
};

// HSM signal IDs (namespace scope for friend functions)
static const tiny_hsm_signal_t signal_subscription_added_or_retained = tiny_hsm_signal_user_start;
static const tiny_hsm_signal_t signal_subscription_failed = tiny_hsm_signal_user_start + 1;
static const tiny_hsm_signal_t signal_subscription_publication_received = tiny_hsm_signal_user_start + 2;
static const tiny_hsm_signal_t signal_subscription_host_came_online = tiny_hsm_signal_user_start + 3;
static const tiny_hsm_signal_t signal_timer_expired = tiny_hsm_signal_user_start + 4;
static const tiny_hsm_signal_t signal_read_completed = tiny_hsm_signal_user_start + 5;
static const tiny_hsm_signal_t signal_read_failed = tiny_hsm_signal_user_start + 6;

// Static const member definitions (required for ODR)
const tiny_hsm_signal_t SubscriptionHandler::signal_subscription_added_or_retained = signal_subscription_added_or_retained;
const tiny_hsm_signal_t SubscriptionHandler::signal_subscription_failed = signal_subscription_failed;
const tiny_hsm_signal_t SubscriptionHandler::signal_subscription_publication_received = signal_subscription_publication_received;
const tiny_hsm_signal_t SubscriptionHandler::signal_subscription_host_came_online = signal_subscription_host_came_online;

SubscriptionHandler::SubscriptionHandler(i_tiny_gea3_erd_client_t* erd_client,
                                         ErdStateTable* state_table,
                                         tiny_timer_group_t* timer_group)
    : erd_client_(erd_client),
      state_table_(state_table),
      timer_group_(timer_group),
      address_(0)
{
  if (erd_client_ != nullptr) {
    // Subscribe to ERD client activity events
    tiny_event_subscription_init(&erd_client_activity_sub_, this,
                                 [](void* ctx, const void* args) {
                                   reinterpret_cast<SubscriptionHandler*>(ctx)
                                     ->on_erd_client_activity_(ctx, args);
                                 });
    tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client_),
                         &erd_client_activity_sub_);
  }
}

SubscriptionHandler::~SubscriptionHandler()
{
  if (erd_client_ != nullptr) {
    tiny_event_unsubscribe(tiny_gea3_erd_client_on_activity(erd_client_),
                           &erd_client_activity_sub_);
  }
}

void SubscriptionHandler::start(uint8_t address, const std::vector<tiny_erd_t>& valid_erds)
{
  address_ = address;
  known_erds_.clear();
  valid_erds_to_read_ = valid_erds;
  initial_read_index_ = 0;
  initial_read_pending_ = false;
  s_active_handler = this;
  tiny_hsm_init(&hsm_, &sub_hsm_config, state_subscribing);
}

void SubscriptionHandler::stop()
{
  s_active_handler = nullptr;
  tiny_timer_stop(timer_group_, &timer_);
}

void SubscriptionHandler::loop()
{
  // Non-blocking — HSM is driven by event callbacks, not polling
}

bool SubscriptionHandler::is_known_erd(tiny_erd_t erd_id) const
{
  return known_erds_.count(erd_id) > 0;
}

// ---------------------------------------------------------------------------
// ERD client activity callback
// ---------------------------------------------------------------------------

void SubscriptionHandler::on_erd_client_activity_(void* context, const void* args)
{
  SubscriptionHandler* self = reinterpret_cast<SubscriptionHandler*>(context);
  const auto* a = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);

  // Filter by address — ignore responses for other appliances
  if (a->address != self->address_) {
    return;
  }

  switch (a->type) {
    case tiny_gea3_erd_client_activity_type_subscription_added_or_retained:
      tiny_hsm_send_signal(&self->hsm_, signal_subscription_added_or_retained, nullptr);
      break;

    case tiny_gea3_erd_client_activity_type_subscribe_failed:
      tiny_hsm_send_signal(&self->hsm_, signal_subscription_failed, nullptr);
      break;

    case tiny_gea3_erd_client_activity_type_subscription_publication_received:
      tiny_hsm_send_signal(&self->hsm_, signal_subscription_publication_received, args);
      break;

    case tiny_gea3_erd_client_activity_type_subscription_host_came_online:
      tiny_hsm_send_signal(&self->hsm_, signal_subscription_host_came_online, nullptr);
      break;

    case tiny_gea3_erd_client_activity_type_read_completed:
      tiny_hsm_send_signal(&self->hsm_, signal_read_completed, args);
      break;

    case tiny_gea3_erd_client_activity_type_read_failed:
      tiny_hsm_send_signal(&self->hsm_, signal_read_failed, args);
      break;

    default:
      break;
  }
}

void SubscriptionHandler::on_resubscribe_timer_(void* context)
{
  SubscriptionHandler* self = reinterpret_cast<SubscriptionHandler*>(context);
  tiny_hsm_send_signal(&self->hsm_, signal_timer_expired, nullptr);
}

void SubscriptionHandler::on_retention_timer_(void* context)
{
  SubscriptionHandler* self = reinterpret_cast<SubscriptionHandler*>(context);
  tiny_hsm_send_signal(&self->hsm_, signal_timer_expired, nullptr);
}

// ---------------------------------------------------------------------------
// HSM states
// ---------------------------------------------------------------------------

tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)hsm;
  SubscriptionHandler* self = s_active_handler;

  switch (signal) {
    case signal_subscription_publication_received: {
      const auto* args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      tiny_erd_t erd = args->subscription_publication_received.erd;

      // Track known ERDs
      self->known_erds_.insert(erd);

      // Write to state table — sets publish_flag only if value changed
      self->state_table_->update_erd_value(
        erd,
        reinterpret_cast<const uint8_t*>(args->subscription_publication_received.data),
        args->subscription_publication_received.data_size);
    } break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

tiny_hsm_result_t state_subscribing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)hsm;
  SubscriptionHandler* self = s_active_handler;
  (void)data;

  switch (signal) {
    case signal_subscription_host_came_online:
      // Appliance restarted — clear known ERDs so they're re-registered
      self->known_erds_.clear();
      __attribute__((fallthrough));
    case tiny_hsm_signal_entry:
    case signal_subscription_failed:
    case signal_timer_expired:
      if (!tiny_gea3_erd_client_subscribe(self->erd_client_, self->address_)) {
        // Queue full — retry after delay
        tiny_timer_start(self->timer_group_, &self->timer_, SUB_RESUBSCRIBE_DELAY_MS, self,
                         SubscriptionHandler::on_resubscribe_timer_);
      }
      break;

    case signal_subscription_added_or_retained:
      tiny_hsm_transition(hsm, state_subscribed);
      break;

    case tiny_hsm_signal_exit:
      tiny_timer_stop(self->timer_group_, &self->timer_);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  (void)hsm;
  SubscriptionHandler* self = s_active_handler;

  switch (signal) {
    case tiny_hsm_signal_entry:
      // If there are valid ERDs to read initially, start reading them one by one.
      // This populates ErdStateTable with current values so they're published
      // to MQTT even if the appliance never sends subscription updates for them.
      if (!self->valid_erds_to_read_.empty() && self->initial_read_index_ == 0) {
        self->initial_read_pending_ = true;
        tiny_erd_t erd = self->valid_erds_to_read_[self->initial_read_index_];
        tiny_gea3_erd_client_request_id_t rid;
        (void)tiny_gea3_erd_client_read(self->erd_client_, &rid, self->address_, erd);
      } else {
        // No initial reads needed — arm periodic retention timer immediately
        tiny_timer_start_periodic(self->timer_group_, &self->timer_, SUB_RETENTION_PERIOD_MS, self,
                                  SubscriptionHandler::on_retention_timer_);
      }
      break;

    case signal_read_completed: {
      if (!self->initial_read_pending_) {
        return tiny_hsm_result_signal_deferred;
      }
      const auto* args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
      // Write to state table — sets publish_flag so MqttSideStateMachine publishes it
      self->state_table_->update_erd_value(
        args->read_completed.erd,
        reinterpret_cast<const uint8_t*>(args->read_completed.data),
        args->read_completed.data_size);
      self->known_erds_.insert(args->read_completed.erd);
      self->initial_read_index_++;
      // Continue reading next ERD
      if (self->initial_read_index_ < self->valid_erds_to_read_.size()) {
        tiny_erd_t erd = self->valid_erds_to_read_[self->initial_read_index_];
        tiny_gea3_erd_client_request_id_t rid;
        (void)tiny_gea3_erd_client_read(self->erd_client_, &rid, self->address_, erd);
      } else {
        // All initial reads complete — arm retention timer and enter steady state
        self->initial_read_pending_ = false;
        tiny_timer_start_periodic(self->timer_group_, &self->timer_, SUB_RETENTION_PERIOD_MS, self,
                                  SubscriptionHandler::on_retention_timer_);
      }
    } break;

    case signal_read_failed: {
      if (!self->initial_read_pending_) {
        return tiny_hsm_result_signal_deferred;
      }
      // Skip this ERD and continue with the next one
      self->initial_read_index_++;
      if (self->initial_read_index_ < self->valid_erds_to_read_.size()) {
        tiny_erd_t erd = self->valid_erds_to_read_[self->initial_read_index_];
        tiny_gea3_erd_client_request_id_t rid;
        (void)tiny_gea3_erd_client_read(self->erd_client_, &rid, self->address_, erd);
      } else {
        // All initial reads complete (some may have failed) — arm retention timer
        self->initial_read_pending_ = false;
        tiny_timer_start_periodic(self->timer_group_, &self->timer_, SUB_RETENTION_PERIOD_MS, self,
                                  SubscriptionHandler::on_retention_timer_);
      }
    } break;

    case signal_timer_expired:
      tiny_gea3_erd_client_retain_subscription(self->erd_client_, self->address_);
      break;

    case signal_subscription_host_came_online:
    case signal_subscription_failed:
      tiny_hsm_transition(hsm, state_subscribing);
      break;

    case tiny_hsm_signal_exit:
      tiny_timer_stop(self->timer_group_, &self->timer_);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

}  // namespace geappliances_bridge
}  // namespace esphome
