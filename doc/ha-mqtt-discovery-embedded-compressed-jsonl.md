# Plan: HA MQTT Discovery with Embedded Compressed JSONL

## Architecture Overview

```
  ┌─────────────────────────────────────────────────────────────┐
  │  Build Time (Python)                                        │
  │  appliance_api_erd_definitions.json                         │
  │    → generate_ha_discovery.py                               │
  │      → JSONL per category (joshualongenecker format)        │
  │      → zlib compress each category                          │
  │      → ha_discovery_data.cpp (byte arrays + chunk table)    │
  └─────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │  Runtime (C++ on ESP32)                                     │
  │                                                             │
  │  loop() → check_steady_state() → true (one-shot)            │
  │    → ha_discovery_manager.start()                           │
  │      → FreeRTOS task: decompress + parse JSONL              │
  │      → Match ERDs against cache (binary search)             │
  │      → Queue discovery payloads                             │
  │      → Background task: drain queue, publish MQTT           │
  │        (follows erd_cache_mqtt_publisher pattern)            │
  │      → State: IDLE → PUBLISHING → COMPLETE                  │
  └─────────────────────────────────────────────────────────────┘
```

## Phase 1: Build-Time JSONL Generation & Compression

New file: `scripts/generate_ha_discovery.py`

- Reads `lib/public-appliance-api-documentation/appliance_api_erd_definitions.json`
- Groups ERDs into 10 categories by hex range (same as `generate_erd_lists.py`)
- For each ERD, generates a JSONL line in joshualongenecker's format:
  - i: ERD ID (hex, lowercase, zero-padded to 4 chars)
  - n: Entity name
  - d: HA domain (sensor, binary_sensor, switch, select, number, button)
  - vt: Value template (derived from data type)
  - dc: Device class
  - u: Unit of measurement
  - sc: State class
  - fi: Field ID (for multi-field ERDs)
  - r: Role (request/status for paired ERDs)
  - p: Paired ERD ID
  - o: Options (for select entities)
  - cm: Command template
  - sf: Scale factor
  - dt: Data type (for number entities)
  - ds: Data size

**Field sources**: Fields read directly from the JSON (`ha_domain`, `device_class`, `unit_of_measurement`, `state_class`, `scaling_factor`, `paired_erd`, `pair_role`) use the existing values in `appliance_api_erd_definitions.json`. Fields that must be *derived* include:
- `d` (HA domain): Uses `ha_domain` from JSON when present. For ERDs where `ha_domain` is `sensor` but the ERD is writable with enum values, derive `select` or `switch` from the `operations` array and data type.
- `vt` (value template): Derived from the first data field's `type` (u8, u16, string, enum, etc.).
- `cm` (command template): Derived for writable ERDs based on data type and size.
- `o` (options): Derived from enum `values` map in the data field definition.

**Multi-field ERD handling**: ERDs with multiple data fields (e.g., Clock Time with Hours/Minutes/Seconds) generate one JSONL line *per field*, each with a distinct `fi` (field ID) and a suffixed entity name (e.g., `clock_time_hours`, `clock_time_minutes`). Each field becomes a separate HA discovery payload.

- Compresses each category's JSONL with zlib
- Generates `components/geappliances_bridge/ha_discovery_data.cpp` with:
  - Per-category byte arrays (compressed data)
  - Chunk tables (offset, size per chunk)
  - Category metadata (name, data pointer, chunk count, max decompressed size)
- Also generates `components/geappliances_bridge/ha_discovery_config.h` (string ERD list)

Modified: `scripts/generate_erd_lists.py` — calls the new script at the end of `main()`.

Modified: `components/geappliances_bridge/__init__.py` — runs `generate_ha_discovery.py` during `to_code()` if `generate_device_config` is true. Reuses the existing `generate_device_config_` C++ member and `set_generate_device_config` setter already wired in the component.

## Phase 2: miniz Integration

New file: `lib/miniz/miniz.h` — single-header miniz library (zlib-compatible, public domain).

Modified: `components/geappliances_bridge/__init__.py` — adds `lib/miniz` to the component's include path (not via `cg.add_library()`, since miniz is committed locally as a single header, not fetched from GitHub).

## Phase 3: HaDiscoveryManager (C++)

New files:
- `components/geappliances_bridge/ha_discovery_manager.h`
- `components/geappliances_bridge/ha_discovery_manager.cpp`
- `components/geappliances_bridge/ha_discovery_data.h`

Based on eddie's implementation, simplified:
- No stale topic cleanup (per your decision)
- States: IDLE → PUBLISHING → COMPLETE / FAILED
- `configure()`: Set device_id, model_number, serial_number, erd_cache pointer, mqtt_client_adapter pointer
- `start()`: Spawn FreeRTOS task to decompress + parse JSONL, queue discovery payloads, then spawn a second background task for MQTT publishing
- `signal_work()`: Called from `loop()` to signal the background MQTT publish task (follows the `erd_cache_mqtt_publisher_signal_work` pattern)
- `cleanup()`: Stop tasks and free resources on teardown

**Two-task design** (mirrors `erd_cache_mqtt_publisher`):
1. **Decompress/parse task**: Spawns once during `start()`, decompresses all categories, parses JSONL, filters against ERD cache via binary search, queues discovery payloads into a pre-allocated ring buffer, then terminates.
2. **MQTT publish task**: Long-lived background task that drains the ring buffer and publishes discovery payloads to MQTT. Signaled from `loop()` via `signal_work()`. This avoids blocking the main `loop()` on the IDF MQTT mutex, preventing TWDT timeouts. On non-ESP-IDF platforms, falls back to inline draining from `loop()`.

