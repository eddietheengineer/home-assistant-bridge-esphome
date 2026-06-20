# HaDiscoveryManager — Specification

## 1. Overview

### 1.1 Purpose

The HaDiscoveryManager publishes Home Assistant MQTT autodiscovery payloads for the ERDs that the bridge has registered at runtime. It watches for a "ready" signal indicating the bridge has discovered enough ERDs, fetches per-category JSONL entity definitions via HTTPS, filters them against the registered ERD set, and publishes the resulting discovery messages to Home Assistant with rate limiting.

### 1.2 Responsibilities

- Wait for a "ready" signal (quiet window in subscription mode or polling list complete in polling mode)
- Spawn a FreeRTOS background task to fetch per-category JSONL definitions via HTTPS
- Parse JSONL lines into MQTT discovery payloads, filtering against the registered ERD snapshot
- Rate-limited publishing of discovery messages to Home Assistant (50 ms per entity)
- Manage FreeRTOS task lifecycle (create, queue, cleanup)

### 1.3 Not Responsible For

- Determining which ERDs are valid (receives registered ERD set externally)
- Managing bridge lifecycle or MQTT connection state
- Any post-discovery entity updates (discovery is a one-shot operation)
- Building the probe or polling list (see `erd_poll_list_builder`)

---

## 2. Initialization

### 2.1 Primary Init

```cpp
void init(const std::string& base_url,
          const std::string& device_id,
          const std::string& model_number,
          const std::string& serial_number,
          const tiny_erd_t* registered_erds,
          uint16_t registered_erds_count,
          bool generate_device_config);
```

| Parameter | Description |
|-----------|-------------|
| `base_url` | Base URL for JSONL definition files (e.g., `https://example.com/ha_discovery`) |
| `device_id` | Unique device identifier used in MQTT topic paths and discovery payloads |
| `model_number` | Appliance model number (from ERD 0x0001), embedded in device JSON |
| `serial_number` | Appliance serial number (from ERD 0x0002), embedded in device JSON |
| `registered_erds` / `registered_erds_count` | Array of ERDs the bridge has registered. Copied into both `registered_erds_` and `registered_erds_snapshot_` (capped at `HA_DISCOVERY_MAX_ERDS`). The snapshot is the immutable filter used during entity publishing. |
| `generate_device_config` | If `true`, include device metadata in each discovery payload |

Init transitions the state to `HA_DISCOVERY_WAITING_FOR_READY` and sets `last_activity_` and `start_time_` to `millis()`.

### 2.2 Update Registered ERDs

```cpp
void set_registered_erds(const tiny_erd_t* erds, uint16_t count);
```

Replaces both `registered_erds_` and `registered_erds_snapshot_` with a new ERD set. Capped at `HA_DISCOVERY_MAX_ERDS`. Intended to be called before discovery begins (e.g., after feature bit parsing completes but before `run()` triggers publishing).

### 2.3 Set MQTT Adapter

```cpp
void set_mqtt_adapter(esphome_mqtt_client_adapter_t* mqtt_adapter);
```

Sets the typed MQTT adapter pointer for publishing. If `nullptr`, the manager falls back to synchronous publishing or queue-based async publishing depending on the build configuration.

### 2.4 Cleanup

```cpp
void cleanup();
```

Gracefully terminates the FreeRTOS fetch task and frees all associated resources:

1. Sends a `nullptr` sentinel to the queue to signal the fetch task to stop
2. Drains any remaining items from the queue (deletes heap-allocated `HaDiscoveryItem` objects)
3. Re-sends the sentinel (guaranteed space after drain)
4. Waits up to 5 seconds for the task to terminate (polling `task_handle_` with 10 ms delay, resetting the task watchdog)
5. Frees `task_stack_` and `task_tcb_` (heap-allocated in `publish_ha_discovery_`)
6. Deletes the queue

Safe to call multiple times; null guards prevent double-free.

---

## 3. State Machine

```
HA_DISCOVERY_IDLE
  └─ init() → HA_DISCOVERY_WAITING_FOR_READY
       ├─ ready signal received → HA_DISCOVERY_PUBLISHING
       │    └─ all entities published → HA_DISCOVERY_COMPLETE
       └─ fetch/task creation failed → HA_DISCOVERY_FAILED
```

### 3.1 `HA_DISCOVERY_IDLE`

Initial state. Transitions to `HA_DISCOVERY_WAITING_FOR_READY` on `init()`.

### 3.2 `HA_DISCOVERY_WAITING_FOR_READY`

The manager is waiting for a "ready" signal before starting discovery. The `run()` method is called every loop iteration to evaluate readiness.

**Ready signal logic:**

