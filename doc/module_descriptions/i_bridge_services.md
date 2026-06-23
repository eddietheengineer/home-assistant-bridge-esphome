# IBridgeServices

## Purpose

Define the contract between the startup state machine and the bridge so the HSM can drive startup phases without a compile-time dependency on `GeappliancesBridge`'s internals.

## Interface

`IBridgeServices` is a pure abstract class (all methods are `virtual = 0`). `GeappliancesBridge` implements this interface. The startup HSM holds a pointer to `IBridgeServices` through `startup_hsm_wrapper_t` and invokes methods through it, never directly referencing `GeappliancesBridge`.

### Autodiscovery

| Method | Description |
|--------|-------------|
| `run_autodiscovery()` | Run one tick of the autodiscovery manager. |
| `is_autodiscovery_complete()` | Returns `true` once autodiscovery has found (or given up on) an appliance. |
| `get_discovered_host_address()` | Host address of the discovered appliance (valid when `is_autodiscovery_complete()`). |
| `is_discovered_gea2_protocol()` | `true` if the discovered appliance uses the GEA2 protocol. |

### Device ID

| Method | Description |
|--------|-------------|
| `init_device_id_reading()` | Initialize device-ID reading (idempotent when already complete). |
| `is_device_id_complete()` | Returns `true` once the device ID has been determined. |

### MQTT Client Adapter

| Method | Description |
|--------|-------------|
| `is_mqtt_client_initialized()` | Returns `true` if the MQTT client adapter has been initialized. |
| `initialize_mqtt_client()` | Initialize the MQTT client adapter (idempotent). |

### Feature Bits

| Method | Description |
|--------|-------------|
| `start_feature_bit_reading()` | Begin the feature-bit reading sequence (self-driving, no further polling needed). |
| `is_feature_bits_complete()` | Returns `true` once all feature-bit ERDs have been read and parsed, or if the manager entered `FEATURE_BIT_STATE_FAILED`. |

### Bridge Initialization

| Method | Description |
|--------|-------------|
| `is_bridge_initialized()` | Returns `true` if the ERD bridge (poll/subscribe) has been initialized. |
| `initialize_erd_bridge()` | Initialize the ERD bridge in the mode selected by configuration. |

### Operating Mode

| Method | Description |
|--------|-------------|
| `get_mode()` | Returns the current `BridgeMode`. |
| `get_subscription_state()` | Returns the current `subscription_state_t`. |
| `get_polling_state()` | Returns the current `polling_state_t`. |

### Startup Delay

| Method | Description |
|--------|-------------|
| `record_startup_delay_start()` | Record the timestamp when the startup delay phase begins. |
| `is_startup_delay_elapsed()` | Returns `true` if the startup delay has elapsed. |

### Recurring Tasks

| Method | Description |
|--------|-------------|
| `handle_subscription_failed()` | Called when the subscription bridge enters the failed state; triggers fallback to polling mode in AUTO mode. |
| `handle_polling_failed()` | Called when the polling bridge enters the failed state while running alongside a subscription bridge; cleans up the polling bridge. |
| `maybe_start_custom_erd_polling()` | Start custom-ERD polling bridge if conditions are met (idempotent). |
| `log_poll_state_transitions()` | Log any pending polling-bridge state-name transitions. |
| `run_all_managers()` | Run one tick of all managers (autodiscovery, device-ID, feature bits). |

### ERD Cache MQTT Publisher

| Method | Description |
|--------|-------------|
| `initialize_erd_cache_publisher()` | Initialize the ERD cache MQTT publisher (idempotent). |
| `is_erd_cache_publisher_initialized()` | Returns `true` if the ERD cache publisher has been initialized. |

## Dependencies

- `bridge_mode.h` — `BridgeMode` enum

## Key Design Decisions

- **container_of for back-pointer**: The `IBridgeServices` pointer is stored in `startup_hsm_wrapper_t` alongside the HSM. `services_from_hsm()` uses `container_of` to recover the wrapper from the HSM pointer, making the HSM reentrant — each wrapper instance carries its own services pointer. Same pattern as `erd_write_bridge_t` and `erd_bridge_poll_t`.
- **Phase-transition queries**: Each phase has an `is_X_complete()` method so the HSM can check completion without internal state knowledge.
- **Idempotent init methods**: `init_device_id_reading()`, `initialize_mqtt_client()`, and `initialize_erd_cache_publisher()` are safe to call multiple times.
- **No state ownership**: The interface does not own any state — it is purely a contract for method invocation.

## Testing

Exercised through the startup HSM integration tests in `test/tests/startup_hsm_test.cpp`.