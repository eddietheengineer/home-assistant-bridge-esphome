#include "erd_state_table.h"
#include <cstring>
#include "esphome/core/log.h"

static const char* const TAG __attribute__((unused)) = "erd_state_table";

namespace esphome {
namespace geappliances_bridge {

ErdStateTable::ErdStateTable()
{
  tiny_event_init(&erd_changed_);
}

size_t ErdStateTable::find_index_(tiny_erd_t erd_id) const
{
  // Binary search on the sorted entries_ vector.
  size_t lo = 0;
  size_t hi = entries_.size();
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (entries_[mid].erd_id < erd_id) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  // lo is the insertion point. Check if the ERD actually exists there.
  if (lo < entries_.size() && entries_[lo].erd_id == erd_id) {
    return lo;
  }
  return entries_.size();  // not found
}

void ErdStateTable::update_erd_value(tiny_erd_t erd_id, const uint8_t* value, uint8_t size)
{
  update_erd_value_internal_(erd_id, value, size, false);
}

void ErdStateTable::update_erd_value_from_subscription(tiny_erd_t erd_id, const uint8_t* value, uint8_t size)
{
  update_erd_value_internal_(erd_id, value, size, true);
}

void ErdStateTable::update_erd_value_internal_(tiny_erd_t erd_id, const uint8_t* value, uint8_t size, bool from_subscription)
{
  // Guard against null value pointer — malformed appliance data could crash the bridge.
  if (value == nullptr) {
    return;
  }

  // Clamp size to MAX_ERD_VALUE_SIZE
  if (size > MAX_ERD_VALUE_SIZE) {
    size = MAX_ERD_VALUE_SIZE;
  }

  size_t idx = find_index_(erd_id);

  if (idx < entries_.size()) {
    // ERD already exists — update in place
    ErdEntry* entry = &entries_[idx];
    bool changed = (entry->value_size != size) ||
                   (std::memcmp(entry->value, value, size) != 0);
    std::memcpy(entry->value, value, size);
    entry->value_size = size;
    // For subscription publications, ALWAYS set publish_flag because the
    // appliance only sends subscription publications when the value has
    // actually changed - we trust the appliance's notification.
    // For polling reads, set publish_flag only if the value actually changed.
    if (from_subscription || changed) {
      entry->publish_flag = true;
      tiny_event_publish(&erd_changed_, &erd_id);
    }
    ESP_LOGD(TAG, "ErdStateTable update 0x%04X: size=%u flag=%d", erd_id, entry->value_size, entry->publish_flag);
  } else {
    // New ERD — insert in sorted order
    if (entries_.size() >= MAX_ERD_ENTRIES) {
      return;  // table full, silently drop
    }
    ErdEntry entry;
    entry.erd_id = erd_id;
    std::memcpy(entry.value, value, size);
    entry.value_size = size;
    entry.publish_flag = true;
    // Insert at idx to maintain sorted order
    entries_.insert(entries_.begin() + static_cast<long>(idx), entry);
    tiny_event_publish(&erd_changed_, &erd_id);
    ESP_LOGD(TAG, "ErdStateTable new 0x%04X: size=%u flag=%d", erd_id, entry.value_size, entry.publish_flag);
  }
}

void ErdStateTable::set_publish_flag(tiny_erd_t erd_id)
{
  size_t idx = find_index_(erd_id);
  if (idx < entries_.size()) {
    entries_[idx].publish_flag = true;
  }
}

std::vector<tiny_erd_t> ErdStateTable::get_flagged_erds() const
{
  std::vector<tiny_erd_t> result;
  for (const auto& entry : entries_) {
    if (entry.publish_flag) {
      result.push_back(entry.erd_id);
    }
  }
  return result;
}

const uint8_t* ErdStateTable::get_erd_value(tiny_erd_t erd_id, uint8_t& size_out) const
{
  size_t idx = find_index_(erd_id);
  if (idx < entries_.size()) {
    size_out = entries_[idx].value_size;
    return entries_[idx].value;
  }
  size_out = 0;
  return nullptr;
}

void ErdStateTable::clear_publish_flag(tiny_erd_t erd_id)
{
  size_t idx = find_index_(erd_id);
  if (idx < entries_.size()) {
    entries_[idx].publish_flag = false;
  }
}

bool ErdStateTable::has_flag(tiny_erd_t erd_id) const
{
  size_t idx = find_index_(erd_id);
  if (idx < entries_.size()) {
    return entries_[idx].publish_flag;
  }
  return false;
}

i_tiny_event_t* ErdStateTable::on_erd_changed()
{
  return &erd_changed_.interface;
}

}  // namespace geappliances_bridge
}  // namespace esphome