| Bridge Mode | Condition |
|-------------|-----------|
| **Poll mode** (`is_poll_mode == true`) | `polling_list_complete == true` |
| **Subscription mode** (`is_poll_mode == false`) | Quiet window elapsed (`millis() - last_activity_ >= HA_DISCOVERY_QUIET_MS`) **OR** safety cap elapsed (`millis() - start_time_ >= HA_DISCOVERY_MAX_WAIT_MS`). Additionally, if `polling_bridge_initialized` is true, `polling_list_complete` must also be true. |
| **Auto mode** (both bridges active) | Quiet window + polling list complete (both conditions must be satisfied) |

**Quiet window tracking:** `on_erd_seen(erd)` is called when a new ERD is observed. It records the ERD in `seen_erds_` (deduped) and updates `last_activity_` to `millis()`. This resets the quiet window timer.

**Safety cap:** If `millis() - start_time_ >= HA_DISCOVERY_MAX_WAIT_MS`, the manager considers itself ready regardless of quiet window status. This prevents indefinite blocking if the appliance is slow or unresponsive.

### 3.3 `HA_DISCOVERY_PUBLISHING`

Discovery is in progress. The `run()` method drives rate-limited publishing:

```
if (millis() - last_publish_ms_ >= HA_ENTITY_PUBLISH_INTERVAL_MS) {
    last_publish_ms_ = millis();
    publish_next_entity_();
}
```

Each call to `publish_next_entity_()` publishes one discovery payload to MQTT. When all entities are published, the state transitions to `HA_DISCOVERY_COMPLETE`.

### 3.4 `HA_DISCOVERY_COMPLETE`

Terminal state. All discovery payloads have been published. No further action is taken.

### 3.5 `HA_DISCOVERY_FAILED`

Terminal state. Set when a critical failure occurs (e.g., heap allocation failure when spawning the fetch task). No recovery is attempted.

---

## 4. Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `HA_DISCOVERY_QUIET_MS` | 10000 ms | Quiet window in subscription mode — no new ERDs seen for this duration triggers readiness |
| `HA_DISCOVERY_MAX_WAIT_MS` | 30000 ms | Safety cap — maximum time to wait in `WAITING_FOR_READY` before proceeding |
| `HA_ENTITY_PUBLISH_INTERVAL_MS` | 50 ms | Rate limit between entity publishes to avoid overwhelming the MQTT broker |
| `HA_DISCOVERY_MAX_ERDS` | 645 | Maximum number of registered/seen ERDs tracked |

---

## 5. Background Fetch Task

### 5.1 Task Creation

On ESP-IDF builds, `publish_ha_discovery_()` spawns a FreeRTOS task via `xTaskCreateStatic()`:

- **Stack:** heap-allocated `StackType_t` of sufficient size (48 KB)
- **TCB:** heap-allocated `StaticTask_t`
- **Queue:** `xQueueCreate()` for passing `HaDiscoveryItem*` pointers from the fetch task to the main loop
- **Priority:** standard task priority (below main loop)

### 5.2 Heap Safety Checks

Before spawning the fetch task, the manager checks:
- Free heap > minimum threshold (110 KB)
- Largest free block > stack size requirement (48 KB)

If either check fails, the manager transitions to `HA_DISCOVERY_FAILED` and logs a warning. This prevents heap corruption or task watchdog resets due to memory pressure.

### 5.3 Category-Based Fetching

The fetch task determines which JSONL categories to download by scanning the registered ERD snapshot:

| Category | ERD Range |
|----------|-----------|
| `common` | 0x0000–0x0FFF (always fetched) |
| `refrigeration` | 0x1000–0x1FFF |
| `laundry` | 0x2000–0x2FFF |
| `dishwasher` | 0x3000–0x3FFF |
| `waterheater` | 0x4000–0x4FFF |
| `range` | 0x5000–0x5FFF |
| `airconditioning` | 0x7000–0x7FFF |
| `waterfilter` | 0x8000–0x8FFF |
| `smallappliance` | 0x9000–0x9FFF |
| `energy` | 0xD000–0xDFFF |

Only categories that contain at least one registered ERD are fetched. The `common` category is always fetched. Between each category fetch, the task yields for 50 ms.

### 5.4 HTTP Fetch

Each category is fetched via `esp_http_client`:
- **Timeout:** 20 seconds
- **Certificate bundle:** `esp_crt_bundle_attach`
- **Max redirections:** 5
- **Status 404:** treated as success (category not applicable to this device)
- **Status != 200:** treated as failure

### 5.5 JSONL Parsing

Each line of the JSONL response is parsed with `cJSON`:

