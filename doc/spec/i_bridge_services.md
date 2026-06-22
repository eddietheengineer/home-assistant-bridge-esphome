# IBridgeServices — Specification

## 1. Overview

### 1.1 Purpose

`IBridgeServices` defines the contract between the startup state machine (`GeappliancesBridgeStartupHSM`) and the bridge implementation (`GeappliancesBridge`). It allows the HSM to drive startup phases — autodiscovery, device-ID reading, feature-bit reading, MQTT client initialization, and bridge initialization — without a compile-time dependency on `GeappliancesBridge`'s internals.

### 1.2 Responsibilities

- Declare every operation the startup HSM is allowed to invoke on the bridge
- Provide phase-transition queries (`is_X_complete`, `is_X_initialized`) so the HSM can check completion without internal state knowledge
- Provide phase-timing helpers (`record_*_start`, `is_*_elapsed`) for delay-based transitions
- Group recurring-work dispatch methods so the running state stays clean
- Expose the current operating mode (`get_mode`) and subscription bridge state (`get_subscription_state`)

### 1.3 Not Responsible For

- Implementing any phase logic — `GeappliancesBridge` does that
- Owning any state — the interface is purely a contract for method invocation
- Anything not called by `geappliances_bridge_startup_hsm.cpp`

---

## 2. Interface

`IBridgeServices` is a pure abstract class. Every method is declared `virtual` with `= 0`. The single concrete implementation is `GeappliancesBridge`.

```cpp
class IBridgeServices {
 public:
  virtual ~IBridgeServices() = default;
  // ... all methods virtual = 0
};
```

The startup HSM holds a pointer to `IBridgeServices` and calls methods through the interface. The back-pointer from the interface to the concrete implementation is provided via a static global `g_bridge_instance`, used when the HSM needs to access implementation-specific state not exposed through the interface.

---

## 3. Method Groups

### 3.1 Autodiscovery

| Method | Description |
|--------|-------------|
| `run_autodiscovery()` | Run one tick of the autodiscovery manager. |
| `is_autodiscovery_complete()` | Returns `true` once autodiscovery has found (or given up on) an appliance. |
| `get_discovered_host_address()` | Host address of the discovered appliance. Valid when `is_autodiscovery_complete()` returns `true`. |
| `is_discovered_gea2_protocol()` | Returns `true` if the discovered appliance uses the GEA2 protocol. |

### 3.2 Device ID

| Method | Description |
|--------|-------------|
| `init_device_id_reading()` | Begin reading device-identification ERDs. Idempotent — safe to call multiple times. |
| `is_device_id_complete()` | Returns `true` once device-ID reading is complete. |

### 3.3 MQTT Client

| Method | Description |
|--------|-------------|
| `is_mqtt_client_initialized()` | Returns `true` if the MQTT client has been initialized. |
| `initialize_mqtt_client()` | Initialize the MQTT client. Idempotent — safe to call multiple times. |

### 3.4 Feature Bits

| Method | Description |
|--------|-------------|
| `start_feature_bit_reading()` | Start the feature-bit ERD read sequence. |
| `is_feature_bits_complete()` | Returns `true` once all feature bits have been read and parsed. |

### 3.5 Bridge Init

| Method | Description |
|--------|-------------|
| `is_bridge_initialized()` | Returns `true` if the ERD bridge (polling or subscription) has been initialized. |
| `initialize_erd_bridge()` | Initialize the ERD bridge (polling or subscription mode). |

### 3.6 Operating Mode

| `get_mode()` | Returns the current `BridgeMode` (POLL, SUBSCRIBE, or AUTO). |
| `get_subscription_state()` | Returns the current subscription bridge state as a `subscription_state_t` enum: `subscription_state_none`, `subscription_state_subscribing`, `subscription_state_subscribed`, `subscription_state_steady`, or `subscription_state_failed`. Callers derive `is_subscription_mode_active` from this: active when the value is not `subscription_state_none` and not `subscription_state_failed`. |

### 3.7 Startup Delay

