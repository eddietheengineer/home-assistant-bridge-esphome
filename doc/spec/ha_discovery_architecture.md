# HA Discovery Architecture — PR #43

## 1. Overview

### 1.1 Purpose

This document describes the end-to-end architecture of Home Assistant MQTT autodiscovery as implemented in PR #43. The system embeds all entity definition data in firmware flash, decompresses it at runtime without network dependency, matches discovered ERDs against the definitions, and publishes HA discovery payloads — all while operating within the severe memory constraints of ESP32-C3 (320 KB SRAM, ~1.8 MB flash).

### 1.2 Core Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Embedded JSONL (no HTTP)** | TLS/HTTP buffers exhaust ESP32-C3 heap; eliminates network dependency entirely |
| **Zero-allocation parsing** | `cJSON_Parse`/`cJSON_Delete` caused ~2000 heap allocations per discovery run; replaced with structural string matching |
| **Fixed-size char arrays** | `std::string` in `HaDiscoveryItem` and `PublishedTopic` replaced with `char[128]`/`char[1024]` to eliminate heap churn |
| **Pre-allocated item pool** | `HaDiscoveryItem` objects come from a static pool (`item_pool_[32]`), not `new`; queue carries `uint16_t` indices, not pointers |
| **Static arrays for topic tracking** | `published_topics_[256]` and `stale_topics_[64]` are class BSS members, not heap-allocated, eliminating ~211 KB peak heap spike |
| **Live ERD cache pointer** | No snapshot is taken at init; the fetch task reads the cache directly at fetch time, eliminating race conditions |

### 1.3 Not Responsible For

- Determining which ERDs are valid (delegated to `FeatureBitManager` / `ErdRegistry`)
- Managing bridge lifecycle or MQTT connection state
- Post-discovery entity updates (discovery is a one-shot operation per startup)

---

## 2. Startup Integration

### 2.1 HSM Phase Flow

HA discovery is **Phase 8** in the startup HSM, executed after all bridges reach steady state:

```
protocol_stack → startup_delay → autodiscovery → device_id
  → mqtt_client_init → feature_bits → bridge_init → subscription_watch
  → ha_discovery → running
```

### 2.2 Entry Point: `startup_state_ha_discovery`

**On entry:**
- Calls `svc->init_ha_discovery()`, which:
  1. Clears any previously-published HA discovery topics (`clear_ha_discovery_sync()`)
  2. Initializes the `HaDiscoveryManager` with device identity and a pointer to the live `erd_cache_`
  3. Sets the MQTT adapter

**On `signal_run_loop`:**
- Defers if MQTT is not yet connected
- Calls `svc->run_ha_discovery()`, which calls `ha_discovery_manager_.run(is_device_steady_state())`
- Transitions to `startup_state_running` after the first run call

### 2.3 Steady State Gate

`is_device_steady_state()` returns `true` only when both:
- **Subscription steady state:** Either no subscription activity for `HA_DISCOVERY_QUIET_MS` (10 s), or 10 s elapsed since subscription start time (handles the initial burst)
- **Polling steady state:** Polling bridge has completed its probe phase (or is not active)

This ensures the ERD cache is fully populated before discovery begins.

---

