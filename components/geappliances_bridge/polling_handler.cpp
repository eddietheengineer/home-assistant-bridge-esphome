#include "polling_handler.h"
#include <algorithm>
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

PollingHandler::PollingHandler(i_tiny_gea3_erd_client_t* erd_client,
                               ErdStateTable* state_table,
                               uint32_t polling_interval_ms,
                               bool only_publish_on_change)
    : erd_client_(erd_client),
      state_table_(state_table),
      polling_interval_ms_(polling_interval_ms),
      only_publish_on_change_(only_publish_on_change),
      timer_group_(nullptr),
      address_(0),
      timer_running_(false)
{
  if (erd_client_ != nullptr) {
    tiny_event_subscription_init(&erd_client_activity_sub_, this,
                                 [](void* ctx, const void* args) {
                                   reinterpret_cast<PollingHandler*>(ctx)
                                     ->on_erd_client_activity_(ctx, args);
                                 });
    tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client_),
                         &erd_client_activity_sub_);
  }
}

PollingHandler::~PollingHandler()
{
  if (erd_client_ != nullptr) {
    tiny_event_unsubscribe(tiny_gea3_erd_client_on_activity(erd_client_),
                           &erd_client_activity_sub_);
  }
}

void PollingHandler::add_erd_to_poll(tiny_erd_t erd_id)
{
  if (std::find(polling_erds_.begin(), polling_erds_.end(), erd_id)
      == polling_erds_.end()) {
    polling_erds_.push_back(erd_id);
  }
}

void PollingHandler::remove_erd_from_poll(tiny_erd_t erd_id)
{
  polling_erds_.erase(
    std::remove(polling_erds_.begin(), polling_erds_.end(), erd_id),
    polling_erds_.end());
}

void PollingHandler::set_polling_interval(uint32_t ms)
{
  polling_interval_ms_ = ms;
  if (timer_running_ && timer_group_) {
    tiny_timer_stop(timer_group_, &poll_timer_);
    timer_running_ = false;
    tiny_timer_start_periodic(timer_group_, &poll_timer_,
                              polling_interval_ms_, this,
                              [](void* ctx) {
                                reinterpret_cast<PollingHandler*>(ctx)->on_poll_timer_(ctx);
                              });
    timer_running_ = true;
  }
}

void PollingHandler::poll_now(uint8_t address)
{
  if (erd_client_ == nullptr || polling_erds_.empty()) {
    return;
  }
  for (tiny_erd_t erd : polling_erds_) {
    tiny_gea3_erd_client_request_id_t request_id;
    tiny_gea3_erd_client_read(erd_client_, &request_id, address, erd);
  }
}

void PollingHandler::handle_poll_response(tiny_erd_t erd_id,
                                          const uint8_t* value,
                                          uint8_t size)
{
  if (state_table_ == nullptr) {
    return;
  }
  state_table_->update_erd_value(erd_id, value, size);
  if (!only_publish_on_change_) {
    state_table_->set_publish_flag(erd_id);
  }
}

void PollingHandler::handle_poll_failure(tiny_erd_t erd_id)
{
  // No-op — the FSM handles error transitions
  (void)erd_id;
}

void PollingHandler::start(tiny_timer_group_t* timer_group, uint8_t address)
{
  timer_group_ = timer_group;
  address_ = address;
  if (!polling_erds_.empty() && !timer_running_) {
    tiny_timer_start_periodic(timer_group_, &poll_timer_,
                              polling_interval_ms_, this,
                              [](void* ctx) {
                                reinterpret_cast<PollingHandler*>(ctx)->on_poll_timer_(ctx);
                              });
    timer_running_ = true;
  }
}

void PollingHandler::stop(tiny_timer_group_t* timer_group)
{
  if (timer_running_ && timer_group) {
    tiny_timer_stop(timer_group, &poll_timer_);
  }
  timer_group_ = nullptr;
  timer_running_ = false;
}

bool PollingHandler::is_running() const
{
  return timer_running_;
}

uint32_t PollingHandler::get_polling_interval() const
{
  return polling_interval_ms_;
}

bool PollingHandler::get_only_publish_on_change() const
{
  return only_publish_on_change_;
}

const std::vector<tiny_erd_t>& PollingHandler::get_polling_erds() const
{
  return polling_erds_;
}

void PollingHandler::on_poll_timer_(void* context)
{
  PollingHandler* self = reinterpret_cast<PollingHandler*>(context);
  self->poll_now(self->address_);
}

void PollingHandler::on_erd_client_activity_(void* context, const void* args)
{
  PollingHandler* self = reinterpret_cast<PollingHandler*>(context);
  const auto* a = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);

  if (a->address != self->address_) {
    return;
  }

  switch (a->type) {
    case tiny_gea3_erd_client_activity_type_read_completed:
      self->handle_poll_response(a->read_completed.erd,
                                 reinterpret_cast<const uint8_t*>(a->read_completed.data),
                                 a->read_completed.data_size);
      break;

    case tiny_gea3_erd_client_activity_type_read_failed:
      self->handle_poll_failure(a->read_failed.erd);
      break;

    default:
      break;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
