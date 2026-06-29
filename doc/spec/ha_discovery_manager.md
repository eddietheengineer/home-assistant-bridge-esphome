# HA Discovery Manager — Specification

## 1. Overview

### 1.1 Purpose

The HA Discovery Manager generates and publishes Home Assistant MQTT discovery payloads for GE Appliances devices. It decompresses embedded JSONL entity definitions (stored as zlib-compressed data in `ha_discovery_data.h`), parses each line to extract entity metadata, builds a valid Home Assistant discovery payload, and publishes it to the appropriate `homeassistant/{domain}/{device_id}/{entity_id}/config` topic with `retain=true`.

Discovery runs once at device startup. The manager processes categories relevant to the appliance type, filtering out entities whose ERDs are not registered in the device's ERD cache.

### 1.2 Responsibilities

- Build a sorted ERD list from the cache for binary-search filtering
- Decompress embedded JSONL chunks sequentially (one at a time into a shared buffer)
- Parse JSONL lines and extract entity fields (name, domain, ERD ID, templates, etc.)
- Filter entities by checking ERD registration (binary search on sorted array)
- Build Home Assistant discovery payloads with proper JSON escaping
- Publish discovery payloads to MQTT with rate limiting (50 ms interval)
- Track discovery progress (categories, chunks, lines) with cooperative yielding
- Transition through well-defined states: IDLE → BUILDING → DISCOVERING → COMPLETE/FAILED
- Manage embedded cleanup module for removing stale discovery topics

### 1.3 Not Responsible For

- ERD cache lifecycle (owned by `GeappliancesBridge`)
- MQTT connection lifecycle (owned by `EsphomeMqttClientAdapter`)
- Generating or updating ERD data values (handled by `erd_cache_mqtt_publisher`)
- Determining which ERDs are registered (set by polling/subscription bridges)
- Parsing the raw JSONL schema (entity definitions are pre-compiled into embedded data)

---

## 2. State Machine

### 2.1 States

```c
typedef enum {
  ha_discovery_state_idle,        // Initial state after init()
  ha_discovery_state_building,    // Build task running (ESP-IDF only)
  ha_discovery_state_discovering, // Main loop decompressing/publishing
  ha_discovery_state_complete,    // All entities published
  ha_discovery_state_failed       // Error during build or discovery
} ha_discovery_state_t;
```

### 2.2 Transitions

```
IDLE ──start()──> BUILDING ──build done──> DISCOVERING ──all categories done──> COMPLETE
  │                     │                          │
  │                     │                          └──decompress fail──> FAILED
  │                     │
  └──start() fail───────┘
```

- **IDLE → BUILDING**: `start()` creates the build task on ESP-IDF. On non-ESP-IDF, transitions directly to COMPLETE.
- **BUILDING → DISCOVERING**: `run()` detects the build task finished (via `done_sem`), transitions state, and initializes discovery counters.
- **DISCOVERING → COMPLETE**: `run()` finishes all categories and chunks.
- **BUILDING → FAILED**: `start()` fails to allocate task stack/TCB.
- **DISCOVERING → FAILED**: `chunk_decompress()` fails for any chunk.

### 2.3 State Guards

- `start()` only proceeds from IDLE; calls in other states are no-ops.
- `run()` only proceeds from BUILDING or DISCOVERING; calls in other states return immediately.

---

## 3. Initialization

### 3.1 Init

```c
void ha_discovery_manager_init(ha_discovery_manager_t* self);
```

`init()` MUST:
- Zero the struct with `memset`
- Set `state = ha_discovery_state_idle`
- Set `get_time_ms = esphome::millis`
- On ESP-IDF:
  - Delete any existing `done_sem` to prevent leaks on re-init
  - Create `done_sem` via `xSemaphoreCreateBinary()`
  - If semaphore creation fails, log error but continue (degraded mode)
  - Set `task_running = false`
  - Initialize embedded `cleanup` module via `ha_discovery_cleanup_init()`

### 3.2 Configure

```c
void ha_discovery_manager_configure(
  ha_discovery_manager_t* self,
  const char* device_id,
  const char* model_number,
  const char* serial_number,
  uint8_t appliance_type,
  bool filter_config_topics,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client);
```