| JSON Key | Field | Description |
|----------|-------|-------------|
| `i` | ERD ID (hex) | Required — identifies the ERD |
| `n` | Name | Entity display name |
| `o` | Object ID | Home Assistant object_id |
| `t` | Component type | Required — determines MQTT topic prefix |
| `u` | Unit of measurement | Optional |
| `ic` | Icon | Optional |
| `d` | Device class | Optional |
| `e` | Entity category | Optional (config, diagnostic, etc.) |
| `s` | State topic | Optional |
| `c` | Command topic | Optional |
| `on` | Payload on | Optional |
| `of` | Payload off | Optional |
| `a` | Availability topic | Optional |
| `j` | JSON attributes topic | Optional |
| `v` | Value template | Optional |
| `cm` | Command template | Optional |
| `opt` | Options (comma-separated) | Optional — for select entities |
| `r` | Role | `r` = read, `w` = write |
| `p` | Paired ERD (hex) | Optional — paired ERD for request/response pairs |

### 5.6 Entity Filtering

Before building a discovery payload, the entity is filtered against `registered_erds_snapshot_`:

1. If `registered_erds_snapshot_count_ == 0`: accept all entities (no filter)
2. If the entity's ERD is in the snapshot: accept
3. If the entity is a read role (`r == "r"`) and has a paired ERD (`p` is non-empty): check if either the entity's ERD or the paired ERD is in the snapshot — if either matches, accept
4. Otherwise: reject

This ensures only entities for ERDs the device actually supports are published.

### 5.7 Payload Delivery

Each parsed entity is wrapped in a `HaDiscoveryItem` (topic + payload) and delivered:

- If `mqtt_adapter_` is set: publish synchronously via `esphome_mqtt_client_adapter_publish()`
- Otherwise: send to the FreeRTOS queue for async publishing by the main loop

The fetch task sends a `nullptr` sentinel to the queue when all categories are processed, signaling completion.

---

## 6. Rate-Limited Publishing

The `run()` method in `HA_DISCOVERY_PUBLISHING` state publishes one entity per tick, gated by `HA_ENTITY_PUBLISH_INTERVAL_MS` (50 ms). This spreads MQTT publish calls across time to:

- Avoid overwhelming the MQTT broker with burst traffic
- Prevent triggering rate limits on the network or broker
- Keep the main loop responsive (each publish is a blocking operation)

---

## 7. Data Structures

### 7.1 HaDiscoveryItem

```cpp
struct HaDiscoveryItem {
    std::string topic;
    std::string payload;
};
```

A heap-allocated `(topic, payload)` pair passed between the fetch task and the main loop via the FreeRTOS queue.

### 7.2 Member Variables

| Member | Type | Description |
|--------|------|-------------|
| `state_` | `HaDiscoveryState` | Current state of the discovery process |
| `base_url_` | `std::string` | Base URL for JSONL definitions |
| `device_id_` | `std::string` | Unique device identifier |
| `model_number_` | `std::string` | Appliance model number |
| `serial_number_` | `std::string` | Appliance serial number |
| `registered_erds_` | `tiny_erd_t[HA_DISCOVERY_MAX_ERDS]` | Current registered ERD set |
| `registered_erds_count_` | `uint16_t` | Count of registered ERDs |
| `registered_erds_snapshot_` | `tiny_erd_t[HA_DISCOVERY_MAX_ERDS]` | Immutable snapshot taken at init/set — used for entity filtering |
| `registered_erds_snapshot_count_` | `uint16_t` | Count of snapshot ERDs |
| `seen_erds_` | `tiny_erd_t[HA_DISCOVERY_MAX_ERDS]` | ERDs seen during the waiting phase (for quiet window tracking) |
| `seen_erds_count_` | `uint16_t` | Count of seen ERDs |
| `generate_device_config_` | `bool` | Include device metadata in payloads |
| `last_activity_` | `uint32_t` | `millis()` of the last ERD seen (quiet window timer) |
| `last_publish_ms_` | `uint32_t` | `millis()` of the last entity publish (rate limiter) |
| `start_time_` | `uint32_t` | `millis()` when `WAITING_FOR_READY` was entered (safety cap timer) |
| `mqtt_adapter_` | `esphome_mqtt_client_adapter_t*` | Typed MQTT adapter for publishing |
| `queue_` | `QueueHandle_t` | FreeRTOS queue for async publishing (ESP-IDF only) |
| `task_handle_` | `TaskHandle_t` | FreeRTOS task handle (ESP-IDF only) |
| `task_stack_` | `StackType_t*` | Heap-allocated task stack (ESP-IDF only) |
| `task_tcb_` | `StaticTask_t*` | Heap-allocated task TCB (ESP-IDF only) |

---

## 8. Invariants

1. **ESP-IDF only for fetch:** The background fetch task, queue, and HTTP client are only compiled and executed on ESP-IDF builds (`#ifdef USE_ESP_IDF`). On non-ESP-IDF builds, `publish_ha_discovery_()` transitions directly to `COMPLETE` with a debug log.

