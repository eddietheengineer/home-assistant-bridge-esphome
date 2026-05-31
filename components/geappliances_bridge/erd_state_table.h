// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Store current ERD values and publish flags. This is the core shared
//       data structure between appliance-side and MQTT-side. ErdStateTable is
//       runtime state only — ErdRegistry is metadata only (valid ERD set,
//       string-typed ERDs, registered ERDs). They do not structurally integrate.
//
// Responsibilities:
//   - Store ERD values in fixed-size inline buffers (no per-ERD heap allocation)
//   - Track publish_flag per ERD (change tracking + reconnect recovery)
//   - Provide O(1) lookup via index table into flat entry array
//   - Fire on_erd_changed event when value differs from stored
//
// NOT responsible for:
//   - Validating whether an ERD is "real" (ErdRegistry does that)
//   - Publishing MQTT messages (EsphomeMqttClientAdapter does that)
//   - Any bridge lifecycle management
//
// Dependencies:
//   - geappliances_bridge_constants.h (MAX_ERD_VALUE_SIZE, MAX_ERD_ENTRIES)
//   - tiny_erd.h (tiny_erd_t type)
//   - i_tiny_event.h / tiny_event.h (event subscription pattern)
// =============================================================================

#pragma once

#include <cstdint>
#include <vector>

extern "C" {
#include "tiny_erd.h"
#include "i_tiny_event.h"
#include "tiny_event.h"
}

#include "geappliances_bridge_constants.h"

namespace esphome {
namespace geappliances_bridge {

struct ErdEntry {
  tiny_erd_t erd_id;
  uint8_t value[MAX_ERD_VALUE_SIZE];  // raw ERD data; fixed-size to avoid heap alloc
  uint8_t value_size;
  bool publish_flag;  // true = needs MQTT publish; persists while MQTT disconnected
};

class ErdStateTable {
 public:
  ErdStateTable();

  // -----------------------------------------------------------------------
  // Write operations (called by appliance-side handlers)
  // -----------------------------------------------------------------------

  /// Store value; set publish_flag=true ONLY if value differs from stored.
  /// Fires on_erd_changed event.
  /// Used by: SubscriptionHandler, PollingHandler (only_publish_on_change=true)
  void update_erd_value(tiny_erd_t erd_id, const uint8_t* value, uint8_t size);

  /// Unconditionally set publish_flag=true without changing the stored value.
  /// Used by: PollingHandler when only_publish_on_change=false (call after update_erd_value)
  void set_publish_flag(tiny_erd_t erd_id);

  // -----------------------------------------------------------------------
  // Read operations (called by MQTT-side FSM)
  // -----------------------------------------------------------------------

  /// Returns all ERDs with publish_flag==true. Allocates a std::vector on each
  /// call — acceptable at this scale (max 300 entries, 2 bytes each = 600 bytes,
  /// called once per MQTT publish cycle, not in a tight loop).
  std::vector<tiny_erd_t> get_flagged_erds() const;

  /// Returns pointer to stored value and its size; nullptr if ERD not found.
  const uint8_t* get_erd_value(tiny_erd_t erd_id, uint8_t& size_out) const;

  /// Called immediately after publish() — synchronous, no broker ACK needed.
  void clear_publish_flag(tiny_erd_t erd_id);

  // -----------------------------------------------------------------------
  // Query
  // -----------------------------------------------------------------------

  bool has_flag(tiny_erd_t erd_id) const;

  // -----------------------------------------------------------------------
  // Events
  // -----------------------------------------------------------------------

  /// Fired when update_erd_value() stores a value that differs from what was stored.
  /// Args: tiny_erd_t erd_id (passed as pointer in args)
  i_tiny_event_t* on_erd_changed();

 private:
  // Flat array of entries, allocated at boot. ERDs stored in insertion order.
  ErdEntry entries_[MAX_ERD_ENTRIES];
  size_t   entry_count_;

  // Index table for O(1) lookup. Index by ERD ID (16-bit = 0x0000..0xFFFF).
  // Stores entry index (uint16_t). 0xFFFF means not present.
  uint16_t index_table_[65536];

  tiny_event_t erd_changed_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