| Parameter | Description |
|-----------|-------------|
| `device_id` | Device ID string used in topic construction and unique IDs. |
| `model_number` | Model number for device info in discovery payloads (may be NULL). |
| `serial_number` | Serial number for device info in discovery payloads (may be NULL). |
| `appliance_type` | ERD 0x0008 appliance type value, used for category filtering. |
| `filter_config_topics` | Whether to filter discovery topics (stored for future use). |
| `cache` | Shared ERD cache for building the sorted ERD list. |
| `mqtt_client` | MQTT client interface for publishing discovery payloads. |

`configure()` stores all parameters directly; no validation or deep copy.

### 3.3 Start

```c
void ha_discovery_manager_start(ha_discovery_manager_t* self);
```

On ESP-IDF:
- Guard against non-IDLE state
- Allocate `task_stack` (4096 bytes) and `task_tcb` (sizeof(StaticTask_t)) via `heap_caps_malloc(MALLOC_CAP_8BIT)`
- If allocation fails: log warning, free any partial allocation, set state to FAILED
- Create the build task via `xTaskCreateStatic()` with name `"ha_discovery_build"`, priority 1
- If task creation fails: log error, free stack/TCB, set state to FAILED
- Set `task_running = true` and `state = ha_discovery_state_building`

On non-ESP-IDF:
- Set `state = ha_discovery_state_complete` (no discovery work to do)

### 3.4 Cleanup

```c
void ha_discovery_manager_cleanup(ha_discovery_manager_t* self);
```

On ESP-IDF:
- If `task_handle` is non-NULL:
  - Set `task_running = false`
  - Wait for `done_sem` with 1000 ms timeout
  - Reset WDT and yield 100 ms for idle task to unlink TCB
  - Free `task_stack` and `task_tcb` (safe to call `free(NULL)`)
  - Set `task_handle = NULL`
- Call `cleanup_resources()` to delete `done_sem` and free any remaining task resources
- Call `ha_discovery_cleanup_destroy()` on the embedded cleanup module
- Zero the struct with `memset`

On non-ESP-IDF:
- Zero the struct with `memset`

### 3.5 Run

```c
void ha_discovery_manager_run(ha_discovery_manager_t* self);
```

Called from the main loop. See §5 for the full run loop behavior.

On non-ESP-IDF: no-op.

### 3.6 Query Functions

```c
bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self);
```

Returns `true` if state is BUILDING or DISCOVERING.

```c
ha_discovery_state_t ha_discovery_manager_get_state(ha_discovery_manager_t* self);
```

Returns the current state.

```c
void ha_discovery_manager_set_time_fn(
  ha_discovery_manager_t* self,
  uint32_t (*get_time_ms)(void));
```

Override the time source (defaults to `esphome::millis`). Used for testing to control time progression during rate limiting.

---

## 4. Architecture

### 4.1 ESP-IDF: Build Task + Main Loop Discovery

On ESP-IDF, discovery is split into two phases:

**Build phase (background task):**
- **Stack**: 4096 bytes, heap-allocated via `heap_caps_malloc(MALLOC_CAP_8BIT)`
- **Priority**: 1
- **Name**: `"ha_discovery_build"`
- **Work**: Builds the sorted ERD list from the cache and pre-builds the device JSON string
- **Completion**: Signals `done_sem` and calls `vTaskDelete(NULL)`
- **Stack watermark**: Logged at task exit for capacity verification

**Discovery phase (main loop):**
- `run()` is called from the main loop
- First call in BUILDING state: waits for build task completion, frees task resources, transitions to DISCOVERING
- Subsequent calls in DISCOVERING state: decompresses chunks and publishes entities incrementally
- Cooperative yielding: `vTaskDelay(1)` after each entity, `vTaskDelay(2)` every 5 entities, `vTaskDelay(10)` after each chunk

### 4.2 Non-ESP-IDF: No-Op

On non-ESP-IDF platforms, `start()` transitions directly to COMPLETE and `run()` is a no-op. Discovery is not supported in simulator/test builds without ESP-IDF stubs.

---

## 5. Data Flow

### 5.1 Embedded Data Structure

Discovery entity definitions are stored as compressed JSONL in `ha_discovery_data.h`:

```
ha_discovery_categories[] (10 categories)
  └── ha_discovery_category_t
        ├── name: "common", "refrigeration", "laundry", ...
        ├── data: pointer to compressed bytes
        ├── chunks: ha_discovery_chunk_t[] (offset + compressed size)
        ├── num_chunks: number of chunks
        └── max_decompressed_chunk: max decompressed size for any chunk
```