All FreeRTOS task code is guarded with `#ifdef USE_ESP_IDF` for simulator/test build compatibility.

Key design decisions from eddie's approach:
- Pre-allocated item pool (no heap allocation during fetch)
- Static task stack (heap_caps_malloc, falls back from 8KB to 4KB)
- Binary search against sorted ERD cache for filtering
- Zero-allocation JSON parser (custom `json_get_str`)
- All buffers on stack or pre-allocated members

## Phase 4: Integration into GeappliancesBridge

Modified: `components/geappliances_bridge/geappliances_bridge.h`
- Add `HaDiscoveryManager ha_discovery_manager_;` member
- Add `bool ha_discovery_started_{false};` guard

Modified: `components/geappliances_bridge/geappliances_bridge.cpp`
- In `loop()`, trigger discovery start once steady state is reached. Note: `check_steady_state()` returns `true` only on the first transition (it short-circuits on `!steady_state_reached_`), so the `ha_discovery_started_` guard is the durable check:
  ```cpp
    if (this->steady_state_reached_ && !this->ha_discovery_started_ && this->generate_device_config_) {
      this->ha_discovery_started_ = true;
      this->ha_discovery_manager_.configure(
        this->device_identity_manager_.get_device_id(),
        this->device_identity_manager_.get_model_number(),
        this->device_identity_manager_.get_serial_number(),
        &this->erd_cache_,
        &this->mqtt_client_adapter_,
        this->generate_device_config_
      );
      this->ha_discovery_manager_.start();
    }
  ```
- In `loop()`, while `ha_discovery_manager_` is in PUBLISHING state, signal the background task:
  ```cpp
  #ifdef USE_ESP_IDF
    ha_discovery_manager_.signal_work();
  #else
    ha_discovery_manager_.run();  // inline drain for non-ESP-IDF
  #endif
  ```
- In `teardown()`, call `ha_discovery_manager_.cleanup()` **before** `erd_cache_destroy()` and `esphome_mqtt_client_adapter_destroy()`, as the discovery manager holds pointers to both.
- Wire `mqtt_client_adapter_` to the discovery manager via `configure()`.

Modified: `components/geappliances_bridge/__init__.py`
- Remove the deprecation warning for `generate_device_config`
- Add `lib/miniz` to the component's include path
- Run `generate_ha_discovery.py` during build

## Phase 5: Build System Integration

Modified: `Makefile` — Add `ha_discovery_data.cpp` and `ha_discovery_config.h` as build dependencies, following the existing pattern for `erd_lists.h` and `appliance_api_feature_lists.h` (lines 82–94). The generated files depend on `appliance_api_erd_definitions.json` and `scripts/generate_ha_discovery.py`.

## Files Summary

| File | Action |
|---|---|
| `scripts/generate_ha_discovery.py` | New — JSONL generation + compression |
| `lib/miniz/miniz.h` | New — miniz single header |
| `components/geappliances_bridge/ha_discovery_manager.h` | New — Manager header |
| `components/geappliances_bridge/ha_discovery_manager.cpp` | New — Manager implementation |
| `components/geappliances_bridge/ha_discovery_data.h` | New — Compressed data header |
| `components/geappliances_bridge/ha_discovery_data.cpp` | Generated — Compressed byte arrays |
| `components/geappliances_bridge/ha_discovery_config.h` | Generated — String ERD list |
| `components/geappliances_bridge/geappliances_bridge.h` | Modified — Add manager member |
| `components/geappliances_bridge/geappliances_bridge.cpp` | Modified — Wire into loop and teardown |
| `components/geappliances_bridge/__init__.py` | Modified — Build integration |
| `scripts/generate_erd_lists.py` | Modified — Call generate_ha_discovery |
| `Makefile` | Modified — Add regeneration target |

## Risks & Mitigations

1. **Flash size**: ~421KB compressed data estimate is unvalidated. Prototype the JSONL generation and measure actual compressed size before committing. ESP32-C3 4MB has ~2MB headroom after firmware, OTA partition, and NVS, but the exact number depends on the final JSONL format and zlib compression level.
2. **Heap during decompression**: Pre-allocated 4KB decompression buffer. No heap allocation during fetch task.
3. **MQTT publish blocking**: Resolved by using a background task for MQTT publishing (same pattern as `erd_cache_mqtt_publisher`). The main `loop()` only signals work, never blocks on the IDF MQTT mutex. WDT is fed in `loop()` between the protocol stack and HSM signals as it already is today.
4. **JSONL generation correctness**: Value templates derived from data types must match what Home Assistant expects. Validate against joshualongenecker's existing JSONL output. Multi-field ERDs generate separate entities per field.
5. **No re-discovery on appliance change**: Discovery payloads are published once at steady state. If the appliance changes (different model connected), there is no mechanism to re-publish discovery. This is accepted for the initial implementation; re-discovery can be added later if needed.
6. **FreeRTOS task portability**: All FreeRTOS task spawning code is guarded with `#ifdef USE_ESP_IDF`. Non-ESP-IDF builds (simulator, tests) fall back to inline execution from `loop()`.
