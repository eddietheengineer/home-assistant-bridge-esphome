#include "appliance_side_state_machine.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

ApplianceSideStateMachine::ApplianceSideStateMachine(ErdStateTable* state_table,
                                                     GlobalStateRegistry* registry,
                                                     WriteQueue* write_queue,
                                                     i_tiny_gea3_erd_client_t* erd_client)
    : state_table_(state_table),
      registry_(registry),
      write_queue_(write_queue),
      erd_client_(erd_client),
      current_state_(ApplianceSideState::IDLE),
      config_(),
      subscription_handler_(nullptr),
      polling_handler_(nullptr),
      write_handler_(nullptr),
      appliance_address_sub_(),
      timer_group_(nullptr),
      error_retry_timer_(),
      error_retry_timer_running_(false)
{
  // Handlers are created and owned by the bridge (GeappliancesBridge).
  // The FSM delegates to them via set_handlers().
  // This avoids duplicate handler instances and the s_active_handler static
  // pointer race between bridge-owned and FSM-owned handlers.

  // Subscribe to appliance address changes
  tiny_event_subscription_init(&appliance_address_sub_, this,
                               [](void* ctx, const void* args) {
                                 reinterpret_cast<ApplianceSideStateMachine*>(ctx)
                                   ->on_appliance_address_changed(ctx, args);
                               });
  tiny_event_subscribe(registry_->on_appliance_address_changed(),
                       &appliance_address_sub_);
}

ApplianceSideStateMachine::~ApplianceSideStateMachine()
{
  tiny_event_unsubscribe(registry_->on_appliance_address_changed(),
                         &appliance_address_sub_);
  // Handlers are owned by the bridge, not the FSM.
}

void ApplianceSideStateMachine::set_config(const ApplianceSideConfig& config)
{
  config_ = config;

  // Apply polling config to the bridge-owned handler
  if (config_.enable_polling && polling_handler_ != nullptr) {
    for (tiny_erd_t erd : config_.polling_erds) {
      polling_handler_->add_erd_to_poll(erd);
    }
    polling_handler_->set_polling_interval(config_.polling_interval_ms);
  }
}

void ApplianceSideStateMachine::set_timer_group(tiny_timer_group_t* timer_group)
{
  timer_group_ = timer_group;
}

void ApplianceSideStateMachine::set_handlers(SubscriptionHandler* sub,
                                              PollingHandler* poll,
                                              WriteHandler* write)
{
  subscription_handler_ = sub;
  polling_handler_ = poll;
  write_handler_ = write;
}

void ApplianceSideStateMachine::loop()
{
  switch (current_state_) {
    case ApplianceSideState::IDLE:
      // Check if appliance has been discovered
      if (registry_->get_appliance_address() != 0) {
        transition_to(ApplianceSideState::RUNNING);
      }
      break;

    case ApplianceSideState::RUNNING:
      // Process writes
      if (write_handler_ != nullptr) {
        write_handler_->process_writes();
      }
      break;

    case ApplianceSideState::ERROR:
      // Wait for retry timer — timer callback handles transition
      break;
  }
}

ApplianceSideState ApplianceSideStateMachine::get_current_state() const
{
  return current_state_;
}

SubscriptionHandler* ApplianceSideStateMachine::get_subscription_handler()
{
  return subscription_handler_;
}

PollingHandler* ApplianceSideStateMachine::get_polling_handler()
{
  return polling_handler_;
}

WriteHandler* ApplianceSideStateMachine::get_write_handler()
{
  return write_handler_;
}

void ApplianceSideStateMachine::on_appliance_address_changed(void* context,
                                                             const void* args)
{
  (void)context;
  (void)args;

  if (current_state_ == ApplianceSideState::IDLE) {
    uint8_t addr = registry_->get_appliance_address();
    if (addr != 0) {
      transition_to(ApplianceSideState::RUNNING);
    }
  } else if (current_state_ == ApplianceSideState::ERROR) {
    // Appliance address changed while in error — retry immediately
    if (error_retry_timer_running_ && timer_group_) {
      tiny_timer_stop(timer_group_, &error_retry_timer_);
      error_retry_timer_running_ = false;
    }
    transition_to(ApplianceSideState::RUNNING);
  }
}

void ApplianceSideStateMachine::on_error_retry_timer(void* context)
{
  auto self = reinterpret_cast<ApplianceSideStateMachine*>(context);
  self->error_retry_timer_running_ = false;
  if (self->current_state_ == ApplianceSideState::ERROR) {
    self->transition_to(ApplianceSideState::RUNNING);
  }
}

void ApplianceSideStateMachine::transition_to(ApplianceSideState next)
{
  if (next == current_state_) {
    return;
  }

  current_state_ = next;

  switch (next) {
    case ApplianceSideState::RUNNING:
      start_handlers();
      break;

    case ApplianceSideState::ERROR:
      stop_handlers();
      // Start retry timer
      if (timer_group_) {
        tiny_timer_start(timer_group_, &error_retry_timer_,
                         ERROR_RETRY_DELAY_MS, this,
                         [](void* ctx) {
                           reinterpret_cast<ApplianceSideStateMachine*>(ctx)
                             ->on_error_retry_timer(ctx);
                         });
        error_retry_timer_running_ = true;
      }
      break;

    case ApplianceSideState::IDLE:
      stop_handlers();
      break;
  }
}

void ApplianceSideStateMachine::start_handlers()
{
  uint8_t addr = registry_->get_appliance_address();
  if (write_handler_ != nullptr) {
    write_handler_->set_appliance_address(addr);
  }

  if (config_.enable_subscriptions && subscription_handler_ != nullptr) {
    subscription_handler_->start(addr);
  }

  if (config_.enable_polling && timer_group_ != nullptr && polling_handler_ != nullptr) {
    polling_handler_->start(timer_group_, addr);
  }
}

void ApplianceSideStateMachine::stop_handlers()
{
  if (subscription_handler_ != nullptr) {
    subscription_handler_->stop();
  }
  if (timer_group_ != nullptr && polling_handler_ != nullptr) {
    polling_handler_->stop(timer_group_);
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