Each category's data is zlib-compressed and split into one or more chunks. Chunk descriptors specify the byte offset and compressed size within the category's data array.

**Categories and compression ratios:**

| Category | Chunks | Compressed | Decompressed | Ratio |
|----------|--------|------------|--------------|-------|
| common | 1 | 2,124 B | 7,023 B | 3.3× |
| refrigeration | 24 | 32,752 B | 320,080 B | 9.8× |
| laundry | 42 | 80,771 B | 554,481 B | 6.9× |
| dishwasher | 4 | 7,388 B | 46,074 B | 6.2× |
| waterheater | 2 | 3,671 B | 25,617 B | 7.0× |
| range | 45 | 59,563 B | 611,235 B | 10.3× |
| airconditioning | 5 | 10,784 B | 67,408 B | 6.3× |
| waterfilter | 1 | 604 B | 1,664 B | 2.8× |
| smallappliance | 12 | 14,576 B | 154,869 B | 10.6× |
| energy | 1 | 896 B | 5,505 B | 6.1× |

### 5.2 Processing Pipeline

```
Category iteration → Chunk decompression → Line parsing → Entity filtering → Payload building → MQTT publish
```

1. **Category iteration**: Walk `ha_discovery_categories[]` from index 0 to `ha_discovery_category_count - 1`
2. **Category filtering**: `should_process_category()` checks if the category is relevant for the configured `appliance_type`
3. **Chunk decompression**: For each chunk in the category, decompress into `decomp_buf` (14 KB) using `miniz/tinfl`
4. **Line parsing**: Scan the decompressed buffer for newline-delimited JSON lines, copying each into `line_buf` (14 KB)
5. **Entity filtering**: `process_jsonl_line()` extracts ERD ID and checks registration via binary search on the sorted ERD array
6. **Payload building**: Build the Home Assistant discovery payload in `payload_buf` (8 KB) with proper JSON escaping
7. **MQTT publish**: Publish to `homeassistant/{domain}/{device_id}/{entity_id}/config` with `retain=true`
8. **Rate limiting**: Enforce 50 ms minimum interval between publishes; if not enough time has elapsed, return from `run()` to yield to the main loop

### 5.3 Chunk Processing

The manager processes one chunk at a time into the shared `decomp_buf`. Once a chunk is fully consumed (all lines parsed), it decompresses the next chunk into the same buffer — no need to keep all decompressed data in memory simultaneously.

**Progress tracking:**
- `current_category`: index into `ha_discovery_categories[]`
- `current_chunk`: index into current category's `chunks[]`
- `current_offset`: byte offset within the current decompressed chunk
- `current_decomp_size`: size of the current decompressed chunk

When `current_decomp_size == 0`, the next chunk needs decompression. When `current_offset >= current_decomp_size`, the chunk is done and the manager advances to the next chunk.

### 5.4 JSONL Line Processing

`process_jsonl_line()` performs the following steps:

1. **Extract required fields**: `i` (ERD ID hex), `n` (entity name), `d` (domain)
2. **Extract optional fields**: `fi` (field ID), `p` (paired ERD), `r` (role), `u` (unit), `dc` (device class), `sc` (state class), `o` (options), `dt` (data type), `sf` (scale factor), `m` (mode), `pon` (payload on), `poff` (payload off), `son` (state on), `soff` (state off), `mn` (min), `mx` (max), `st` (step)
3. **ERD registration check**: Binary search the sorted ERD array; if not found, increment `total_filtered` and return `false`
4. **Paired ERD check**: If a paired ERD is specified, verify it is also registered
5. **Build unique ID**: `{device_id}_erd_{erd_id_hex}[_{field_id}]`
6. **Build topics**: state topic `geappliances/{device_id}/erd/0x{erd_id}/value`, command topic `geappliances/{device_id}/erd/0x{erd_id}/write`
7. **Paired entity topic swap**: For paired entities with `role="request"`, swap state and command topics between the primary and paired ERDs
8. **Build discovery topic**: `homeassistant/{domain}/{device_id}/{erd_id}[_{field_id}]/config` using a cached domain prefix
9. **Build payload**: Construct the JSON payload with all applicable fields, embedding `value_template` and `command_template` directly from the raw JSONL line
10. **Button domain special case**: Buttons omit `state_topic` and `value_template`, using `payload_press="1"` instead