## 3. Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                     Startup HSM (Phase 8)                      │
│                                                                 │
│  ha_discovery entry → init_ha_discovery()                      │
│    ├── clear_ha_discovery_sync()   (clear retained topics)      │
│    └── ha_discovery_manager_.init(device_id, model, serial,    │
│                                    &erd_cache_, true)           │
│         └── state_ = HA_DISCOVERY_WAITING_FOR_READY             │
│                                                                 │
│  ha_discovery run_loop → run_ha_discovery()                    │
│    └── ha_discovery_manager_.run(is_device_steady_state())     │
│                                                                 │
│  WAITING_FOR_READY + device_steady_state                       │
│    └── publish_ha_discovery_()                                 │
│         ├── Allocate queue, task stack (8KB→4KB), TCB          │
│         └── xTaskCreateStatic(ha_fetch_task_fn_)               │
│              state_ = HA_DISCOVERY_PUBLISHING                   │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│              Background Fetch Task (ha_fetch_task_fn_)          │
│                                                                 │
│  fetch_ha_definitions_()                                       │
│    ├── Build sorted_erds_ from live cache + on_erd_seen list   │
│    ├── Insertion-sort for binary search                        │
│    ├── build_device_json_() → device_json_buf_[512]            │
│    └── For each needed category:                               │
│         └── process_category_(cat, device_id, device_json)     │
│              ├── For each chunk:                                │
│              │    └── tinfl_decompress() → decomp_buf_[4096]   │
│              │         └── Parse lines → line_buf_[4096]       │
│              │           └── process_jsonl_line_(line, ...)    │
│              │                ├── json_get_str() for each key  │
│              │                ├── Binary search sorted_erds_   │
│              │                ├── Build payload (stack)        │
│              │                ├── Insert into item_pool_       │
│              │                └── xQueueSend(queue, &idx)      │
│              └── vTaskDelay(50ms) + WDT reset                  │
│                                                                 │
│  xQueueSend(queue, SENTINEL)                                   │
│  fetch_done_ = true                                            │
│  vTaskDelete(nullptr)                                          │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│              Main Loop (HA_DISCOVERY_PUBLISHING)                │
│                                                                 │
│  run() every loop iteration:                                   │
│    if (now - last_publish_ms_ >= 50ms):                        │
│      publish_next_entity_()                                    │
│        ├── xQueueReceive(queue, &idx)                          │
│        │   ├── SENTINEL → cleanup + discover_stale_topics_()   │
│        │   ├── fetch_done_ → cleanup + discover_stale_topics_()│
│        │   └── idx → publish item_pool_[idx] via MQTT          │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│              Stale Topic Discovery (CLEANING_STALE)             │
│                                                                 │
│  discover_stale_topics_()                                      │
│    └── Subscribe to homeassistant/*/<device_id>/*/config       │
│         └── stale_topic_callback_() per retained message       │
│              ├── Parse component + erd_hex from topic          │
│              ├── Binary search published_topics_               │
│              └── If not found → add to stale_topics_ (sorted)  │
│                                                                 │
│  run() every loop iteration:                                   │
│    ├── If subscription timeout (2s):                           │
│    │    └── Unsubscribe, stay in CLEANING_STALE                │
│    └── If subscription closed:                                 │
│         └── publish_stale_cleanup_() at 50ms rate              │
│              └── Publish empty retained message per stale topic│
│                   → state_ = HA_DISCOVERY_IDLE                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. Embedded Data

### 4.1 Structure

All 10 appliance category JSONL files are compressed with zlib (chunked, 4 KB blocks) and embedded as C byte arrays in `.rodata`:

```
ha_discovery_data.cpp  (27,017 lines, ~421 KB compressed from 2.4 MB)
  └── ha_discovery_categories[]  (array of HaDiscoveryCategory)
        └── .data[]        (flash byte array for all chunks)
        └── .chunks[]      (HaDiscoveryChunk table: offset + size)
        └── .num_chunks    (chunk count)
        └── .max_decompressed_chunk  (for buffer sizing)
```

### 4.2 Categories and ERD Ranges

| Category | ERD Range | Always Fetched? |
|----------|-----------|-----------------|
| `common` | 0x0000–0x0FFF | Yes |
| `refrigeration` | 0x1000–0x1FFF | Only if cache has ERD in range |
| `laundry` | 0x2000–0x2FFF | Only if cache has ERD in range |
| `dishwasher` | 0x3000–0x3FFF | Only if cache has ERD in range |
| `waterheater` | 0x4000–0x4FFF | Only if cache has ERD in range |
| `range` | 0x5000–0x5FFF | Only if cache has ERD in range |
| `airconditioning` | 0x7000–0x7FFF | Only if cache has ERD in range |
| `waterfilter` | 0x8000–0x8FFF | Only if cache has ERD in range |
| `smallappliance` | 0x9000–0x9FFF | Only if cache has ERD in range |
| `energy` | 0xD000–0xDFFF | Only if cache has ERD in range |

### 4.3 Flash Impact