| Method | Description |
|--------|-------------|
| `record_startup_delay_start()` | Record the timestamp when the startup delay phase begins. |
| `is_startup_delay_elapsed()` | Returns `true` if the configured startup delay has elapsed. |

### 3.8 Recurring Tasks

| Method | Description |
|--------|-------------|
| `handle_subscription_failed()` | Called when the subscription bridge enters the failed state; triggers fallback to polling mode in AUTO mode. |
| `handle_polling_failed()` | Called when the polling bridge enters the failed state; cleans up the polling bridge in dual-bridge mode or logs in POLL-only mode. |
| `maybe_start_custom_erd_polling()` | Start custom-ERD polling bridge if conditions are met. Idempotent. |
| `log_poll_state_transitions()` | Log any pending polling-bridge state-name transitions. |
| `run_all_managers()` | Run one tick of all managers (autodiscovery, device-ID, feature bits). |

### 3.9 ERD Cache Publisher

| Method | Description |
|--------|-------------|
| `initialize_erd_cache_publisher()` | Initialize the ERD cache MQTT publisher. Idempotent. |
| `is_erd_cache_publisher_initialized()` | Returns `true` if the ERD cache publisher has been initialized. |

---

## 4. State Machine

The interface itself has no state machine. It is a passive contract — the startup HSM drives state transitions by calling methods on the interface and checking completion queries. The state machine lives in `GeappliancesBridgeStartupHSM`, which uses `IBridgeServices` as its dependency.

The HSM startup sequence is:

```
startup_delay
  └─ autodiscovery
      └─ device_id
          └─ mqtt_client_init
              └─ feature_bits
                  └─ bridge_init
                      └─ erd_cache_publisher_init
                          └─ running (recurring tasks)
```

Each phase completes when the corresponding `is_X_complete()` or `is_X_initialized()` returns `true`.

---

## 5. Data Structures

The interface declares no data members. All state is owned by `GeappliancesBridge`.

The only shared artifact is a static global pointer:

```cpp
static IBridgeServices* g_bridge_instance;
```

This provides the back-pointer from the interface to the concrete `GeappliancesBridge` instance, used when the HSM needs to access implementation-specific state not exposed through the interface.

---

## 6. Invariants

1. **No state ownership:** `IBridgeServices` owns no state. It is a pure interface — all data lives in `GeappliancesBridge`.
2. **Idempotent init methods:** `init_device_id_reading()`, `initialize_mqtt_client()`, and `initialize_erd_cache_publisher()` are safe to call multiple times. They guard against re-initialization internally.
3. **Back-pointer via `g_bridge_instance`:** The static global pointer provides access from the interface to the concrete implementation. It is set during `GeappliancesBridge` construction.
4. **Const correctness:** All query methods (`is_*`, `get_*`) are `const` — they do not mutate state.
5. **Single implementation:** Only `GeappliancesBridge` implements `IBridgeServices`. The interface is not designed for multiple implementations.

---

## 7. Dependencies

| Dependency | Role |
|------------|------|
| `bridge_mode.h` | `BridgeMode` enum (POLL, SUBSCRIBE, AUTO). Shared type to avoid circular dependency between `IBridgeServices` and `GeappliancesBridge`. |

---

## 8. Known Limitations

1. **Static global back-pointer:** `g_bridge_instance` is a static global pointer, not thread-safe. This is acceptable because the entire bridge runs in a single-threaded ESP32 FreeRTOS task context.
2. **Non-POD implementation:** `GeappliancesBridge` is a non-POD C++ class (virtual methods, inheritance), so `offsetof` is conditionally-supported. A static global pointer is used instead of `container_of`-style pointer arithmetic.
3. **Single implementation:** The interface is designed for exactly one implementation. Adding a second implementation would require refactoring `g_bridge_instance` to support multiple instances.
4. **No compile-time enforcement of phase order:** The HSM enforces phase ordering at runtime. The interface itself does not prevent calling methods out of order (e.g., calling `initialize_erd_bridge()` before `is_autodiscovery_complete()`).
