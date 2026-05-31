// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Manage subscribable global state variables accessible to all modules.
//       The StartupHsm sets these values; the FSMs read them.
//
// Responsibilities:
//   - Store device_id, appliance_address, gea_protocol_type, bridge_state
//   - Fire typed per-field events when values change (not string-keyed)
//   - device_id is write-once; fires on_device_id_ready() when first set
//
// NOT responsible for:
//   - Any bridge lifecycle management
//   - Reading or writing ERD data
//
// Dependencies:
//   - i_tiny_event.h (tiny event subscription pattern)
// =============================================================================

#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include "i_tiny_event.h"
#include "tiny_event.h"
}

namespace esphome {
namespace geappliances_bridge {

// Bridge lifecycle state, updated as startup progresses.
enum class BridgeState {
  STARTING,
  RUNNING,
  ERROR
};

class GlobalStateRegistry {
 public:
  GlobalStateRegistry();

  // -----------------------------------------------------------------------
  // Read accessors
  // -----------------------------------------------------------------------

  const std::string& get_device_id() const;
  uint8_t get_appliance_address() const;
  uint8_t get_gea_protocol_type() const;
  BridgeState get_bridge_state() const;

  // -----------------------------------------------------------------------
  // Write methods (called only during startup/initialization)
  // -----------------------------------------------------------------------

  /// Set device_id (write-once; fires on_device_id_ready() when first set).
  void set_device_id(const std::string& id);

  /// Set appliance address (notifies subscribers).
  void set_appliance_address(uint8_t addr);

  /// Set GEA protocol type (notifies subscribers).
  void set_gea_protocol_type(uint8_t type);

  /// Set bridge state (notifies subscribers).
  void set_bridge_state(BridgeState state);

  // -----------------------------------------------------------------------
  // Event accessors (typed per-field, not string-keyed)
  // -----------------------------------------------------------------------

  i_tiny_event_t* on_device_id_ready();
  i_tiny_event_t* on_appliance_address_changed();
  i_tiny_event_t* on_gea_protocol_type_changed();
  i_tiny_event_t* on_bridge_state_changed();

 private:
  std::string device_id_;
  uint8_t     appliance_address_;
  uint8_t     gea_protocol_type_;
  BridgeState bridge_state_;
  bool        device_id_set_;

  tiny_event_t device_id_ready_;
  tiny_event_t appliance_address_changed_;
  tiny_event_t gea_protocol_type_changed_;
  tiny_event_t bridge_state_changed_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