On ESP32-C3 with 1.8 MB flash, embedded data uses ~23% of flash. This is a deliberate trade-off: eliminates network dependency at the cost of flash space. A future optimization could lazy-load categories from network on first use, caching in PSRAM if available.

---

## 5. State Machine

### 5.1 States

```
HA_DISCOVERY_IDLE
  └─ init() → HA_DISCOVERY_WAITING_FOR_READY
       ├─ device_steady_state + timeout → HA_DISCOVERY_FAILED
       └─ device_steady_state → publish_ha_discovery_()
            ├─ allocation failure → HA_DISCOVERY_COMPLETE (graceful skip)
            └─ success → HA_DISCOVERY_PUBLISHING
                 └─ all entities published → discover_stale_topics_()
                      ├─ no MQTT adapter → HA_DISCOVERY_IDLE
                      └─ subscribed → HA_DISCOVERY_CLEANING_STALE
                           ├─ timeout (2s) → unsubscribe, stay in CLEANING_STALE
                           └─ subscription closed → publish_stale_cleanup_()
                                └─ all stale cleaned → HA_DISCOVERY_IDLE

HA_DISCOVERY_CLEARING (used by clear_ha_discovery())
  └─ all cleared → HA_DISCOVERY_COMPLETE
```

### 5.2 State Transitions

| From | Condition | To |
|------|-----------|-----|
| `IDLE` | `init()` called | `WAITING_FOR_READY` |
| `WAITING_FOR_READY` | `millis() - start_time_ > 30s` | `FAILED` |
| `WAITING_FOR_READY` | `device_steady_state == true` | `PUBLISHING` |
| `PUBLISHING` | sentinel received + cleanup done | `CLEANING_STALE` |
| `CLEANING_STALE` | subscription timeout + all stale published | `IDLE` |
| `CLEARING` | all topics cleared | `COMPLETE` |
| Any | allocation failure during `publish_ha_discovery_()` | `COMPLETE` (graceful skip) |

---

## 6. Key Components

### 6.1 `HaDiscoveryManager` (ha_discovery_manager.h/cpp)

The core orchestrator. Owns the state machine, FreeRTOS task lifecycle, and all data structures.

**Memory layout (ESP-IDF build):**

| Member | Size | Location |
|--------|------|----------|
| `decomp_buf_[4096]` | 4 KB | class BSS |
| `decomp_state_` | ~32 KB | class BSS (tinfl_decompressor) |
| `line_buf_[4096]` | 4 KB | class BSS |
| `device_json_buf_[512]` | 512 B | class BSS |
| `item_pool_[32]` | 32 × 1,152 B = 36.9 KB | class BSS |
| `sorted_erds_[645]` | 1.3 KB | class BSS |
| `published_topics_[256]` | 256 × 64 B = 16 KB | class BSS |
| `stale_topics_[64]` | 64 × 128 B = 8 KB | class BSS |
| **Total static** | **~88 KB** | class BSS |

**Heap allocations at runtime:**

| Allocation | Size | When |
|------------|------|------|
| Queue storage | 64 B (32 × 2 B) | `publish_ha_discovery_()` |
| Queue TCB | ~32 B | `publish_ha_discovery_()` |
| Task stack | 4–8 KB | `publish_ha_discovery_()` |
| Task TCB | ~300 B | `publish_ha_discovery_()` |
| **Total peak heap** | **~5–12 KB** | during fetch task |

### 6.2 `json_get_str()` — Zero-Allocation JSON Parser

A structural key-value extractor for flat JSON objects. Returns a pointer into the source string, eliminating allocation.

**Algorithm:**
1. Scan for `"` characters
2. Verify structural position: preceded by `{` or `,` (with optional whitespace) — prevents matching key names inside value strings
3. Match the key character-by-character, handling escaped characters in keys
4. Advance past `:` to find the value
5. Return pointer to first character of the value (after opening quote), or `""` if not found or not a string

**Limitations:**
- Only works for flat (non-nested) JSON objects
- Returns `""` for non-string values (numbers, booleans, null)
- Does not handle `\uXXXX` escapes in values (skips them)

### 6.3 `process_jsonl_line_()` — Entity Builder

