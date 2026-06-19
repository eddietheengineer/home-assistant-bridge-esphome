# Remove ESPHome MQTT, Replace ERD Publishes with Debug Logs

## Context

The GE Appliances bridge currently depends on ESPHome's MQTT component to publish ERD value updates to topics like `geappliances/{device_id}/erd/0x{ERD}/value`. The goal is to remove the ESPHome MQTT dependency entirely and replace ERD value publishes with ESPHome debug log messages (`ESP_LOGD`) that show the ERD number and its hex data payload. This enables running the bridge without an MQTT broker for debugging and development.

Only ERD value publishes are replaced. Write result publishes and HA discovery publishes are not in scope — but both are downstream of the MQTT adapter being removed, so they must be handled as no-ops.

## Approach

### Step 1: Remove MQTT dependency from `__init__.py`

**File:** `components/geappliances_bridge/__init__.py`

- **Line 21:** Change `DEPENDENCIES = ["uart", "mqtt"]` to `DEPENDENCIES = ["uart"]`.
- **Line 14:** Remove `mqtt` from the import: change `from esphome.components import esp32, mqtt, uart` to `from esphome.components import esp32, uart`.

This removes the hard dependency on the ESPHome MQTT component. Users no longer need `mqtt:` in their YAML config.

### Step 2: Replace `esphome_mqtt_client_adapter.cpp` publish logic with debug logging

**File:** `components/geappliances_bridge/esphome_mqtt_client_adapter.cpp`

This is the core change. The adapter currently implements `i_mqtt_client_t` and calls `esphome::mqtt::global_mqtt_client` for all MQTT operations. Replace all MQTT interactions with debug log output.

#### 2a: Remove MQTT include and global client access

- **Line 2:** Delete `#include "esphome/components/mqtt/mqtt_client.h"`.
- The `global_mqtt_client` singleton is accessed at lines 43, 144, 225, 302 — all are removed.

#### 2b: Rewrite `publish_now()` function (lines 39-47)

Replace with a debug-logging function:

```cpp
static void publish_now(const std::string& topic,
                        const std::string& payload,
                        bool retain)
{
  ESP_LOGD(TAG, "MQTT PUBLISH [retain=%s] topic=%s payload=%s",
           retain ? "true" : "false", topic.c_str(), payload.c_str());
}
```

#### 2c: Rewrite `update_erd()` function (lines 75-123)

This is the primary ERD value publish path. Currently it:
1. Checks ERD registry filter
2. Validates inputs
3. Builds topic string `geappliances/{device_id}/erd/0x{ERD}/value`
4. Converts binary to hex
5. Queues in `pending_updates` map

Replace with: skip the queue entirely, log the ERD and hex data directly:

```cpp
static void update_erd(i_mqtt_client_t* _self, tiny_erd_t erd, const void* value, uint8_t size)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  if (self->erd_registry != nullptr && !self->erd_registry->is_valid(erd)) {
    return;
  }

  if (value == nullptr || size == 0) {
    ESP_LOGW(TAG, "Invalid ERD update: null value or zero size for ERD 0x%04X", erd);
    return;
  }

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);
  std::string hex;
  hex.reserve(size * 2);
  for (uint8_t i = 0; i < size; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", bytes[i]);
    hex += buf;
  }

  ESP_LOGD(TAG, "ERD 0x%04X: %s", erd, hex.c_str());
}
```

Key decisions:
- The ERD registry filter is preserved (same as current behavior).
- The `pending_updates` map is no longer populated — it's dead code after this change.
- The hex conversion is inlined (same algorithm as before).
- The log format is `ERD 0x{XXXX}: {hex}` — concise, matches the existing hex conversion pattern in the codebase.

#### 2d: Rewrite `update_erd_write_result()` function (lines 125-152)

Replace MQTT publish with debug log:

```cpp
static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  (void)_self;
  (void)failure_reason;
  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, success ? "success" : "failure");
}
```

#### 2e: Rewrite `subscribe_write_topic()` function (lines 208-291)

This function subscribes to `geappliances/{device_id}/erd/+/write` for incoming write commands. Without MQTT, this is a no-op. Replace entirely:

```cpp
extern "C" void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self)
{
  (void)self;
  // No MQTT broker — write command subscriptions are not available.
}
```

The write-request callback lambda (lines 232-286) is removed entirely. Write commands from MQTT will not be processed.

#### 2f: Rewrite `drain_pending_updates()` function (lines 293-317)

Replace with no-op that returns 0 (queue is always empty):

```cpp
extern "C" size_t esphome_mqtt_client_adapter_drain_pending_updates(
  esphome_mqtt_client_adapter_t* self)
{
  (void)self;
  return 0;
}
```

