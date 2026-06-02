#include "erd_state_table.h"
#include <cstring>
#include "esphome/core/log.h"

static const char* const TAG __attribute__((unused)) = "erd_state_table";

namespace esphome {
namespace geappliances_bridge {

ErdStateTable::ErdStateTable()
    : entry_count_(0)
{
  for (size_t i = 0; i < 65536; i++) {
    index_table_[i] = 0xFFFF;
  }
  tiny_event_init(&erd_changed_);
}

void ErdStateTable::update_erd_value(tiny_erd_t erd_id, const uint8_t* value, uint8_t size)
{
  // Guard against null value pointer — malformed appliance data could crash the bridge.
  if (value == nullptr) {
    return;
  }

  // Clamp size to MAX_ERD_VALUE_SIZE
  if (size > MAX_ERD_VALUE_SIZE) {
    size = MAX_ERD_VALUE_SIZE;
  }

  uint16_t idx = index_table_[erd_id];

  if (idx != 0xFFFF) {
    // ERD already exists — update in place
    ErdEntry* entry = &entries_[idx];
    bool changed = (entry->value_size != size) ||
                   (std::memcmp(entry->value, value, size) != 0);
    std::memcpy(entry->value, value, size);
    entry->value_size = size;
    if (changed) {
      entry->publish_flag = true;
      tiny_event_publish(&erd_changed_, &erd_id);
    }
    ESP_LOGD(TAG, "ErdStateTable update 0x%04X: size=%u flag=%d", erd_id, entry->value_size, entry->publish_flag);
  } else {
    // New ERD — add to flat array
    if (entry_count_ >= MAX_ERD_ENTRIES) {
      return;  // table full, silently drop
    }
    size_t new_idx = entry_count_;
    ErdEntry* entry = &entries_[new_idx];
    entry->erd_id = erd_id;
    std::memcpy(entry->value, value, size);
    entry->value_size = size;
    entry->publish_flag = true;
    index_table_[erd_id] = static_cast<uint16_t>(new_idx);
    entry_count_++;
    tiny_event_publish(&erd_changed_, &erd_id);
    ESP_LOGD(TAG, "ErdStateTable new 0x%04X: size=%u flag=%d", erd_id, entry->value_size, entry->publish_flag);
  }
}

void ErdStateTable::set_publish_flag(tiny_erd_t erd_id)
{
  uint16_t idx = index_table_[erd_id];
  if (idx != 0xFFFF) {
    entries_[idx].publish_flag = true;
  }
}

std::vector<tiny_erd_t> ErdStateTable::get_flagged_erds() const
{
  std::vector<tiny_erd_t> result;
  for (size_t i = 0; i < entry_count_; i++) {
    if (entries_[i].publish_flag) {
      result.push_back(entries_[i].erd_id);
    }
  }
  return result;
}

const uint8_t* ErdStateTable::get_erd_value(tiny_erd_t erd_id, uint8_t& size_out) const
{
  uint16_t idx = index_table_[erd_id];
  if (idx != 0xFFFF) {
    size_out = entries_[idx].value_size;
    return entries_[idx].value;
  }
  size_out = 0;
  return nullptr;
}

void ErdStateTable::clear_publish_flag(tiny_erd_t erd_id)
{
  uint16_t idx = index_table_[erd_id];
  if (idx != 0xFFFF) {
    entries_[idx].publish_flag = false;
  }
}

bool ErdStateTable::has_flag(tiny_erd_t erd_id) const
{
  uint16_t idx = index_table_[erd_id];
  if (idx != 0xFFFF) {
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