For each decompressed JSONL line:
1. Extract fields using `json_get_str()` via the `get_str` lambda (handles escape sequences)
2. Parse ERD ID from hex string
3. Binary search `sorted_erds_` to check if ERD is registered
4. If not found and role is `r` (read) with a paired ERD, binary search for the paired ERD
5. Build the HA discovery payload JSON on a stack buffer (`payload_buf[1024]`)
6. Build the MQTT topic on a stack buffer (`topic_buf[128]`)
7. Insert the topic into `published_topics_` (sorted by component, then erd_hex)
8. Store the entity in `item_pool_` (round-robin via `item_pool_next_`)
9. Send the pool index to the queue (spin-yield if full)

### 6.4 `EsphomeMqttClientAdapter` — MQTT Bridge

Provides C-compatible MQTT operations for the discovery manager:

| Function | Purpose |
|----------|---------|
| `esphome_mqtt_client_adapter_publish()` | Publish topic/payload with retain flag |
| `esphome_mqtt_client_adapter_subscribe()` | Subscribe with raw C callback + user_data (no `std::function` stored) |
| `esphome_mqtt_client_adapter_unsubscribe()` | Unsubscribe by handle |
| `esphome_mqtt_client_adapter_is_connected()` | Check MQTT connection status |

The `MqttSubscription` struct uses `char topic[128]` and a raw function pointer + `user_data` to avoid heap allocation per subscription.

### 6.5 `miniz.h` — Decompression

Standalone zlib decompressor (public domain, Espressif variant). Only `tinfl_decompress` is used. All unnecessary APIs are disabled via `#define MINIZ_NO_*` macros. The decompressor operates on pre-allocated buffers — no internal allocation.

---

## 7. Performance Characteristics

### 7.1 Time Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| ERD cache scan at fetch start | O(n) | Single pass to build `sorted_erds_` |
| ERD sort (insertion sort) | O(n²) | n ≤ 645, typically ~100–200 |
| Per-JSONL-line ERD lookup | O(log n) | Binary search on sorted array |
| Published topic insert | O(n) | Insertion sort on append (n ≤ 256) |
| Stale topic lookup | O(log n) | Binary search on sorted arrays |

### 7.2 Memory Profile

| Phase | Free Heap (ESP32-C3) | Notes |
|-------|---------------------|-------|
| After boot | ~85 KB | Before any bridge work |
| After ERD cache | ~39 KB | Subscription burst populates cache |
| After fetch task start | ~27 KB | 8 KB stack + queue overhead |
| During entity publishing | ~27 KB | No per-entity allocation (pool-based) |
| After fetch task end | ~35 KB | Stack/TCB freed |

### 7.3 Timing

| Phase | Duration | Notes |
|-------|----------|-------|
| `WAITING_FOR_READY` | 10–30 s | Quiet window or safety cap |
| Fetch task (all categories) | 2–5 s | Depends on number of entities |
| Entity publishing | N × 50 ms | Rate-limited per entity |
| Stale discovery | 2 s | Fixed timeout |
| Stale cleanup | M × 50 ms | Rate-limited per stale topic |

For a typical device with ~250 entities: ~12.5 s for entity publishing + ~2 s for stale discovery.

---

## 8. Crash Risks and Mitigations

### 8.1 Heap Exhaustion

**Risk:** ERD cache + fetch task stack + queue + entity allocations exceed available DRAM.

**Mitigations:**
- Pre-allocated item pool eliminates per-entity `new`
- Static arrays for topic tracking eliminate heap growth
- Task stack falls back from 8 KB to 4 KB if allocation fails
- Graceful skip: if queue/stack/TCB allocation fails, transition to `COMPLETE` with a warning

### 8.2 Task Watchdog Timeout

**Risk:** Fetch task or main loop blocks long enough to trigger WDT.

**Mitigations:**
- `esp_task_wdt_reset()` called after each category in the fetch task
- `esp_task_wdt_reset()` called in `publish_next_entity_()` and `publish_next_clear_()` main loop paths
- `esp_task_wdt_reset()` called inside `clear_ha_discovery_sync()` loop
- 50 ms `vTaskDelay` between categories in fetch task