#### 2g: Rewrite `notify_connected()` function (lines 319-324)

Replace with no-op:

```cpp
extern "C" void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self)
{
  (void)self;
}
```

#### 2h: Rewrite `notify_disconnected()` function (lines 196-206)

The disconnect event fires `on_mqtt_disconnect_event` which triggers HSM state transitions in both bridges. Without MQTT, this is never fired:

```cpp
extern "C" void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self)
{
  (void)self;
}
```

#### 2i: Rewrite `esphome_mqtt_client_adapter_publish()` function (lines 348-355)

Replace with debug log:

```cpp
extern "C" void esphome_mqtt_client_adapter_publish(
  esphome_mqtt_client_adapter_t* /*self*/,
  const std::string& topic,
  const std::string& payload,
  bool retain)
{
  publish_now(topic, payload, retain);
}
```

#### 2j: Update `register_erd()` function (lines 54-73)

Keep the ERD registry tracking and debug log — these don't depend on MQTT:

```cpp
static void register_erd(i_mqtt_client_t* _self, tiny_erd_t erd)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  if (self->erd_registry != nullptr) {
    self->erd_registry->register_erd(erd);
  }

  ESP_LOGD(TAG, "Registered ERD 0x%04X", erd);
}
```

#### 2k: Clean up adapter struct in header

**File:** `components/geappliances_bridge/esphome_mqtt_client_adapter.h`

