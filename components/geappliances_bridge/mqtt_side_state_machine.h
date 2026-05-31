// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Standalone FSM that reads flagged ERDs from ErdStateTable and
//       publishes them to MQTT. Extracted from the inline MqttConnectionState
//       FSM in GeappliancesBridge::loop().
//
// Responsibilities:
//   - Manage MQTT-side state machine (Disconnected, Subscribing, Flushing, Running)
//   - Drain flagged ERDs from ErdStateTable and publish via the adapter
//   - Subscribe to the wildcard write topic on first connect after disconnect
//   - Enforce MAX_FLUSH_PER_CALL limit per loop() call
//
// NOT responsible for:
//   - Any appliance protocol logic (ApplianceSideStateMachine does that)
//   - Bridge startup lifecycle (StartupHsm does that)
//   - Parsing MQTT write commands (WriteRouter does that)
//
// Dependencies:
//   - ErdStateTable (reads flagged ERDs, clears flags after publish)
//   - GlobalStateRegistry (device_id for topic construction)
//   - ErdRegistry (is_string_type for payload formatting)
//   - esphome_mqtt_client_adapter_t (publish, subscribe, is_connected)
//   - WriteRouter (constructor parameter for future wiring; no-op in this FSM)
//
// Construction order constraint:
//   esphome_mqtt_client_adapter_init() must be called before constructing
//   this FSM, because the adapter must be valid when loop() is first called.
// =============================================================================

#pragma once

#include <cstdint>

#include "erd_state_table.h"
#include "global_state_registry.h"
#include "erd_registry.h"
#include "erd_payload_formatter.h"
#include "esphome_mqtt_client_adapter.h"

namespace esphome {
namespace geappliances_bridge {

// Forward declaration — MqttSideStateMachine takes a pointer so it does not
// need the full WriteRouter definition at compile time.
class WriteRouter;

class MqttSideStateMachine {
 public:
  // States map directly from the existing inline MqttConnectionState FSM.
  enum class State {
    DISCONNECTED,
    SUBSCRIBING,
    FLUSHING,
    RUNNING
  };

  // Constructor: all dependencies are passed as raw pointers.
  // state_table:  ErdStateTable to read flagged ERDs from
  // registry:     GlobalStateRegistry for device_id
  // erd_registry: ErdRegistry for is_string_type lookups
  // adapter:      MQTT adapter for publish/subscribe/is_connected
  // write_router: WriteRouter (reserved for future wiring; not used by this FSM)
  MqttSideStateMachine(
    ErdStateTable* state_table,
    GlobalStateRegistry* registry,
    ErdRegistry* erd_registry,
    esphome_mqtt_client_adapter_t* adapter,
    WriteRouter* write_router);

  // Fast, non-blocking; called from GeappliancesBridge::loop().
  void loop();

  // Returns the current state.
  State get_current_state() const;

  // Called by GeappliancesBridge when MQTT connect/disconnect events fire.
  void on_mqtt_connected();
  void on_mqtt_disconnected();

  // Note: no on_mqtt_ack() -- ESPHome publish is fire-and-forget at QoS=0.

 private:
  static constexpr size_t MAX_FLUSH_PER_CALL = 5;

  ErdStateTable* state_table_;
  GlobalStateRegistry* registry_;
  ErdRegistry* erd_registry_;
  esphome_mqtt_client_adapter_t* adapter_;
  WriteRouter* write_router_;

  State state_;

  // Shared by Flushing and Running states.
  void drain_flagged_erds_();
};

}  // namespace geappliances_bridge
}  // namespace esphome
