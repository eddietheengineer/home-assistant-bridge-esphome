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
//   - Provide O(log n) lookup via sorted vector + binary search
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

#include <algorithm>
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

  /// Store value from a subscription publication. ALWAYS sets publish_flag=true
  /// because the appliance only sends subscription publications when the value
  /// has actually changed - we trust the appliance's notification.
  /// Fires on_erd_changed event.
  /// Used by: SubscriptionHandler, bridge's handle_erd_client_activity_() fallback
  void update_erd_value_from_subscription(tiny_erd_t erd_id, const uint8_t* value, uint8_t size);

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
  // Find entry index by binary search. Returns entries_.size() if not found.
  size_t find_index_(tiny_erd_t erd_id) const;

  // Internal implementation shared by update_erd_value() and
  // update_erd_value_from_subscription(). 'from_subscription' controls
  // whether publish_flag is set unconditionally (true) or only on change (false).
  void update_erd_value_internal_(tiny_erd_t erd_id, const uint8_t* value, uint8_t size, bool from_subscription);

  // Sorted vector of entries, kept in erd_id order for binary search.
  // Max ~300 entries × ~36 bytes each = ~10.8 KB total.
  std::vector<ErdEntry> entries_;

  tiny_event_t erd_changed_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