### 8.3 Race Conditions

**Risk:** `cleanup()` races with `publish_next_entity_()` on shared resources (queue, stack, TCB).

**Mitigations:**
- `fetch_done_` bool is set by the fetch task before `vTaskDelete`; `publish_next_entity_()` checks `fetch_done_` instead of `task_handle_ == nullptr`
- `cleanup()` sets `fetch_done_ = true` and `task_handle_ = nullptr` before touching the queue
- `cleanup()` is idempotent: early return when `queue_ == nullptr && task_stack_ == nullptr`

### 8.4 Use-After-Free

**Risk:** `eTaskGetState()` on a deleted TCB reads freed memory.

**Mitigation:** Removed `eTaskGetState` polling entirely; the semaphore/sentinel is the authoritative completion signal.

---

## 9. File Map

| File | Role |
|------|------|
| `ha_discovery_manager.h` | Public API, state enum, `HaDiscoveryItem` struct, class declaration with all member data |
| `ha_discovery_manager.cpp` | Full implementation: state machine, fetch task, JSON parsing, entity building, stale cleanup |
| `ha_discovery_data.h` | `HaDiscoveryCategory` and `HaDiscoveryChunk` struct definitions; category array declaration |
| `ha_discovery_data.cpp` | Auto-generated compressed byte arrays for all 10 categories (~27K lines) |
| `ha_discovery_config.h` | Known string-valued ERD IDs for the MQTT adapter |
| `miniz.h` | Standalone zlib decompressor (tinfl only, all other APIs disabled) |
| `esphome_mqtt_client_adapter.h` / `.cpp` | C-compatible MQTT adapter with subscribe/unsubscribe support |
| `geappliances_bridge.cpp` / `.h` | Bridge integration: `init_ha_discovery()`, `run_ha_discovery()`, steady-state checks |
| `geappliances_bridge_startup_hsm.cpp` | HSM Phase 8: `startup_state_ha_discovery` entry/run_loop/exit |
| `i_bridge_services.h` | Abstract interface: `init_ha_discovery()`, `run_ha_discovery()`, `is_device_steady_state()`, `is_mqtt_connected()` |
| `__init__.py` | ESPHome codegen: removed `esp_http_client` dependency (no longer needed) |

---

## 10. Known Limitations

1. **Flash usage:** ~421 KB embedded data is ~23% of ESP32-C3 flash. All 10 categories are embedded regardless of which are needed. A lazy-load approach (fetch from network on first use) would reduce this but reintroduces network dependency.

2. **Published topic tracking cap:** On ESP-IDF builds, only 256 published topics are tracked (vs. 645 on stubs). Devices with >256 entities still publish all entities, but excess topics won't be cleared on re-discovery.

3. **Stale topic tracking cap:** On ESP-IDF builds, only 64 stale topics are tracked. If the broker has more stale topics than this, the excess won't be cleaned up.

4. **Single-shot discovery:** Discovery runs once at startup. If the ERD cache changes after discovery (e.g., new subscription data arrives), the discovery payloads are not updated.

5. **JSON parser limitations:** `json_get_str()` only handles flat JSON objects. Nested objects, arrays of objects, or non-string values are not supported. This is acceptable because the JSONL data is generated and controlled.

6. **No retry on category failure:** If decompression of a chunk fails, that chunk is skipped with a warning. Entities in that chunk will be missing from discovery.

7. **Graceful skip on constrained devices:** If free heap is insufficient at discovery start, the manager transitions to `COMPLETE` without publishing any entities. This is safe but means HA discovery won't work on devices where the ERD cache consumes most of available DRAM.

---

## 11. Test Coverage

- 365 unit tests (1,417 checks, 0 failures)
- ESP32-C3 compilation succeeds (DRAM: 92.1%, Flash: 83.1%)
- ESP32-C6 OTA verified: 252 entities published (8 common + 240 laundry + 4 energy) with 0 stale topics
- ESP-IDF stubs for test builds: `freertos_stub.h`, `esp_zlib_stub.h`, `cJSON.h` stub, `button.h` stub
