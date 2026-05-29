# GeappliancesBridge Startup HSM

## Purpose

Hierarchical state machine that drives the linear startup sequence of the GE Appliances bridge. Replaces the previous manual switch-based phase progression with a proper `tiny_hsm`-based state machine where each phase handles its own entry/exit logic and waits for signals from managers before transitioning.

## Public API

| Function | Description |
|----------|-------------|
| `set_bridge_services(services)` | Set the `IBridgeServices` back-pointer used by all HSM state functions |
| `services_from_hsm(hsm)` | Retrieve the `IBridgeServices` pointer from an HSM pointer |

## Signals

| Signal | Description |
|--------|-------------|
| `signal_run_loop` | Drive ongoing work in the current state (sent every `loop()`) |
| `signal_autodiscovery_complete` | Autodiscovery found (or gave up on) appliance |
| `signal_device_id_complete` | Device ID ready (read or pre-configured) |
| `signal_device_id_failed` | Device ID read failed, using fallback |
| `signal_mqtt_connected` | MQTT broker connection established |
| `signal_feature_bits_complete` | All feature bit ERDs read and parsed |
| `signal_bridge_ready` | MQTT bridge (poll/subscribe) initialized |
| `signal_subscription_fallback` | AUTO mode: subscription timed out, fell back to polling |

## State Machine

Flat hierarchy — all states defer unhandled signals to `startup_state_top`.

```
startup_state_top (root — handles entry/exit, defers all other signals)
  │
  ├─ startup_state_protocol_stack
  │    └─ entry → immediately transition to autodiscovery
  │
  ├─ startup_state_autodiscovery
  │    ├─ run_loop: run AutodiscoveryManager (retries indefinitely)
  │    └─ complete/signal → device_id
  │
  ├─ startup_state_device_id
  │    ├─ entry: init DeviceIdentityManager, start timeout timer
  │    ├─ run_loop: run manager, check timeout (30 s)
  │    └─ complete/failed/signal → mqtt_client_init
  │
  ├─ startup_state_mqtt_client_init
  │    └─ entry: init MQTT adapter, start feature bit reading → feature_bits
  │
  ├─ startup_state_feature_bits
  │    ├─ run_loop: run FeatureBitManager, check both feature bits AND MQTT connected
  │    ├─ timeout: 60 s — continue without feature filtering
  │    └─ feature_bits_complete + mqtt_connected → bridge_init
  │
  ├─ startup_state_bridge_init
  │    ├─ run_loop: if MQTT connected + autodiscovery complete, init bridge
  │    └─ mqtt_connected/signal → subscription_watch
  │
  ├─ startup_state_subscription_watch
  │    ├─ non-AUTO modes: skip to ha_discovery
  │    ├─ AUTO mode: monitor subscription activity, fall back to polling
  │    └─ fallback or non-AUTO → ha_discovery
  │
  ├─ startup_state_ha_discovery
  │    ├─ run_loop: run HaDiscoveryManager
  │    └─ transition to running (HA discovery runs in background)
  │
  └─ startup_state_running (steady state)
       └─ run_loop: run all managers (autodiscovery, device identity,
           feature bits, ha discovery), check subscription activity
           (AUTO mode), maybe start custom ERD polling, log poll state
           transitions
```

## Dependencies

- `tiny_hsm` — hierarchical state machine framework
- `IBridgeServices` — abstract contract implemented by `GeappliancesBridge`; the HSM is isolated from the concrete class (accessed via `services_from_hsm()`)
- ESPHome `mqtt::global_mqtt_client` — MQTT connection state check in feature_bits / bridge_init states

## Key Design Decisions

- **Back-pointer instead of container_of**: The bridge is a non-POD C++ class (virtual methods, inheritance), so `offsetof` is conditionally-supported. A static global pointer (`g_bridge_instance`) is used instead.
- **Flat hierarchy**: All states are children of `startup_state_top` with no intermediate parent states. This simplifies signal routing — any unhandled signal bubbles to the top and is deferred.
- **Signal-driven transitions**: Each state waits for specific signals from its manager rather than polling for completion. This allows the `run_loop` signal to drive ongoing work while signals trigger transitions.
- **Phase timeouts**: Device ID (30 s) and feature bits (60 s) phases have timeouts to prevent indefinite stalls. On timeout, the bridge continues with fallback values.
- **Feature bits + MQTT gate**: The `feature_bits` state waits for BOTH feature bit completion AND MQTT connection before transitioning to `bridge_init`. Either signal alone can trigger the check.

## Testing

Covered by integration tests in `test/tests/` through the full startup sequence. State transitions are tested implicitly as the bridge progresses through each phase.
