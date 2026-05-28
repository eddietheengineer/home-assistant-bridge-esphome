// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Define the BridgeMode enumeration as a standalone, shared type so
//       it can be referenced by both IBridgeServices and GeappliancesBridge
//       without creating a circular dependency.
//
// Responsibilities:
//   - Declare BridgeMode and its three values (POLL, SUBSCRIBE, AUTO)
//   - Provide the mapping comment that ties values to __init__.py constants
//
// NOT responsible for:
//   - Any logic, state, or behaviour
//
// Dependencies:
//   - none
// =============================================================================

#pragma once

namespace esphome {
namespace geappliances_bridge {

// Operation mode for the bridge.
// NOTE: These enum values must match MODE_*_VALUE constants in __init__.py.
enum BridgeMode {
  BRIDGE_MODE_POLL      = 0,  // Always use polling mode
  BRIDGE_MODE_SUBSCRIBE = 1,  // Always use subscription mode
  BRIDGE_MODE_AUTO      = 2   // Auto: try subscription, fallback to polling
};

}  // namespace geappliances_bridge
}  // namespace esphome