**JSON helpers:**
- `json_get_str()`: Extracts a value for a given key from a JSON string, handling string values, objects, and arrays
- `json_unescape()`: Unescapes JSON string values (`\"`, `\\`, `\n`, `\r`, `\t`, `\uXXXX`)
- `json_reescape()`: Copies a raw JSON value for embedding in another JSON string (no transformation needed — escape sequences remain valid)

---

## 6. Category Filtering

### 6.1 should_process_category()

Determines which categories to process based on `appliance_type` (ERD 0x0008). The `common` category is always processed. Each appliance type maps to one or more specific categories plus the `energy` category:

| Appliance Types | Categories |
|----------------|------------|
| 0 (WaterHeater) | waterheater, energy |
| 1, 2, 23, 45, 46 (Laundry) | laundry, energy |
| 3, 24, 25, 48, 51 (Refrigeration) | refrigeration, energy |
| 4, 5, 7–9, 11–13, 15, 17, 19, 34, 39–41, 43, 47, 49, 54 (Range/Cooking) | range, energy |
| 6, 32 (Dishwasher) | dishwasher, energy |
| 10, 14, 20, 22, 30, 31, 35, 36, 44, 52, 56 (Air Conditioning) | airconditioning, energy |
| 16, 21 (Water Filter) | waterfilter, energy |
| 18, 26–29, 33, 37, 38, 42, 50, 53, 55 (Small Appliance) | smallappliance, energy |

Appliance types not in any group (e.g., 42=SmartPlug) only get the `common` category.

---

## 7. Domain Mapping

### 7.1 Home Assistant Domains

The manager supports 21 Home Assistant domains, defined in `HA_DOMAIN_STRINGS[]`:

`alarm_control_panel`, `binary_sensor`, `button`, `camera`, `climate`, `cover`, `date`, `datetime`, `event`, `fan`, `light`, `lock`, `number`, `select`, `sensor`, `switch`, `text`, `time`, `update`, `vacuum`, `valve`

The domain for each entity comes from the JSONL data (field `d`), not from ERD ID mapping. The `ha_domain_to_index()` helper converts a domain string to its index in the array for validation.

### 7.2 Domain Topic Prefix Caching

To avoid repeated `snprintf` calls during discovery, the manager caches the domain topic prefix:

```c
char domain_topic_prefix[128];       // "homeassistant/{domain}/{device_id}/"
char current_domain_prefix_buf[32];  // Tracks current domain name
```

When the domain changes (or on first use), the prefix is rebuilt. Subsequent entities in the same domain reuse the cached prefix, concatenating only the entity suffix (`{erd_id}[_{field_id}]/config`).

---

## 8. Error Handling and Recovery

### 8.1 Build Task Failure

- **Allocation failure**: If `heap_caps_malloc` fails for stack or TCB, `start()` logs a warning, frees any partial allocation, and transitions to FAILED. Discovery is skipped.
- **Task creation failure**: If `xTaskCreateStatic` fails, `start()` logs an error, frees stack/TCB, and transitions to FAILED.

### 8.2 Decompression Failure

If `chunk_decompress()` returns non-zero for any chunk:
- Log error with category name, chunk index, offset, and size
- Transition to FAILED state
- Discovery stops immediately — no further categories or chunks are processed

### 8.3 Payload Too Large

If the built payload exceeds `payload_buf` (8 KB):
- Log warning with the ERD ID
- Increment `total_filtered`
- Return `false` from `process_jsonl_line()` — the entity is skipped but discovery continues

### 8.4 ERD Not Registered

If the ERD ID (or paired ERD ID) is not found in the sorted ERD array:
- Increment `total_filtered`
- Return `false` from `process_jsonl_line()` — the entity is skipped but discovery continues

### 8.5 No Recovery

Once the state transitions to FAILED, there is no automatic recovery. The caller would need to re-initialize and re-start the manager.

---

## 9. Memory Management

### 9.1 Pre-allocated Buffers

All buffers are embedded in the struct — no heap allocation during discovery processing:

