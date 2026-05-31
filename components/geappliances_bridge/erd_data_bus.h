/*!
 * @file
 * @brief Central ERD state table for decoupling bridge modules.
 *
 * ErdDataBus stores the most recent value of each registered ERD along with
 * a dirty flag. Modules write updates via write(); the MQTT connection manager
 * drains dirty entries and publishes them. On MQTT reconnect, all entries can
 * be marked dirty again so they are re-published.
 *
 * Modules may also subscribe to per-ERD change notifications so they react to
 * data changes without polling or calling each other directly.
 *
 * Memory model:
 *   - Keys and values are stored in a std::map (stable addresses, O(log n) lookup).
 *   - The per-ERD tiny_event_t subscriber list is stable because std::map nodes
 *     are individually heap-allocated and never moved after insertion.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

extern "C" {
#include "tiny_erd.h"
#include "tiny_event.h"
#include "tiny_event_subscription.h"
}

namespace esphome {
namespace geappliances_bridge {

struct ErdChangedArgs {
  tiny_erd_t erd;
  const void* data;
  uint8_t size;
};

class ErdDataBus {
 public:
  // Write a new value for an ERD. Creates the entry on first write.
  // Marks the entry dirty and fires the per-ERD on_change event.
  void write(tiny_erd_t erd, const void* data, uint8_t size);

  // Read the current value into out_data (must be at least out_size bytes).
  // Returns true if the ERD exists and its stored size equals out_size.
  bool read(tiny_erd_t erd, void* out_data, uint8_t out_size) const;

  // Returns true if an ERD entry exists (has been written at least once).
  bool contains(tiny_erd_t erd) const;

  // Returns the stored data size for an ERD, or 0 if not present.
  uint8_t data_size(tiny_erd_t erd) const;

  // Subscribe to change notifications for a specific ERD.
  // The subscription struct must remain valid for the lifetime of the
  // subscription. Callback receives ErdChangedArgs* as the args pointer.
  void subscribe_change(
    tiny_erd_t erd,
    tiny_event_subscription_t* subscription,
    tiny_event_subscription_callback_t callback,
    void* context);

  // Mark all entries dirty so they will be (re-)published on the next drain.
  // Call this on MQTT reconnect.
  void mark_all_dirty();

  // Returns the number of entries currently marked dirty.
  size_t dirty_count() const;

  // Iterate over every dirty entry, invoking fn(erd, data, size) for each.
  // Does NOT clear the dirty flag — call clear_dirty() after publishing.
  void for_each_dirty(std::function<void(tiny_erd_t, const void*, uint8_t)> fn) const;

  // Clear the dirty flag for a single ERD after it has been published.
  void clear_dirty(tiny_erd_t erd);

 private:
  struct Entry {
    Entry() { tiny_event_init(&on_change); }
    // std::map nodes are stable — no copy or move after insertion.
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;

    std::vector<uint8_t> data{};
    bool dirty{false};
    tiny_event_t on_change{};
  };

  Entry& get_or_create(tiny_erd_t erd);
  std::map<tiny_erd_t, Entry> entries_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