2. **Entity filtering against registered ERD snapshot:** Entities are filtered against `registered_erds_snapshot_` — the snapshot taken at `init()` or `set_registered_erds()`. Changes to the registered ERD set after discovery begins do not affect the filtering.

3. **No post-discovery updates:** Once in `COMPLETE` or `FAILED` state, the manager takes no further action. Discovery is a one-shot operation.

4. **Seen ERDs only tracked in WAITING_FOR_READY:** `on_erd_seen()` is a no-op in any state other than `WAITING_FOR_READY`. This prevents stale activity from resetting the quiet window after discovery has started.

5. **Deduped seen ERDs:** `on_erd_seen()` checks `seen_erds_` for duplicates before recording, preventing the same ERD from resetting the quiet window multiple times.

6. **ERD array capped at HA_DISCOVERY_MAX_ERDS:** Both `registered_erds_` and `seen_erds_` are capped at 645 entries. Excess entries are silently dropped.

7. **Clean task termination:** `cleanup()` waits for the fetch task to terminate before freeing its stack and TCB, preventing use-after-free. A 5-second timeout prevents indefinite blocking if the task is stuck.

---

## 9. Public API

| Method | Description |
|--------|-------------|
| `init(base_url, device_id, model_number, serial_number, registered_erds, count, generate_device_config)` | Initialize with device info and ERD list. Transitions to `WAITING_FOR_READY`. |
| `set_registered_erds(erds, count)` | Replace the registered ERD set and snapshot. Intended for use before discovery begins. |
| `on_erd_seen(erd)` | Record a new ERD seen. Updates `last_activity_` for quiet window tracking. No-op outside `WAITING_FOR_READY`. |
| `run(is_poll_mode, polling_bridge_initialized, polling_list_complete, subscription_activity_detected)` | Drive the state machine. Called every loop iteration. Evaluates readiness in `WAITING_FOR_READY`; publishes entities in `PUBLISHING`. |
| `set_mqtt_adapter(mqtt_adapter)` | Set the MQTT adapter for publishing. |
| `is_complete()` | Returns `true` if state is `COMPLETE`. |
| `is_failed()` | Returns `true` if state is `FAILED`. |
| `is_publishing()` | Returns `true` if state is `PUBLISHING`. |
| `is_ready_to_start()` | Returns `true` if state is `WAITING_FOR_READY`. |
| `get_state()` | Returns the current `HaDiscoveryState`. |
| `cleanup()` | Free FreeRTOS task, queue, stack, and TCB resources. |

---

## 10. Dependencies

| Dependency | Role |
|------------|------|
| `esp_http_client` (ESP-IDF) | HTTPS fetch of JSONL definition files |
| `cJSON` (ESP-IDF) | JSON parsing of JSONL lines |
| `esp_crt_bundle` (ESP-IDF) | TLS certificate verification |
| `esp_heap_caps` (ESP-IDF) | Heap safety checks before task creation |
| `esp_task_wdt` (ESP-IDF) | Task watchdog reset during cleanup wait loop |
| FreeRTOS (`xTaskCreateStatic`, `xQueueCreate`, `xQueueSend`, `xQueueReceive`) | Background fetch task and inter-task communication |
| `esphome_mqtt_client_adapter` | Typed MQTT publishing interface |
| `tiny_gea3_erd_client` | `tiny_erd_t` type definition |

---

## 11. Known Limitations

1. **Non-ESP-IDF builds skip fetch:** On non-ESP-IDF platforms (e.g., unit tests), the fetch task is not compiled. `publish_ha_discovery_()` transitions directly to `COMPLETE` without publishing any entities. This is by design — the module is not intended to run on non-ESP-IDF platforms in production.

2. **No re-discovery after initial run:** Once the manager reaches `COMPLETE` or `FAILED`, it does not restart. If the bridge needs to re-discover entities (e.g., after appliance re-identification), a new `HaDiscoveryManager` instance must be created and initialized.

3. **Single-shot operation:** The manager is designed for one discovery pass at startup. It does not support incremental updates or re-publishing when the registered ERD set changes after discovery completes.

4. **Heap pressure:** The fetch task requires significant heap (48 KB stack + buffers for HTTP and JSON parsing). If the system is under memory pressure, discovery will fail gracefully but no entities will be published.

5. **Network dependency:** Discovery requires network connectivity to fetch JSONL definitions. If the network is unavailable, the HTTP fetch will timeout (20 s per category), potentially delaying discovery by several minutes.

6. **No retry on fetch failure:** If a category fetch fails (non-200, non-404 status), it is not retried. Entities in that category will be missing from the discovery payloads.