| Buffer | Size | Purpose |
|--------|------|---------|
| `decomp_buf` | 14,336 B | Decompression output (max chunk is ~14 KB) |
| `line_buf` | 14,336 B | JSONL line parsing (max line is ~14 KB) |
| `topic_buf` | 192 B | Discovery topic string |
| `payload_buf` | 8,192 B | Discovery payload JSON |
| `device_json_buf` | 512 B | Pre-built device JSON fragment |
| `entity_name_buf` | 160 B | Entity name from JSONL |
| `erd_id_hex_buf` | 8 B | ERD ID in hex |
| `domain_buf` | 32 B | Domain string from JSONL |
| `field_id_buf` | 72 B | Field ID from JSONL |
| `paired_erd_buf` | 8 B | Paired ERD ID |
| `role_buf` | 16 B | Role string (e.g., "request") |
| `unit_buf` | 32 B | Unit of measurement |
| `device_class_buf` | 32 B | Device class string |
| `state_class_buf` | 32 B | State class string |
| `options_buf` | 256 B | Options array (raw JSON) |
| `data_type_buf` | 16 B | Data type (e.g., "u8", "i16") |
| `scale_factor_buf` | 16 B | Scale factor |
| `min_buf` / `max_buf` / `step_buf` | 32 B each | Number domain bounds |
| `mode_buf` | 16 B | Input mode (e.g., "box", "slider") |
| `payload_on_buf` / `payload_off_buf` | 16 B each | Switch payloads |
| `state_on_buf` / `state_off_buf` | 16 B each | State mapping |
| `unique_id_buf` | 160 B | Unique ID for entity |
| `state_topic_buf` | 128 B | State topic string |
| `command_topic_buf` | 128 B | Command topic string |
| `actual_state_topic_buf` | 128 B | Effective state topic (may differ for paired entities) |
| `actual_command_topic_buf` | 128 B | Effective command topic (may differ for paired entities) |
| `domain_topic_prefix` | 128 B | Cached domain topic prefix |
| `current_domain_prefix_buf` | 32 B | Tracks current domain for prefix caching |
| `sorted_erds` | 1,290 B | Sorted ERD array (645 × 2 bytes) |

**Peak memory usage:**
- Struct size: ~54 KB (buffers embedded in struct)
- Build task stack: 4 KB (heap-allocated, freed after build)
- Build task TCB: ~256 bytes (heap-allocated, freed after build)
- Decompression state: embedded `tinfl_decompressor` (~3 KB)

### 9.2 Heap Allocation

The only heap allocations are:
- `task_stack` (4096 bytes) and `task_tcb` (sizeof(StaticTask_t)) in `start()` — freed after build task completes
- `done_sem` in `init()` — deleted in `cleanup()` or `cleanup_resources()`

All other memory is static (embedded in the struct or in `ha_discovery_data.h`).

### 9.3 Cleanup

`cleanup_resources()` releases:
- `done_sem` via `vSemaphoreDelete()`
- `task_stack` via `free()`
- `task_tcb` via `free()`

`ha_discovery_manager_cleanup()` additionally:
- Handles premature cleanup (task still running) with semaphore wait + delay
- Calls `ha_discovery_cleanup_destroy()` on the embedded cleanup module
- Zeros the entire struct

---

## 10. Rate Limiting and Yielding

### 10.1 Publish Rate Limit

A minimum interval of 50 ms (`HA_DISCOVERY_PUBLISH_INTERVAL_MS`) is enforced between publishes. If the elapsed time since the last publish is less than 50 ms, `run()` returns immediately without advancing the offset — the next call will retry the same line.

### 10.2 Cooperative Yielding

To keep the WDT happy and allow MQTT callbacks to drain during long publish runs:

| Point | Delay | Purpose |
|-------|-------|---------|
| After each entity | 1 ms | Basic yield to other tasks |
| Every 5 entities | 2 ms | Extended yield for MQTT callback drain |
| After each chunk | 10 ms | Longer yield between chunks |
| After build task deletion | 100 ms | Allow idle task to unlink TCB |

The `publish_yield_counter` tracks entities published since the last extended yield, resetting to 0 after each extended yield.

### 10.3 WDT Reset

`esp_task_wdt_reset()` is called before each `vTaskDelay()` to prevent watchdog timeouts during the discovery process.

---

## 11. Stats

| Field | Type | Description |
|-------|------|-------------|
| `total_discovered` | `uint32_t` | Total entities that passed filtering (ERD registered). |
| `total_published` | `uint32_t` | Total discovery payloads published to MQTT. |
| `total_filtered` | `uint32_t` | Entities skipped (ERD not registered, paired ERD not registered, or payload too large). |

`total_discovered` and `total_published` are incremented together on each successful publish. `total_filtered` is incremented when an entity is skipped for any reason.

---

## 12. Invariants

