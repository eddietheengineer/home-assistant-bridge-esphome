# GE Appliances Bridge — Technical Reference

> **Audience**: Systems engineers familiar with embedded C/C++, UART serial buses, and MQTT.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Hardware Interfaces](#2-hardware-interfaces)
3. [Software Architecture](#3-software-architecture)
4. [Configuration Parameters](#4-configuration-parameters)
5. [Startup Sequence](#5-startup-sequence)
6. [Autodiscovery State Machine](#6-autodiscovery-state-machine)
7. [Device ID Generation](#7-device-id-generation)
8. [MQTT Bridge Modes](#8-mqtt-bridge-modes)
9. [Per-Board Bridge Initialization](#9-per-board-bridge-initialization)
10. [Event Handlers](#10-event-handlers)
11. [Auto-Mode Subscription Watchdog](#11-auto-mode-subscription-watchdog)
12. [Data Flow Diagram](#12-data-flow-diagram)
13. [Key Constants and Limits](#13-key-constants-and-limits)
14. [Known Exclusions](#14-known-exclusions)
15. [ESPHome Integration Points](#15-esphome-integration-points)

---

## 1. Overview

`GeappliancesBridge` is an ESPHome `Component` that bridges one or two GE Appliance serial buses (GEA3 and/or GEA2) to MQTT. It:

- **Discovers** appliance boards on the bus automatically after startup.
- **Generates a unique device ID** for the primary bridged board from its appliance type, model number, and serial number ERDs.
- **Publishes ERD (Entity Reference Designator) values** for that primary board to MQTT topics and accepts write commands from MQTT.
- **Currently bridges a single board per bus** via one MQTT bridge instance, even though internal limits such as `MAX_BOARDS = 8` exist.

The component operates entirely within ESPHome's cooperative `loop()` — there are no RTOS tasks or interrupts. All timing is driven by `tiny_timer_group`.

---

## 2. Hardware Interfaces

### GEA3 Bus (primary)
| Parameter | Value |
|-----------|-------|
| Baud rate | 230,400 bps |
| Client address | `0xE4` (the ESP32's own address on the bus) |
| Protocol | GEA3 (RS-485-like, framed by `tiny_gea3_interface`) |
| ERD client timeouts | 250 ms per request, 10 retries |

### GEA2 Bus (optional)
| Parameter | Value |
|-----------|-------|
| Client address | `0xE4` (same as GEA3) |
| Protocol | GEA2 (older GE bus, framed by `tiny_gea2_interface`) |
| ERD client timeouts | 250 ms per request, 0 retries (no automatic retries at ERD client layer) |
| Timer | 1 ms tick events are manually published from within the 200 ms tight loop (no dedicated periodic `tiny_timer`) |

GEA2 is only initialized when `gea2_uart_id` is configured in YAML. If GEA2 is absent, all GEA2 state machines are skipped.

---

## 3. Software Architecture

```
GeappliancesBridge (ESPHome Component)
│
├── setup()
│   ├── Initialize tiny_timer_group
│   ├── Initialize GEA3 UART adapter → GEA3 interface → GEA3 ERD client
│   ├── Subscribe: GEA3 ERD client activity  →  handle_erd_client_activity_()
│   ├── Subscribe: GEA3 raw packet receive   →  handle_gea3_raw_packet_()
│   └── (if gea2_uart configured)
│       ├── Initialize GEA2 UART adapter → GEA2 interface → GEA2 ERD client
│       ├── Subscribe: GEA2 ERD client activity  →  handle_gea2_erd_client_activity_()
│       └── Subscribe: GEA2 raw packet receive   →  handle_gea2_raw_packet_()
│
└── loop()  (called every ESPHome tick, non-blocking)
    ├── Phase 1: run_protocol_stack_()          ← drives UART RX/TX bytes
    ├── Phase 2: run_autodiscovery_()           ← discovery state machine
    ├── Phase 3: run_device_id_generation_()    ← reads appliance type/model/serial
    ├── Phase 4: initialize_mqtt_client_()      ← one-shot when device ID ready
    ├── Phase 5: run_feature_bit_reading_()     ← reads appliance API feature ERDs
    ├── Phase 6: initialize_mqtt_bridge_()      ← one-shot when feature bits + MQTT ready
    ├── Phase 7: check_subscription_activity_() ← auto-mode watchdog
    └── Phase 8: run_ha_discovery_()            ← deferred HA entity publish
```

**Note on FreeRTOS tasks**: When `generate_device_config: true`, Phase 8 spawns a background FreeRTOS task (`ha_fetch`) at priority 1 to download JSONL entity definitions over HTTPS. All other phases run exclusively in ESPHome's cooperative `loop()` with no RTOS tasks or interrupts.

The three library layers under the bridge:

| Layer | Library type | Role |
|-------|-------------|------|
| `esphome_uart_adapter` | ESPHome shim | Feeds UART bytes to/from `tiny_gea*_interface` |
| `tiny_gea3_interface` / `tiny_gea2_interface` | C library | Framing, addressing, CRC |
| `tiny_gea3_erd_client` / `tiny_gea2_erd_client` | C library | Read/write/subscribe ERD requests, queuing, retries |

---

## 4. Configuration Parameters

| YAML key | Setter | Default | Description |
|----------|--------|---------|-------------|
| `gea3_uart_id` | `set_gea3_uart()` | `nullptr` | Optional GEA3 UART (at least one of `gea2_uart_id` / `gea3_uart_id` is required) |
| `gea2_uart_id` | `set_gea2_uart()` | `nullptr` | Optional GEA2 UART (at least one of `gea2_uart_id` / `gea3_uart_id` is required) |
| `device_id` | `set_device_id()` | `""` | Skip auto-generation; use literal string |
| `mode` | `set_mode()` | `AUTO` | `POLL=0`, `SUBSCRIBE=1`, `AUTO=2` |
| `polling_interval` | `set_polling_interval()` | `10000` ms | ERD poll period |
| `polling_onlypublish_onchange` | `set_polling_only_publish_on_change()` | `false` | When `true`, only publish ERD values to MQTT when the value changes (suppress duplicate publishes) |
| `appliance_api_parsing` | `set_appliance_api_parsing()` | `true` | When `true`, reads appliance API feature-bit ERDs (0x0092–0x010D) and restricts polling to only ERDs advertised by the appliance |
| `generate_device_config` | `set_generate_device_config()` | `false` | When `true`, fetches JSONL entity definitions at runtime and publishes Home Assistant MQTT Discovery payloads after ERD enumeration completes |
| `ha_discovery_base_url` | `set_ha_discovery_base_url()` | GitHub raw URL to `ha_discovery/` | Base URL for the per-category JSONL entity definition files. Override to target a different branch, tag, or local server |

The discovered appliance's address becomes `host_address_` and is used for all subsequent ERD reads/writes.

---

## 5. Startup Sequence

```
Power-on / ESPHome boot
       │
       ▼
setup() ──► Hardware init (UART adapters, GEA interfaces, ERD clients, subscriptions)
       │
       ▼
loop() begins — Phase 1: run_protocol_stack_() always active
       │
       ▼
Phase 2: run_autodiscovery_()
       │
       ├── AUTODISCOVERY_WAITING_5S  ← 5-second stabilization delay
       │          │  (5 s elapsed)
       │          ▼
       │   GEA3_BROADCAST_PENDING/WAITING (then GEA2 if GEA3 fails)
       │          │  (appliance responds)
       │          ▼
       │   AUTODISCOVERY_COMPLETE — host_address_ and active_erd_client_ set
       │
       ▼
Phase 3: run_device_id_generation_()
       │
       ├── (device_id configured) ─► final_device_id_ = configured value
       │                              DEVICE_ID_STATE_COMPLETE
       │
       └── (no device_id)
              ├── Read ERD 0x0008 → appliance type name
              ├── Read ERD 0x0001 → model number
              ├── Read ERD 0x0002 → serial number
              └── DEVICE_ID_STATE_COMPLETE
       │
       ▼
Phase 4: initialize_mqtt_client_()  (one-shot, runs when DEVICE_ID_STATE_COMPLETE)
       │
       ├── esphome_mqtt_client_adapter_init() with final_device_id_
       ├── Wire up ha_registered_erds_ callback
       └── Configure string-ERD filter
       │
       ▼
Phase 5: run_feature_bit_reading_()
       │
       ├── Read ERD 0x0092 (common features)
       ├── Read ERDs 0x0093–0x0097 (appliance API groups 0–4)
       ├── Read ERDs 0x0109–0x010D (appliance API groups 5–9)
       ├── Each value published over MQTT immediately
       └── parse_and_log_feature_bits_() → builds appliance_api_valid_erds_
              → bridge_init_state_ = WAITING_FOR_MQTT
       │
       ▼
Phase 6: initialize_mqtt_bridge_()  (one-shot, when BRIDGE_INIT_STATE_WAITING_FOR_MQTT + MQTT ready)
       │
       └── See §8 and §9 for details
       │
       ▼
Phase 7: check_subscription_activity_()  (AUTO mode watchdog)
Phase 8: run_ha_discovery_()             (deferred, when generate_device_config: true)
```

The 5-second delay (`STARTUP_DELAY_MS = 5000`) allows the appliance buses to fully initialize before sending any bus traffic.

---

## 6. Autodiscovery State Machine

The full state machine is:

```
AUTODISCOVERY_WAITING_5S
        │  (5 s elapsed)
        ▼
┌──────────────────────────────────────────────────────────────────────┐
│  GEA3 PATH (when gea3_uart_id configured)                            │
│                                                                      │
│  GEA3_BROADCAST_PENDING                                              │
│  (queue ERD 0x0008 read to 0xFF; retry until queued)                │
│          │ queued                                                    │
│          ▼                                                           │
│  GEA3_BROADCAST_WAITING                                              │
│  (wait 5 s for any ERD response from the bus)                        │
│          │ response received → gea3_board_discovered_ = true        │
│          │ 5 s window expires                                        │
│          │                                                           │
│          ├── board found  → AUTODISCOVERY_COMPLETE                  │
│          ├── no board + GEA2 configured → GEA2_BROADCAST_PENDING    │
│          └── no board + GEA3 only      → GEA3_BROADCAST_PENDING     │
│                                          (retry)                    │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│  GEA2 PATH (when gea2_uart_id configured; entered if GEA3 fails)    │
│                                                                      │
│  GEA2_BROADCAST_PENDING                                              │
│  (queue ERD 0x0008 read to 0xFF via GEA2 ERD client)                │
│          │ queued                                                    │
│          ▼                                                           │
│  GEA2_BROADCAST_WAITING                                              │
│  (wait 5 s for any ERD response from the GEA2 bus)                  │
│          │ response received → gea2_board_discovered_ = true        │
│          │ 5 s window expires                                        │
│          │                                                           │
│          ├── board found  → AUTODISCOVERY_COMPLETE                  │
│          ├── no board + GEA3 configured → GEA3_BROADCAST_PENDING    │
│          └── no board + GEA2 only      → GEA2_BROADCAST_PENDING     │
│                                          (retry)                    │
└──────────────────────────────────────────────────────────────────────┘

AUTODISCOVERY_COMPLETE → host_address_ set, active_erd_client_ selected
                       → start_device_id_generation_()
```

### Broadcast mechanism

Both GEA3 and GEA2 use the same approach: send an ERD `0x0008` (appliance type) read to the broadcast address `0xFF`. The first appliance that responds sets `host_address_` and marks the protocol as discovered. The 5-second window (`AUTODISCOVERY_BROADCAST_WINDOW_MS`) collects the response via the ERD client activity callback (`handle_erd_client_activity_()`). Addresses `0xBE`, `0xBF`, `0xE4` (self), and `0xFF` are excluded from being recorded as valid hosts.

Discovery repeats (alternating protocols if both are configured) until at least one appliance is found.

---

## 7. Device ID Generation

After autodiscovery (`AUTODISCOVERY_COMPLETE`), `start_device_id_generation_()` is called.

### State machine

```
start_device_id_generation_()
        │
        ├── device_id configured? → final_device_id_ = configured value
        │                           DEVICE_ID_STATE_COMPLETE
        │                           start_feature_bit_reading_()
        │
        └── no device_id
              │
              ▼
        DEVICE_ID_STATE_READING_APPLIANCE_TYPE  (ERD 0x0008)
              │ read_completed
              ▼
        DEVICE_ID_STATE_READING_MODEL_NUMBER    (ERD 0x0001, 32-byte string)
              │ read_completed
              ▼
        DEVICE_ID_STATE_READING_SERIAL_NUMBER   (ERD 0x0002, 32-byte string)
              │ read_completed
              ▼
        final_device_id_ = appliance_type_name + "_" + sanitized_model + "_" + sanitized_serial
        DEVICE_ID_STATE_COMPLETE → start_feature_bit_reading_()
```

### ERD reads

Each ERD read is issued from `loop()` (Phase 3) by calling `try_read_erd_with_retry_()`. If the ERD client queue is full, the call returns `false` and is retried on the next `loop()` iteration (up to `MAX_READ_RETRIES = 1000`). When up to `MAX_DEVICE_ID_RESPONSE_RETRIES = 3` consecutive read failures occur for any single ERD, a fallback value is substituted so device ID generation can always complete.

### Device ID format

```
<ApplianceTypeName>_<ModelNumber>_<SerialNumber>
```

- `ApplianceTypeName` is produced by the auto-generated `appliance_type_to_string()` function.
- Model and serial strings are sanitized: characters `+`, `#`, `/`, `$`, ` ` (space), non-printable bytes, and extended ASCII are replaced with `_`.
- If `device_id` was pre-configured in YAML, generation is skipped entirely and the configured string is used directly.

---

## 8. MQTT Bridge Modes

| Mode | `BridgeMode` value | Behaviour |
|------|-------------------|-----------|
| **Polling** | `BRIDGE_MODE_POLL = 0` | `mqtt_bridge_polling_init()` called; ERDs polled every `polling_interval_ms` |
| **Subscription** | `BRIDGE_MODE_SUBSCRIBE = 1` | `mqtt_bridge_init()` called; appliance pushes ERD updates via GEA3 subscription publications |
| **Auto** | `BRIDGE_MODE_AUTO = 2` | Starts in subscription mode; watchdog falls back to polling after 30 s if no subscription publications arrive |

### Auto-mode fallback

In `BRIDGE_MODE_AUTO`, `check_subscription_activity_()` (Phase 7) is called every `loop()` while `subscription_mode_active_ == true`. If no `subscription_publication_received` event from `host_address_` has been observed within 30 seconds:

1. The subscription bridge (`mqtt_bridge_t`) is destroyed via `mqtt_bridge_destroy()`.
2. A polling bridge (`mqtt_bridge_polling_t`) is initialized in its place.
3. `subscription_mode_active_` is set to `false` so the watchdog no longer runs.

---

## 9. Bridge Initialization

`initialize_mqtt_bridge_()` (Phase 6) is called once when all three conditions are true:

- `bridge_init_state_ == BRIDGE_INIT_STATE_WAITING_FOR_MQTT`
- `autodiscovery_state_ == AUTODISCOVERY_COMPLETE`
- MQTT client is connected

It initializes a single MQTT bridge for the discovered appliance:

1. **Applies the valid-ERD filter** (from feature bit parsing) to `mqtt_client_adapter_` when `appliance_api_parsing_` is enabled.
2. **Selects operating mode**: GEA2 always uses polling; otherwise the configured `mode_` is used (`POLL`, `SUBSCRIBE`, or `AUTO`).
3. **Calls** `mqtt_bridge_init()` (subscription) or `mqtt_bridge_polling_init()` (polling) with:
   - The shared `tiny_timer_group_t`
   - The `active_erd_client_` selected during autodiscovery
   - The initialized `mqtt_client_adapter_`
   - `host_address_` — the discovered appliance address
4. **Custom ERD polling**: if `custom_erds_vec_` is non-empty and the primary bridge is in subscription mode, a separate `custom_erd_bridge_` (`mqtt_bridge_polling_t`) is initialized later (after the subscription quiet window) to poll user-configured additional ERDs.
5. **Defers HA discovery**: sets `ha_discovery_pending_ = true` when `generate_device_config_` is enabled.

---

## 10. Event Handlers

### `handle_gea3_raw_packet_(packet)` / `handle_gea2_raw_packet_(packet)`

Called for every raw packet received on the respective bus.

- **During `AUTODISCOVERY_GEA3_BROADCAST_WAITING` / `AUTODISCOVERY_GEA2_BROADCAST_WAITING`**: ignored — the broadcast response arrives via the ERD client callback, not the raw packet handler.
- **All other states**: returns immediately (no-op).

### `handle_erd_client_activity_(args)`

Called for every ERD client event (read, write, subscribe completions; subscription publications; host-came-online). A single handler services both GEA3 and GEA2 via the `gea2_erd_client_adapter_`.

Key responsibilities:

| State / condition | Action |
|-------------------|--------|
| `AUTODISCOVERY_GEA3_BROADCAST_WAITING` + `read_completed` for `ERD_APPLIANCE_TYPE` | Sets `host_address_` and `gea3_board_discovered_ = true` |
| `AUTODISCOVERY_GEA2_BROADCAST_WAITING` + `read_completed` for `ERD_APPLIANCE_TYPE` | Sets `host_address_` and `gea2_board_discovered_ = true` |
| `DEVICE_ID_STATE_READING_*` | Drives device ID generation (appliance type → model → serial); calls `process_device_id_erd_response_()` or `handle_device_id_read_failure_()` |
| `FEATURE_BIT_STATE_IN_FLIGHT` | Drives feature bit reading; calls `process_feature_bit_erd_response_()` or `skip_to_next_feature_erd_()` |
| `BRIDGE_MODE_AUTO` + `subscription_mode_active_` + `subscription_publication_received` from `host_address_` | Sets `subscription_activity_detected_ = true` (suppresses auto-mode fallback to polling) |

---

## 11. Auto-Mode Subscription Watchdog

`check_subscription_activity_()` (Phase 7) runs in `loop()` only when `mode_ == BRIDGE_MODE_AUTO && subscription_mode_active_`.

```
elapsed = millis() - subscription_start_time_

if subscription_activity_detected_:
    return  (subscription is healthy, do nothing)

if elapsed >= 30000 ms:
    mqtt_bridge_destroy(&mqtt_bridge_)
    mqtt_bridge_polling_init(&mqtt_bridge_polling_, ..., host_address_, appliance_type_)
    subscription_mode_active_ = false
    log WARNING: "switched to polling mode"
```

The 30-second timeout uses unsigned 32-bit subtraction, so it wraps correctly when `millis()` overflows approximately every 49.7 days.

---

## 12. Data Flow Diagram

```
Appliance Bus (UART)
        │
        ▼
esphome_uart_adapter  ◄──────────────────────────────────────────►  tiny_gea3_interface (or gea2)
        │                                                                    │
        │                                                    tiny_gea3_erd_client (or gea2 adapter)
        │                                                                    │
        │                                              ERD client activity subscription
        │                                                                    ▼
        │                                               handle_erd_client_activity_()
        │                                                                    │
        │                        ┌──────────────────────────────────────────┤
        │                        │                                          │
        │              Phase 2: Autodiscovery                    Phase 3–5: Device ID +
        │              host_address_ discovered                  feature bits read
        │                        │                                          │
        │                        └──────────────────┬───────────────────────┘
        │                                           │
        │                                           ▼
        │                                 Phase 6: initialize_mqtt_bridge_()
        │                                           │
        │                              ┌────────────┴────────────┐
        │                              │                         │
        │                    mqtt_bridge_ (subscribe)  mqtt_bridge_polling_ (poll)
        │                    or both if custom ERDs    custom_erd_bridge_ (custom ERDs)
        │                              │                         │
        │                              └────────────┬────────────┘
        │                                           │
        │                              esphome_mqtt_client_adapter_
        │                              (device ID = MQTT topic root)
        │                                           │
        ▼                                           ▼
                                            MQTT Broker
                                        (Home Assistant, etc.)
```

---

## 13. Key Constants and Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `STARTUP_DELAY_MS` | `5,000 ms` | Delay after boot before discovery starts |
| `AUTODISCOVERY_BROADCAST_WINDOW_MS` | `5,000 ms` | Duration of each broadcast listen window |
| `SUBSCRIPTION_TIMEOUT_MS` | `30,000 ms` | Auto-mode: time before falling back to polling |
| `MAX_READ_RETRIES` | `1,000` | ERD queue-full retries before giving up |
| `MAX_DEVICE_ID_RESPONSE_RETRIES` | `3` | Consecutive read failures before substituting fallback for a device-ID ERD |
| `LOG_EVERY_N_RETRIES` | `50` | Log retry warnings every N attempts |
| `HA_DISCOVERY_QUIET_MS` | `10,000 ms` | Subscription-mode: quiet window after last new ERD seen before publishing HA discovery |
| `HA_ENTITY_PUBLISH_INTERVAL_MS` | `50 ms` | Minimum interval between successive HA entity publishes |
| GEA3 ERD client request timeout | `250 ms` | Per-request timeout |
| GEA3 ERD client retries | `10` | Retries per request |
| GEA2 ERD client request timeout | `250 ms` | Per-request timeout |
| GEA2 ERD client retries | `0` | No automatic retries at the ERD client layer — bridge-level retries space requests ~500 ms apart, preventing half-duplex collision with queued retry copies |

---

## 14. Known Exclusions

### Addresses always excluded from discovery

| Address | Reason |
|---------|--------|
| `0xE4` | The bridge's own address (self) |
| `0xFF` | GEA broadcast address |
| `0xBE` | Known non-appliance device (e.g., gateway/controller) |
| `0xBF` | Known non-appliance device |

### Single-board bridging

The current implementation bridges one appliance at a time — the first board that responds during autodiscovery. Internal data structures (`mqtt_bridge_t`, `mqtt_bridge_polling_t`, `mqtt_client_adapter_`) are single instances, not arrays. Multi-appliance support is listed as a future goal in GOALS.md.

### No unsubscribe mechanism

The GEA3 protocol has no unsubscribe command. Subscriptions expire on the appliance side only when the client stops sending `retain_subscription` keepalives (which takes several seconds). This means subscription publications may arrive during the autodiscovery window, but the Phase 2 state handler explicitly ignores them (only `read_completed` / `read_failed` for `ERD_APPLIANCE_TYPE` advance the state machine), so they cause no harm.

---

## 15. ESPHome Integration Points

| ESPHome hook | What the bridge does |
|-------------|----------------------|
| `setup()` | Initializes all hardware and event subscriptions |
| `loop()` | Advances all state machines and timer groups — must run every tick |
| `dump_config()` | Logs current device ID, addresses, mode, polling interval to the ESPHome log |
| `get_setup_priority()` | Returns `setup_priority::DATA` (600) — runs after UART (600) and MQTT (50) setup |

MQTT connectivity is detected by polling `mqtt::global_mqtt_client->is_connected()` each loop. A rising edge (disconnected → connected) triggers `on_mqtt_connected_()`. There is no explicit disconnect handler in the loop; instead, `notify_mqtt_disconnected_()` is called proactively from `on_mqtt_connected_()` to force all MQTT adapters to re-register their ERD subscriptions after a reconnect.
