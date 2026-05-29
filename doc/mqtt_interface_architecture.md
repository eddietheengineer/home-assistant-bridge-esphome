# MQTT Server Interface Architecture

## Overview

This document describes how the MQTT broker (ESPHome's MQTT client) interfaces
with the rest of the geappliances_bridge component. It identifies every coupling
point and rates how well-decoupled each layer is, along with concrete
recommendations for improvement.

The MQTT subsystem has three consumers and one adapter:

```
                    ┌─────────────────────────────────────────┐
                    │  ESPHome MQTT Client (global singleton)  │
                    │  esphome::mqtt::MQTTClientComponent      │
                    └───────────────┬─────────────────────────┘
                                    │  (direct calls)
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
              ▼                     ▼                     ▼
    ┌─────────────────┐  ┌──────────────────┐  ┌──────────────────┐
    │ Geappliances    │  │ EsphomeMqtt      │  │ HaDiscovery      │
    │ Bridge loop()   │  │ Client Adapter   │  │ Manager          │
    │ (FSM states)    │  │ (esphome_mqtt_   │  │ (ha_discovery_   │
    │                 │  │  client_adapter) │  │  manager)        │
    └────────┬────────┘  └────────┬─────────┘  └────────┬─────────┘
             │                    │                      │
             │  calls adapter     │  implements          │  uses adapter
             │  functions         │  i_mqtt_client_t     │  + direct MQTT
             │                    │  interface           │  fallback
             ▼                    ▼                      ▼
       ┌─────────────────────────────────────────────────────────┐
       │  tiny-gea-api bridge libraries (ESPHome-independent)    │
       │  mqtt_bridge, mqtt_bridge_polling                        │
       │  (consume i_mqtt_client_t only)                          │
       └─────────────────────────────────────────────────────────┘
```

## Layer 1: The Abstract Interface (i_mqtt_client_t)

**File:** `i_mqtt_client.h`

**Coupling:** NONE to ESPHome. Pure C interface.

The `i_mqtt_client_t` struct is a vtable-based interface with five callbacks:
- `register_erd(erd)` -- called when a new ERD is discovered
- `update_erd(erd, value, size)` -- called when an ERD value changes
- `update_erd_write_result(erd, success, reason)` -- called after a write completes
- `on_write_request()` -- returns an event pointer for incoming write commands
- `on_mqtt_disconnect()` -- returns an event pointer for disconnect notifications

**Who uses it:**
- `mqtt_bridge` (subscription mode) -- receives ERD updates, sends write commands
- `mqtt_bridge_polling` (polling mode) -- same contract

Both bridge libraries are completely ESPHome-independent. They only know about
`i_mqtt_client_t`. This is the strongest decoupling boundary in the system.

**Rating: EXCELLENT** -- No improvement needed.

---

## Layer 2: The Adapter (esphome_mqtt_client_adapter)

**Files:** `esphome_mqtt_client_adapter.h`, `esphome_mqtt_client_adapter.cpp`

**Coupling:** TIGHT to ESPHome internals.

The adapter is the single implementation of `i_mqtt_client_t`. It bridges the
ESPHome-independent tiny-gea-api libraries to ESPHome's MQTT client.

### Direct ESPHome Dependencies

1. **`esphome::mqtt::global_mqtt_client`** -- accessed directly in multiple
   places (`publish_now()`, `subscribe_write_topic()`, `drain_pending_updates()`).
   This is a global singleton pointer to `MQTTClientComponent`.

2. **`esphome::mqtt::MQTTClientComponent::publish()`** -- called via the global
   pointer for ERD value publishes, write result publishes, and HA discovery
   messages.

3. **`esphome::mqtt::MQTTClientComponent::subscribe()`** -- called to register
   the wildcard write topic. Accepts a C++ lambda closure.

4. **`esphome::millis()`** -- used for the connect-timestamp gating logic.

5. **`esphome::core::log.h`** -- ESP_LOGD, ESP_LOGW, ESP_LOGI macros.

### Coupling Points Within the Adapter

| Function | ESPHome Dependency | Coupling Level |
|----------|-------------------|----------------|
| `publish_now()` | `global_mqtt_client->publish()` | Direct call |
| `subscribe_write_topic()` | `global_mqtt_client->subscribe()` + C++ lambda | Direct call + closure |
| `drain_pending_updates()` | `global_mqtt_client->is_connected()`, `publish()` | Direct call |
| `notify_connected()` | Calls subscribe + drain | Indirect |
| `update_erd_write_result()` | `global_mqtt_client->publish()` (bypasses pending queue) | Direct call |
| `esphome_mqtt_client_adapter_publish()` | `publish_now()` | Indirect |

### ErdRegistry Dependency

The adapter holds a pointer to `ErdRegistry` (a C++ class in the
`esphome::geappliances_bridge` namespace). This is used for:
- Valid-ERD filtering (`is_valid()`)
- String-type detection (`is_string_type()`)
- Runtime registration tracking (`register_erd()`)

This is a moderate coupling -- the adapter depends on a project-internal type,
not ESPHome itself, but it does mean the adapter cannot be reused outside this
project without also porting `ErdRegistry`.

### Issues and Recommendations

**Issue 1: `update_erd_write_result()` bypasses the pending queue**
All other ERD updates go through the pending queue (`update_erd`), but write
results publish directly via `global_mqtt_client`. If MQTT is disconnected, the
write result is silently dropped. This is inconsistent with the rest of the
adapter's behavior.

*Recommendation:* Route write results through the same pending-update mechanism
as regular ERD updates, or at least log a warning when dropped.

**Issue 2: Direct access to `esphome::mqtt::global_mqtt_client` in 4+ places**
The adapter reaches into ESPHome internals multiple times. This means the
adapter cannot be unit-tested without the full ESPHome framework linked.

*Recommendation:* Introduce a thin abstraction layer -- an `i_mqtt_backend_t`
interface with `publish()`, `subscribe()`, and `is_connected()` callbacks. The
ESPHome adapter would implement this interface by delegating to
`global_mqtt_client`. The adapter logic would only depend on the interface, not
the ESPHome global. This would enable mocking in tests and make it trivial to
swap in a different MQTT backend (e.g., a direct MQTT library for non-ESPHome
deployments).

**Issue 3: C++ lambda closure captures `self` pointer**
The `subscribe_write_topic()` function creates a lambda that captures the
adapter pointer. This is fine functionally but means the adapter's memory must
outlive the MQTT client's internal subscription storage. There is no explicit
unsubscription on destroy.

*Recommendation:* Document the lifetime contract explicitly, or add an
unsubscribe call in `esphome_mqtt_client_adapter_destroy()`.

**Rating: MODERATE** -- Good interface abstraction (i_mqtt_client_t), but the
implementation is tightly coupled to ESPHome internals.

---

## Layer 3: GeappliancesBridge loop() -- MQTT FSM

**File:** `geappliances_bridge.cpp` (lines ~195-248)

**Coupling:** TIGHT to both ESPHome and the adapter.

The bridge's `loop()` method contains an MQTT connection state machine with four
states: DISCONNECTED, SUBSCRIBING, FLUSHING, RUNNING.

### Coupling Points

1. **Direct read of `esphome::mqtt::global_mqtt_client`** -- The FSM reads
   `mqtt_client->is_connected()` on every loop iteration to detect connect/
   disconnect edges. This is a direct dependency on the ESPHome global.

2. **Calls to adapter functions** -- `esphome_mqtt_client_adapter_notify_
   disconnected()`, `esphome_mqtt_client_adapter_subscribe_write_topic()`,
   `esphome_mqtt_client_adapter_drain_pending_updates()`. These are the
   intended interface, so this coupling is acceptable.

3. **Startup HSM signal** -- `tiny_hsm_send_signal(&startup_hsm_,
   signal_mqtt_connected, nullptr)` is sent on the connect edge. This ties the
   MQTT connection lifecycle to the startup sequence.

4. **`mqtt_client_adapter_initialized_` guard** -- The FSM checks this boolean
   before calling adapter functions, preventing calls before the adapter is
   ready. This is a coordination dependency between the startup HSM (which
   initializes the adapter) and the loop FSM.

### Issues and Recommendations

**Issue 1: FSM reads `global_mqtt_client` directly instead of asking the adapter**
The adapter already tracks connection state internally (`mqtt_connected_at_ms`,
`wildcard_subscribed`). The FSM could delegate the connection check to the
adapter instead of reading the ESPHome global directly.

*Recommendation:* Add an `esphome_mqtt_client_adapter_is_connected()` function
that returns the adapter's view of connectivity. The FSM would then only depend
on the adapter, not the ESPHome global.

**Issue 2: FSM and adapter both know about subscribe/drain semantics**
The FSM calls `subscribe_write_topic()` then `drain_pending_updates()` in a
specific order (SUBSCRIBING -> FLUSHING -> RUNNING). The adapter's
`notify_connected()` does the same thing internally. This is redundant -- the
FSM essentially reimplements what `notify_connected()` does.

*Recommendation:* Consider collapsing the FSM states. If the adapter can
report its own readiness (subscribed + flushed), the FSM could be simplified
to just DISCONNECTED / CONNECTED, delegating the subscribe+drain logic entirely
to the adapter.

**Issue 3: `signal_mqtt_connected` sent before adapter is initialized**
The connect edge fires `signal_mqtt_connected` immediately, but the adapter may
not be initialized yet (the startup HSM hasn't reached `mqtt_client_init`
phase). The FSM handles this with the `mqtt_client_adapter_initialized_` guard,
but it means the signal fires and gets deferred, adding complexity.

*Recommendation:* This is acceptable as-is. The guard pattern is clear.

**Rating: MODERATE** -- The FSM is well-structured but could be further
decoupled from ESPHome by delegating more to the adapter.

---

## Layer 4: HaDiscoveryManager

**Files:** `ha_discovery_manager.h`, `ha_discovery_manager.cpp`

**Coupling:** MIXED -- uses both the adapter and direct ESPHome access.

### Coupling Points

1. **`set_mqtt_adapter(esphome_mqtt_client_adapter_t*)`** -- The bridge passes
   the adapter pointer so HA discovery can use `esphome_mqtt_client_adapter_
   publish()` for async publishing.

2. **`run()` accepts `mqtt::MQTTClientComponent*`** -- The `run()` method
   takes a raw pointer to the ESPHome MQTT client. This is passed from the
   bridge's `loop()` on every call.

3. **`publish_next_entity_()` dual-path logic** -- If `mqtt_adapter_` is
   non-null, it uses `esphome_mqtt_client_adapter_publish()`. Otherwise, it
   falls back to `mqtt_client->publish()` directly.

4. **`#include "esphome_mqtt_client_adapter.h"`** in the header -- This
   creates a compile-time dependency on the adapter's full type definition.

5. **`#include "esphome/components/mqtt/mqtt_client.h"`** in the .cpp --
   Direct ESPHome header inclusion for the `MQTTClientComponent` type.

### Issues and Recommendations

**Issue 1: Dual publish paths (adapter vs. direct MQTT)**
The manager supports both async publishing via the adapter and synchronous
publishing via the raw MQTT client. This means the manager depends on BOTH
the adapter AND the ESPHome MQTT client type. If the adapter is always
available (which it is in normal operation), the direct MQTT path is dead code.

*Recommendation:* Remove the `mqtt::MQTTClientComponent*` parameter from
`run()` and the direct fallback in `publish_next_entity_()`. The manager
should only depend on the adapter. If a non-ESPHome build needs HA discovery,
it can provide its own `i_mqtt_client_t` implementation.

**Issue 2: Header dependency on `esphome_mqtt_client_adapter.h`**
The header includes the adapter's full type definition (`esphome_mqtt_client_
adapter_t*`). This means any file that includes `ha_discovery_manager.h` also
pulls in the adapter's dependencies (including `erd_registry.h`, which pulls
in ESPHome headers).

*Recommendation:* Use a forward declaration (`struct esphome_mqtt_client_
adapter_t;`) in the header. Only include the full header in the .cpp file.
This reduces compile-time dependencies.

**Issue 3: `run()` signature carries ESPHome type**
The `run()` method signature includes `mqtt::MQTTClientComponent*`, which
forces every caller to have ESPHome's MQTT header available.

*Recommendation:* Replace the `mqtt::MQTTClientComponent*` parameter with
`i_mqtt_client_t*` (the abstract interface). The caller passes
`&mqtt_client_adapter_.interface`. This makes the manager fully ESPHome-
independent at the API level.

**Rating: POOR** -- The manager is the most tightly coupled component to
ESPHome. It should be refactorable to use only the abstract `i_mqtt_client_t`
interface.

---

## Layer 5: Bridge Initialization (geappliances_bridge_bridge_init.cpp)

**File:** `geappliances_bridge_bridge_init.cpp`

**Coupling:** MODERATE -- wiring layer, expected to know about concrete types.

### Coupling Points

1. **`initialize_mqtt_client_()`** -- Calls `esphome_mqtt_client_adapter_init()`
   and `esphome_mqtt_client_adapter_set_erd_registry()`. This is the expected
   initialization path.

2. **`initialize_mqtt_bridge_()`** -- Passes `&mqtt_client_adapter_.interface`
   to both `mqtt_bridge_init()` and `mqtt_bridge_polling_init()`. This is the
   correct pattern -- the bridges only see the abstract interface.

3. **`start_custom_erd_polling_()`** -- Same pattern: passes the interface
   pointer to `mqtt_bridge_polling_init_at_address()`.

4. **`ha_discovery_manager_.set_mqtt_adapter(&mqtt_client_adapter_)`** --
   Passes the concrete adapter type to HA discovery.

### Assessment

This file is the "wiring layer" -- it's expected to know about concrete types
because it's where all components are assembled. The coupling here is
appropriate. The key observation is that the bridges themselves receive only
the abstract `i_mqtt_client_t*`, which is correct.

**Rating: ACCEPTABLE** -- Wiring code should know concrete types.

---

## Summary of Coupling Points

| Consumer | ESPHome Dependency | Abstract Interface Used | Rating |
|----------|-------------------|------------------------|--------|
| `mqtt_bridge` | NONE | `i_mqtt_client_t*` | EXCELLENT |
| `mqtt_bridge_polling` | NONE | `i_mqtt_client_t*` | EXCELLENT |
| `esphome_mqtt_client_adapter` | `global_mqtt_client`, `MQTTClientComponent::publish/subscribe`, `millis()` | Implements `i_mqtt_client_t` | MODERATE |
| `GeappliancesBridge::loop()` FSM | `global_mqtt_client->is_connected()` | Calls adapter functions | MODERATE |
| `HaDiscoveryManager` | `MQTTClientComponent*` parameter, direct `publish()` fallback | `esphome_mqtt_client_adapter_t*` (concrete) | POOR |

## Priority Recommendations (ordered by impact)

### 1. Make HaDiscoveryManager use `i_mqtt_client_t` instead of concrete types (HIGH)

Replace the `mqtt::MQTTClientComponent*` parameter in `run()` and
`publish_next_entity_()` with `i_mqtt_client_t*`. Remove the direct ESPHome
publish fallback. This makes HA discovery fully ESPHome-independent at the API
level, matching how the bridges already work.

Changes needed:
- `ha_discovery_manager.h`: change `run()` signature, forward-declare
  `i_mqtt_client_t`, remove `#include "esphome_mqtt_client_adapter.h"` from
  header
- `ha_discovery_manager.cpp`: remove ESPHome MQTT includes, use
  `mqtt_client_publish()` inline wrapper or add a publish callback to the
  interface
- `geappliances_bridge_bridge_init.cpp`: pass `&mqtt_client_adapter_.interface`
  instead of `mqtt::global_mqtt_client`

### 2. Add `i_mqtt_backend_t` abstraction inside the adapter (MEDIUM)

Create a minimal C interface for the MQTT backend operations the adapter needs:
`publish(topic, payload, qos, retain)`, `subscribe(topic, callback, qos)`,
`is_connected()`. The ESPHome implementation delegates to `global_mqtt_client`.
A mock implementation enables unit testing. The adapter logic becomes ESPHome-
independent.

### 3. Add `esphome_mqtt_client_adapter_is_connected()` (MEDIUM)

Add a function that returns whether the adapter considers MQTT connected. The
FSM in `loop()` can then drop its direct read of `global_mqtt_client` and
delegate to the adapter instead.

### 4. Consolidate FSM states or delegate to adapter (LOW)

The FSM's SUBSCRIBING/FLUSHING/RUNNING states largely duplicate logic that
`notify_connected()` and `subscribe_write_topic()` already handle. Consider
whether the adapter can expose a single `is_ready()` function that the FSM
checks, reducing the state machine to DISCONNECTED/CONNECTED.

### 5. Fix `update_erd_write_result()` to use pending queue (LOW)

Route write results through the same pending-update mechanism as regular ERD
updates for consistency. Currently they are published directly and silently
dropped on disconnect.

## What is Already Well-Decoupled

- **`mqtt_bridge` and `mqtt_bridge_polling`** are completely ESPHome-independent.
  They only depend on `i_mqtt_client_t`, `i_tiny_gea3_erd_client_t`, `tiny_hsm`,
  and `tiny_timer`. These could be compiled and tested on any platform with
  a mock `i_mqtt_client_t` implementation.

- **`i_mqtt_client.h`** is a clean C interface with no ESPHome or C++
  dependencies. It is the correct abstraction boundary.

- **The startup HSM** uses `IBridgeServices` to drive the bridge, so it too
  is decoupled from ESPHome internals.

- **ErdRegistry** provides a single place for valid-ERD filtering and string
  type detection, which the adapter uses via a pointer. This is a clean
  dependency injection pattern.