The `pending_updates` map, `wildcard_subscribed` flag, and `mqtt_connected_at_ms` field are no longer needed. However, to minimize header churn and keep the `i_mqtt_client_t` interface stable, these fields remain in the struct but are unused. The `init()` function still allocates `pending_updates` (it's a `new std::map<...>` call) — this is harmless overhead. No header changes are required.

### Step 3: Remove MQTT connectivity gating from startup HSM

**File:** `components/geappliances_bridge/geappliances_bridge_startup_hsm.cpp`

Three locations check `mqtt::global_mqtt_client->is_connected()` as a gate before transitioning to `startup_state_bridge_init`:

#### 3a: `startup_state_feature_bits` — `signal_run_loop` handler (lines 280-286)

Change from:
```cpp
bool feature_bits_done = svc->is_feature_bits_complete();
bool mqtt_connected = (mqtt::global_mqtt_client != nullptr &&
                       mqtt::global_mqtt_client->is_connected());

if (feature_bits_done && mqtt_connected) {
  tiny_hsm_transition(hsm, startup_state_bridge_init);
}
```

To:
```cpp
bool feature_bits_done = svc->is_feature_bits_complete();
if (feature_bits_done) {
  tiny_hsm_transition(hsm, startup_state_bridge_init);
}
```

#### 3b: `startup_state_feature_bits` — `signal_feature_bits_complete` handler (lines 296-303)

Change from:
```cpp
bool mqtt_connected = (mqtt::global_mqtt_client != nullptr &&
                       mqtt::global_mqtt_client->is_connected());
if (mqtt_connected) {
  tiny_hsm_transition(hsm, startup_state_bridge_init);
}
```

To:
```cpp
tiny_hsm_transition(hsm, startup_state_bridge_init);
```

#### 3c: `startup_state_bridge_init` — `signal_run_loop` handler (lines 334-342)

Change from:
```cpp
if (!svc->is_bridge_initialized() &&
    svc->is_autodiscovery_complete() &&
    mqtt::global_mqtt_client != nullptr &&
    mqtt::global_mqtt_client->is_connected()) {
```

To:
```cpp
if (!svc->is_bridge_initialized() &&
    svc->is_autodiscovery_complete()) {
```

#### 3d: `startup_state_bridge_init` — `signal_mqtt_connected` handler (lines 345-350)

This handler is triggered by `signal_mqtt_connected` from the loop() FSM. Without MQTT, this signal is never sent. Remove the handler entirely (or leave it as dead code — it will never fire). The safest approach: remove the `case signal_mqtt_connected:` block.

#### 3e: Remove MQTT include

**Line 19:** Delete `#include "esphome/components/mqtt/mqtt_client.h"`.

### Step 4: Remove MQTT FSM from `geappliances_bridge.cpp` loop()

**File:** `components/geappliances_bridge/geappliances_bridge.cpp`

#### 4a: Remove the MQTT connection FSM block (lines 185-255)

The entire block starting at line 185 (`{ auto mqtt_client = mqtt::global_mqtt_client; ... }`) through line 255 is removed. This includes:
- DISCONNECTED → SUBSCRIBING transition
- SUBSCRIBING → FLUSHING transition (calls `subscribe_write_topic`)
- FLUSHING → RUNNING transition (calls `drain_pending_updates`)
- RUNNING state (calls `drain_pending_updates` each loop)

#### 4b: Remove MQTT include

Search for and remove `#include "esphome/components/mqtt/mqtt_client.h"` if present in this file.

#### 4c: Update `initialize_mqtt_client_()` in `geappliances_bridge_bridge_init.cpp`

**File:** `components/geappliances_bridge/geappliances_bridge_bridge_init.cpp`

The `initialize_mqtt_client_()` function (lines 85-116) calls `esphome_mqtt_client_adapter_init()`. This remains unchanged — the adapter init is still needed for the `i_mqtt_client_t` interface. The adapter init does not access the MQTT client directly.

#### 4d: Update `run_ha_discovery()` call in loop()

**File:** `components/geappliances_bridge/geappliances_bridge.cpp`

The `run_ha_discovery()` method passes `mqtt::global_mqtt_client` to `ha_discovery_manager_.run()`. Without MQTT, this is `nullptr`. The HA discovery manager already handles `nullptr` mqtt_client gracefully (it logs a warning and skips publishing). No change needed here, but the `#include "esphome/components/mqtt/mqtt_client.h"` in `geappliances_bridge.h` (line 32) and `ha_discovery_manager.cpp` (line 9) must be removed.

### Step 5: Clean up MQTT includes across all files

Remove `#include "esphome/components/mqtt/mqtt_client.h"` from:
- `components/geappliances_bridge/esphome_mqtt_client_adapter.cpp` (line 2)
- `components/geappliances_bridge/geappliances_bridge.h` (line 32)
- `components/geappliances_bridge/geappliances_bridge_startup_hsm.cpp` (line 19)
- `components/geappliances_bridge/ha_discovery_manager.cpp` (line 9)
- `components/geappliances_bridge/ha_discovery_manager.h` — has forward declaration `namespace esphome { namespace mqtt { class MQTTClientComponent; } }` (lines 57-60). This forward declaration must be removed, and the `run()` method signature must change to not accept `mqtt::MQTTClientComponent*`.

### Step 6: Update HA Discovery Manager for no-MQTT

**File:** `components/geappliances_bridge/ha_discovery_manager.h`

- Remove the forward declaration of `mqtt::MQTTClientComponent` (lines 57-60).
- Change `run()` signature from:
  ```cpp
  void run(bool is_poll_mode,
           bool polling_bridge_initialized,
           bool polling_list_complete,
           bool subscription_activity_detected,
           mqtt::MQTTClientComponent* mqtt_client);
  ```
  To:
  ```cpp
  void run(bool is_poll_mode,
           bool polling_bridge_initialized,
           bool polling_list_complete,
           bool subscription_activity_detected);
  ```
- Change `publish_ha_discovery_()` and `publish_next_entity_()` private method signatures to remove `mqtt::MQTTClientComponent*` parameter.

**File:** `components/geappliances_bridge/ha_discovery_manager.cpp`

- Remove `#include "esphome/components/mqtt/mqtt_client.h"` (line 9).
- Update `run()` to not accept `mqtt_client` parameter.
- Update `publish_ha_discovery_()` to be a no-op (logs a debug message and transitions to COMPLETE).
- Update `publish_next_entity_()` to be a no-op.

**File:** `components/geappliances_bridge/geappliances_bridge.cpp`

- Update the call to `ha_discovery_manager_.run()` to remove the `mqtt::global_mqtt_client` argument.

### Step 7: Remove `MqttConnectionState` enum and related state

**File:** `components/geappliances_bridge/geappliances_bridge.h`

- Remove the `MqttConnectionState` enum (lines 152-157).
- Remove the `mqtt_connection_state_` member (line 158).
- Remove `#include "esphome/components/mqtt/mqtt_client.h"` (line 32).

### Step 8: Update unit tests

**File:** `test/tests/esphome_mqtt_client_adapter_test.cpp`

The existing tests verify MQTT publish behavior (topic, payload, QoS, retain). These tests must be updated to verify debug log output instead.

- Remove the `MockMqttClient` struct (lines 36-73) — it extends `MQTTClientComponent`.
- Rewrite tests to verify that `update_erd()` produces the expected log output. Since `ESP_LOGD` is stubbed as `((void)0)` in tests (`test/include/double/esphome_hal_double.hpp`), the tests should verify that:
  - `update_erd()` does not crash
  - `update_erd()` respects the ERD registry filter
  - `update_erd()` handles null/zero-size inputs
  - `drain_pending_updates()` returns 0
  - `notify_connected()` is a no-op
  - `notify_disconnected()` is a no-op
  - `subscribe_write_topic()` is a no-op

Specific test changes:
- `update_erd_publishes_hex_when_connected` → verify no crash, remove mock setup
- `update_erd_hex_payload_is_valid_uppercase_hex` → remove (no publish to verify)
- `update_erd_queues_when_disconnected` → remove (no queue)
- `update_erd_write_result_publishes_success` → verify no crash
- `update_erd_write_result_publishes_failure` → verify no crash
- `update_erd_write_result_drops_when_disconnected` → verify no crash
- `notify_connected_subscribes_wildcard_once` → verify no crash
- `notify_connected_does_not_subscribe_if_not_connected` → verify no crash
- `notify_connected_flushes_pending_updates` → verify returns 0
- `notify_connected_flushes_max_per_call` → verify returns 0
- `notify_disconnected_resets_connect_time` → verify no crash
- `write_request_callback_registered` → remove (no subscription)
- `update_erd_overwrites_pending_for_same_erd` → remove (no queue)

**File:** `test/tests/erd_bridge_poll_test.cpp`

Check for any direct MQTT client references. The polling bridge tests use `mqtt_client_double.cpp` which implements `i_mqtt_client_t` — these should continue to work as the interface is unchanged.

**File:** `test/tests/erd_bridge_subscribe_test.cpp`

Same as above — uses the `i_mqtt_client_t` interface via doubles.

### Step 9: Update documentation

**File:** `doc/example.yaml`

Remove the `mqtt:` block (lines 39-45).

**File:** `doc/test-compile.yaml`

Remove the `mqtt:` block (lines 39-46).

## Critical Files & Anchors

| File | Symbol/Region | Reason |
|------|--------------|--------|
| `components/geappliances_bridge/esphome_mqtt_client_adapter.cpp` | `update_erd()` (line 75) | Primary ERD publish → debug log conversion |
| `components/geappliances_bridge/esphome_mqtt_client_adapter.cpp` | `publish_now()` (line 39) | MQTT publish → debug log |
| `components/geappliances_bridge/geappliances_bridge_startup_hsm.cpp` | `startup_state_feature_bits` (line 268) | MQTT gating removal |
| `components/geappliances_bridge/geappliances_bridge.cpp` | `loop()` MQTT FSM (line 185) | FSM removal |
| `components/geappliances_bridge/__init__.py` | `DEPENDENCIES` (line 21) | Dependency removal |

## Verification

### Build verification

```bash
cd ~/home-assistant-bridge-esphome
# Verify the component compiles without the MQTT dependency
esphome compile doc/test-compile.yaml
```

The `test-compile.yaml` must be updated to remove the `mqtt:` block before this will work.

### Unit test verification

```bash
cd ~/home-assistant-bridge-esphome
make -C test
```

All existing tests must pass after the adapter is converted to debug-only mode. The `esphome_mqtt_client_adapter_test.cpp` tests are rewritten to verify no-crash behavior rather than MQTT publish correctness.

### Debug log verification

After flashing to hardware, the serial log should show lines like:
```
[D][geappliances_bridge.mqtt]: ERD 0x0008: 0A
[D][geappliances_bridge.mqtt]: ERD 0x0001: 4A4553393530305353530000
[D][geappliances_bridge.mqtt]: Registered ERD 0x0008
```

### Behavioral verification

- The bridge must complete startup (all HSM phases) without MQTT connected.
- ERD polling must proceed normally, with values logged instead of published.
- The subscription bridge must not block waiting for MQTT.
- HA discovery must not crash when mqtt_client is null (it should log a warning and skip).

## Assumptions & contingencies

1. **No MQTT broker available**: The component runs entirely offline. Write commands from MQTT are not supported (the wildcard subscription is a no-op). This is acceptable for debug-only usage.

2. **HA discovery is disabled by default** (`generate_device_config: false` in many configs). Even if enabled, the HA discovery manager gracefully handles null mqtt_client by logging a warning and transitioning to COMPLETE state.

3. **The `i_mqtt_client_t` interface remains unchanged**. The bridges (`erd_bridge_subscribe.cpp`, `erd_bridge_poll.cpp`) call `mqtt_client_register_erd()` and `mqtt_client_update_erd()` through the interface. The adapter still implements this interface — it just logs instead of publishing. No changes needed in the bridge files.

4. **If the user later wants MQTT back**: They would need to revert this change or use a separate branch/config. There is no compile-time toggle — this is a clean cutover.

5. **Test doubles**: `test/src/mqtt_client_double.cpp` implements `i_mqtt_client_t` for tests. It does not depend on ESPHome's MQTT component — it's a standalone C struct. No changes needed.

6. **`esphome_mqtt_client_adapter.h` still includes `erd_registry.h`**: The ERD registry is used for filtering and is independent of MQTT. This dependency remains.