1. **No cache ownership:** The manager holds a raw pointer to the cache but never frees or modifies its lifecycle. Cache ownership belongs to `GeappliancesBridge`.
2. **No MQTT lifecycle ownership:** The manager does not create, connect, or destroy the MQTT client. It uses `mqtt_client_publish_raw()` for publishing.
3. **Sequential processing:** Only one chunk is decompressed at a time. Only one entity payload is built at a time. No item pool or queue is needed.
4. **All buffers pre-allocated:** No heap allocation occurs during the discovery phase (after `start()`). All entity field buffers, topic buffers, and payload buffers are embedded in the struct.
5. **Rate limiting is mandatory:** The 50 ms minimum interval between publishes prevents MQTT broker overload during the initial discovery burst.
6. **Build task is short-lived:** The build task runs once, builds the sorted ERD list and device JSON, signals completion, and deletes itself. Its stack and TCB are freed during the first `run()` call.
7. **Domain prefix caching:** The domain topic prefix is cached and reused for consecutive entities in the same domain, reducing `snprintf` calls.
8. **Clean destroy:** `cleanup()` handles both normal completion (where task resources are already freed) and premature cleanup (where the task may still be running).

---

## 13. Dependencies

| Dependency | Role |
|------------|------|
| `erd_cache.h` | ERD cache iteration (`erd_cache_get_next_entry`) for building sorted ERD list |
| `i_mqtt_client.h` | MQTT publish interface (`mqtt_client_publish_raw`) |
| `ha_discovery_data.h` | Embedded compressed JSONL data (categories, chunks, raw bytes) |
| `ha_discovery_cleanup.h` | Embedded cleanup module for removing stale discovery topics |
| `geappliances_bridge_log.h` | Logging tag definition (`GEA_TAG`) |
| `esphome/core/log.h` | Logging macros (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`, `ESP_LOGD`, `ESP_LOGV`) |
| `esphome/core/hal.h` | Time source (`esphome::millis`) |
| `miniz.h` / `miniz_tinfl.h` | Zlib decompression (`tinfl_decompress`) |
| `freertos/FreeRTOS.h`, `freertos/task.h`, `freertos/semphr.h` | FreeRTOS task and semaphore APIs (ESP-IDF only) |
| `esp_task_wdt.h` | Watchdog timer reset (ESP-IDF only) |
| `esp_heap_caps.h` | Heap-capability-aware allocation (ESP-IDF only) |
| `esp_attr.h` | ESP attribute macros (ESP-IDF only) |

---

## 14. Known Limitations

1. **No re-discovery after completion:** Once the state reaches COMPLETE, the manager does not support re-running discovery. To re-discover, the caller must call `init()`, `configure()`, and `start()` again.
2. **No partial re-publish:** If the MQTT broker restarts and loses retained messages, the manager does not re-publish discovery payloads. Only the initial startup run publishes them.
3. **Single device ID:** The manager is configured with one device ID at configure time. Supporting multiple devices would require multiple manager instances.
4. **Payload size limit:** The 8 KB payload buffer limits the complexity of discovery payloads. Entities with very large templates (e.g., complex Jinja2 expressions) may be skipped.
5. **Build task allocation is blocking:** `start()` allocates 4 KB + TCB from the heap. If the heap is fragmented, allocation may fail even if total free memory is sufficient.
6. **No ESP-IDF stub support for decompression:** On simulator builds with `USE_ESP_IDF_STUBS`, `chunk_decompress()` always returns -1, causing discovery to fail. This is intentional — discovery is an ESP-IDF-only feature.
7. **Category filtering is static:** The mapping from appliance type to categories is hardcoded. Adding new appliance types or categories requires code changes.
8. **No discovery ordering guarantee:** Entities within a category are published in the order they appear in the JSONL data. There is no guarantee of a specific ordering across categories.
9. **Non-ESP-IDF builds skip discovery entirely:** On non-ESP-IDF platforms, `start()` transitions directly to COMPLETE and `run()` is a no-op. This is appropriate for simulator builds where MQTT discovery is not needed.
10. **JSONL schema is implicit:** The JSONL field names (`i`, `n`, `d`, `fi`, `p`, `r`, `u`, `dc`, `sc`, `o`, `dt`, `sf`, `m`, `pon`, `poff`, `son`, `soff`, `mn`, `mx`, `st`, `vt`, `ct`) are not documented in the code. The schema is defined by the tool that generates `ha_discovery_data.h`.
