/*!
 * @file
 * @brief Central ERD state table implementation.
 */

#include "erd_data_bus.h"

namespace esphome {
namespace geappliances_bridge {

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

ErdDataBus::Entry& ErdDataBus::get_or_create(tiny_erd_t erd)
{
  auto it = entries_.find(erd);
  if (it == entries_.end()) {
    auto result = entries_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(erd),
      std::forward_as_tuple());
    return result.first->second;
  }
  return it->second;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ErdDataBus::write(tiny_erd_t erd, const void* data, uint8_t size)
{
  Entry& entry = get_or_create(erd);
  entry.data.assign(
    static_cast<const uint8_t*>(data),
    static_cast<const uint8_t*>(data) + size);
  entry.dirty = true;

  ErdChangedArgs args{erd, entry.data.data(), size};
  tiny_event_publish(&entry.on_change, &args);
}

bool ErdDataBus::read(tiny_erd_t erd, void* out_data, uint8_t out_size) const
{
  auto it = entries_.find(erd);
  if (it == entries_.end()) {
    return false;
  }
  if (it->second.data.size() != static_cast<size_t>(out_size)) {
    return false;
  }
  std::memcpy(out_data, it->second.data.data(), out_size);
  return true;
}

bool ErdDataBus::contains(tiny_erd_t erd) const
{
  return entries_.count(erd) > 0;
}

uint8_t ErdDataBus::data_size(tiny_erd_t erd) const
{
  auto it = entries_.find(erd);
  if (it == entries_.end()) {
    return 0;
  }
  return static_cast<uint8_t>(it->second.data.size());
}

void ErdDataBus::subscribe_change(
  tiny_erd_t erd,
  tiny_event_subscription_t* subscription,
  tiny_event_subscription_callback_t callback,
  void* context)
{
  Entry& entry = get_or_create(erd);
  tiny_event_subscription_init(subscription, context, callback);
  tiny_event_subscribe(&entry.on_change.interface, subscription);
}

void ErdDataBus::mark_all_dirty()
{
  for (auto& kv : entries_) {
    kv.second.dirty = true;
  }
}

size_t ErdDataBus::dirty_count() const
{
  size_t count = 0;
  for (const auto& kv : entries_) {
    if (kv.second.dirty) {
      count++;
    }
  }
  return count;
}

void ErdDataBus::for_each_dirty(
  std::function<void(tiny_erd_t, const void*, uint8_t)> fn) const
{
  for (const auto& kv : entries_) {
    if (kv.second.dirty) {
      fn(kv.first,
         kv.second.data.data(),
         static_cast<uint8_t>(kv.second.data.size()));
    }
  }
}

void ErdDataBus::clear_dirty(tiny_erd_t erd)
{
  auto it = entries_.find(erd);
  if (it != entries_.end()) {
    it->second.dirty = false;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
