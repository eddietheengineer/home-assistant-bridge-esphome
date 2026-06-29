# Plan: HA MQTT Discovery with Embedded Compressed JSONL

## Architecture Overview

```
  ┌─────────────────────────────────────────────────────────────┐
  │  Build-Time:                                                │
  │  generate_ha_discovery.py → ha_discovery/*.jsonl            │
  │    → Makefile runs compress_ha_discovery.py → components/geappliances_bridge/ha_discovery_data.h   │
  │                                                             │
  │  Runtime (C++ on ESP32):                                    │
  │  loop() → check_steady_state() → true (one-shot)            │
  │    → ha_discovery_manager.start()                           │
  │      → Decompress embedded JSONL byte arrays                │
  │      → Parse JSONL, filter against ERD cache                │
  │      → Queue discovery payloads                             │
  │      → Main loop drains queue at 50ms intervals             │
  │        (follows erd_cache_mqtt_publisher pattern)            │
  │      → State: IDLE → DECOMPRESSING → PUBLISHING → COMPLETE  │
  └─────────────────────────────────────────────────────────────┘
```

## MQTT Packet Specification

### Data Topics (ERD values)

```
geappliances/<device_id>/erd/0x<ERD_ID>/value
```

- QoS: 0, Retain: true
- Payload: hex-encoded string of the raw ERD bytes (e.g., "01a2b3"), OR plain ASCII text for string-type ERDs (model number, serial number, etc.)
- Example: `geappliances/Dishwasher_ZL4200ABC_12345678/erd/0x0008/value` → "06"

### Write Command Topics

```
geappliances/<device_id>/erd/0x<ERD_ID>/write
```

- Subscribed via wildcard: `geappliances/<device_id>/erd/+/write`
- Payload: hex-encoded bytes to write to the ERD

### Write Result Topics

```
geappliances/<device_id>/erd/0x<ERD_ID>/write_result
```

- QoS: 0, Retain: false
- Payload: "success" or "failure (reason: <N>)"

### HA Discovery Config Topics

```
homeassistant/<domain>/<device_id>/<topic_key>/config
```

Where `<topic_key>` is `<ERD_ID lowercase hex>`, optionally suffixed with `_<field_id>` for multi-field ERDs.

- QoS: 0, Retain: true
- Payload: JSON conforming to the Home Assistant MQTT Discovery schema for the given domain

### Device ID

The `<device_id>` in all topics is either:

- Auto-generated: `<ApplianceTypeName>_<ModelNumber>_<SerialNumber>` (e.g., `Dishwasher_ZL4200ABC_12345678`), derived from ERDs 0x0008, 0x0001, 0x0002
- User-configured: set via YAML `device_id` option

## JSONL Entity Definition Format

Each line in a category JSONL file is a compact JSON object defining one entity:

| Key | Required | Description |
|-----|----------|-------------|
| uid | Optional | Unique ID suffix override. Replaces the default `erd_<id>[_<field>]` suffix in the runtime-assembled unique_id. E.g., `uid: "power_state"` produces `<device_id>_power_state` instead of `<device_id>_erd_0008` |
| i | Yes | ERD ID as lowercase hex string (e.g., "0008") |
| n | Yes | Entity name (human-readable) |
| d | Yes | HA domain: sensor, binary_sensor, switch, select, number, button |
| ds | Yes | ERD data size in bytes |
| vt | Optional | Jinja2 value_template for decoding the hex payload |
| ct | Optional | Jinja2 command_template for encoding commands (select/number) |
| u | Optional | unit_of_measurement |
| dc | Optional | device_class |
| sc | Optional | state_class (e.g., "total", "measurement") |
| fi | Optional | Field ID for sub-fields within a multi-byte ERD. Creates a separate entity with _ suffix in topic key and unique_id |
| p | Optional | Paired ERD ID (hex string). For request/status pairs that form a single HA entity (switch, select) |
| r | Optional | Role: "request" or "status". The **request** ERD maps to the HA discovery `command_topic` (writing to `geappliances/<device_id>/erd/0x<request_erd>/write`). The **status** ERD maps to the HA discovery `state_topic` (reading from `geappliances/<device_id>/erd/0x<status_erd>/value`) |
| o | Optional | JSON array of options (for select domain) |
| dt | Optional | Data type for number domain: int8, uint8, int16, uint16, int24, uint24, int32, uint32 |
| sf | Optional | Scale factor for number domain (integer >= 1) |

## Category Files and ERD Ranges

| File | ERD Range |
|------|-----------|
| common.jsonl | 0x0000–0x0FFF (always included) |
| refrigeration.jsonl | 0x1000–0x1FFF |
| laundry.jsonl | 0x2000–0x2FFF |
| dishwasher.jsonl | 0x3000–0x3FFF |
| waterheater.jsonl | 0x4000–0x4FFF |
| range.jsonl | 0x5000–0x5FFF |
| airconditioning.jsonl | 0x7000–0x7FFF |
| waterfilter.jsonl | 0x8000–0x8FFF |
| smallappliance.jsonl | 0x9000–0x9FFF |
| energy.jsonl | 0xD000–0xDFFF (always included) |

## Phase 1: Build-Time JSONL Generation

New file: `scripts/generate_ha_discovery.py`

- Reads `lib/public-appliance-api-documentation/appliance_api_erd_definitions.json`
- Groups ERDs into categories by hex range (same as `generate_erd_lists.py`)
- For each ERD, generates a JSONL line in the compact format:
  - i: ERD ID (hex, lowercase, zero-padded to 4 chars)
  - n: Entity name
  - d: HA domain (sensor, binary_sensor, switch, select, number, button)
  - ds: Data size in bytes
  - vt: Value template (derived from data type)
  - ct: Command template (derived for writable ERDs)
  - dc: Device class
  - u: Unit of measurement
  - sc: State class
  - fi: Field ID (for multi-field ERDs)
  - r: Role (request/status for paired ERDs)
  - p: Paired ERD ID
  - o: Options (for select entities)
  - dt: Data type (for number entities)
  - sf: Scale factor

**Unique ID**: The `unique_id` is not stored in the JSONL — it is assembled at runtime by the discovery manager from the device ID and ERD ID: `<device_id>_erd_<erd_id>` (e.g., `Dishwasher_ZL4200ABC_12345678_erd_0008`). For multi-field ERDs, the field ID is appended: `<device_id>_erd_<erd_id>_<field_id>` (e.g., `Dishwasher_ZL4200ABC_12345678_erd_0005_hours`). The optional `uid` key in the JSONL replaces the entire `erd_<id>[_<field>]` suffix with the given string (e.g., `uid: "power_state"` produces `<device_id>_power_state`).

- `d` (HA domain): Uses `ha_domain` from JSON when present. For ERDs where `ha_domain` is `sensor` but the ERD is writable with enum values, derive `select` or `switch` from the `operations` array and data type. Domain can also be overridden by `device_class` (see `dc` below).

- `dc` (device class): Uses `device_class` from JSON when present, but must validate against HA's allowed values for the target domain. Invalid values are omitted:
  - `device_class: "enum"` — not a valid HA device class for any domain. Omit from discovery payload.
  - `device_class: "restart"` — valid only for the **button** domain. If the ERD's `ha_domain` is `sensor` but `device_class` is `restart`, change the domain to `button` and keep the device class.
  - All other `device_class` values are passed through as-is.
- `vt` (value template): Derived from the data field's `type` and metadata. The generator must handle these cases:

  - **u8/u16/u24/u32**: Raw hex payload decoded as little-endian integer. If `scaling_factor` is present, divide: `{{ value_json / scaling_factor }}`.
  - **string**: Each byte pair decoded as ASCII with 0x20 offset (per GE API spec), trailing `_` (0x5F) padding stripped. Example ERDs: 0x0001 (Model Number), 0x0002 (Serial Number).

  - **enum**: Raw byte mapped to human-readable label via the `values` map in the data field definition. The template embeds the mapping as a Jinja2 dict lookup.
  - **Multi-field ERDs**: Each field generates its own entity with a template that extracts the byte at that field's `offset`. For example, Clock Time (0x0005) produces three entities — hours extracts byte 0, minutes extracts byte 1, seconds extracts byte 2.
  - **Version ERDs**: See [Version ERD Handling](#version-erd-handling) below.
- `ct` (command template): Derived for writable ERDs based on data type and size.
- `o` (options): Derived from enum `values` map in the data field definition.

**Multi-field ERD handling**: ERDs with multiple data fields (e.g., Clock Time with Hours/Minutes/Seconds) generate one JSONL line *per field*, each with a distinct `fi` (field ID) and a suffixed entity name (e.g., `clock_time_hours`, `clock_time_minutes`). Each field becomes a separate HA discovery payload.

**Version ERD Handling**: Version ERDs with a 4-part structure are consolidated into a single entity per version group, producing a dotted decimal string instead of four separate sensors.

- **Simple version ERDs**: An ERD is classified as a simple version ERD when it has exactly 4 non-reserved `u8` data fields at consecutive offsets 0–3, with field names matching the pattern `Critical Major`, `Critical Minor`, `Non-Critical Major`, `Non-Critical Minor` (case-insensitive, with optional prefix/suffix). The generator produces **one** sensor entity with the ERD's display name and a value template that concatenates the four bytes as decimal with dots:

  ```
  {{ (value[0:2] | int(base=16) | string) + "." + (value[2:4] | int(base=16) | string) + "." + (value[4:6] | int(base=16) | string) + "." + (value[6:8] | int(base=16) | string) }}
  ```

  Example: ERD 0x003A (Application Version) with payload `01000203` → entity value `1.0.2.3`.

  Affected ERDs: 0x0039 (Boot Loader Version), 0x003A (Application Version), 0x003B (Parametric Version), 0x003C (Auxiliary Version).

- **Multi-board version ERDs**: ERDs containing version data for multiple boards (e.g., 0x304D System Software Versions) are detected by grouping fields by board prefix (the text before the version role keyword). Each board that has all 4 version components (Critical Major, Critical Minor, Non-Critical Major, Non-Critical Minor) produces one version entity. If the board also has Parametric Major and Parametric Minor fields, a second parametric version entity is generated.

  - Version entity name: `{display_name} - {board} Version` (e.g., `System Software Versions - UI Version`)
  - Version entity `fi`: slug of the board name (e.g., `ui`)
  - Parametric entity name: `{display_name} - {board} Parametric Version` (e.g., `System Software Versions - UI Parametric Version`)
  - Parametric entity `fi`: slug of `{board}_parametric` (e.g., `ui_parametric`)
  - Parametric value template: `{{ (value[8:10] | int(base=16) | string) + "." + (value[10:12] | int(base=16) | string) }}`

  Affected ERDs: 0x304D (System Software Versions), 0x324D (System Software Versions Tub 1).

- **Non-matching version ERDs**: Board version ERDs that do not follow the 4-part pattern (e.g., refrigeration board versions 0x1152–0x115d with Non-critical Major, Non-critical Minor, ERD Update Version) are treated as standard multi-field ERDs and expanded as separate entities per field.

The generated JSONL files are written to `ha_discovery/` and compressed at build time into `components/geappliances_bridge/ha_discovery_data.h` — embedded as C byte arrays in the firmware binary. No runtime network fetch is needed.

Modified: `scripts/generate_erd_lists.py` — calls the new script at the end of `main()`.

Modified: `components/geappliances_bridge/__init__.py` — runs `generate_ha_discovery.py` during `to_code()` if `generate_device_config` is true. Reuses the existing `generate_device_config_` C++ member and `set_generate_device_config` setter already wired in the component.

## Phase 2: Build-Time Compression

Modified: `Makefile`

- Add a build dependency rule (following the existing pattern for `erd_lists.h` and `appliance_api_feature_lists.h` at lines 82–94) that runs `compress_ha_discovery.py` on the generated JSONL files to produce `components/geappliances_bridge/ha_discovery_data.h`
- Each category file is compressed (e.g., zlib) and turned into a C `const uint8_t` array with a length constant
- The header declares each category as `const uint8_t ha_discovery_<category>[]` and `const size_t ha_discovery_<category>_len`

New file: `scripts/compress_ha_discovery.py`

- Reads each JSONL file from `ha_discovery/`
- Compresses with zlib
- Outputs `components/geappliances_bridge/ha_discovery_data.h` with C arrays

## Phase 3: HaDiscoveryManager (C++)

New files:

- `components/geappliances_bridge/ha_discovery_manager.h`
- `components/geappliances_bridge/ha_discovery_manager.cpp`

Based on eddie's implementation, simplified:

- No stale topic cleanup (per your decision)
- States: IDLE → DECOMPRESSING → PUBLISHING → COMPLETE / FAILED
- `configure()`: Set device_id, model_number, serial_number, erd_cache pointer, mqtt_client_adapter pointer
- `start()`: Decompress embedded JSONL byte arrays, parse, filter against ERD cache, and queue discovery payloads
- `signal_work()`: Called from `loop()` to signal the background MQTT publish task (follows the `erd_cache_mqtt_publisher_signal_work` pattern)
- `cleanup()`: Stop tasks and free resources on teardown

**Two-task design** (mirrors `erd_cache_mqtt_publisher`):

1. **Decompress/parse task**: Spawns once during `start()`, decompresses the embedded JSONL byte arrays, parses JSONL, filters against ERD cache via `erd_cache_find()` (linear scan), queues discovery payloads into a pre-allocated ring buffer, then terminates.
2. **MQTT publish task**: Long-lived background task that drains the ring buffer and publishes discovery payloads to MQTT at 50ms intervals. Signaled from `loop()` via `signal_work()`. This avoids blocking the main `loop()` on the IDF MQTT mutex, preventing TWDT timeouts. On non-ESP-IDF platforms, falls back to inline draining from `loop()`.

All FreeRTOS task code is guarded with `#ifdef USE_ESP_IDF` for simulator/test build compatibility.

Key design decisions:

- Pre-allocated item pool (no heap allocation during decompress)
- Linear scan via `erd_cache_find()` against the ERD cache for filtering. The cache stores the first N entries as `valid=true` (registered ERDs), remaining entries are `valid=false` (empty). `erd_cache_find()` scans for a matching ERD ID — O(N) where N ≤ 200, trivial on ESP32.
- Zero-allocation JSON parser (custom `json_get_str`)
- All buffers on stack or pre-allocated members
- No TLS/HTTPS dependency — data is embedded at build time

## Phase 4: Integration into GeappliancesBridge

Modified: `components/geappliances_bridge/geappliances_bridge.h`

- Add `HaDiscoveryManager ha_discovery_manager_;` member
- Add `bool ha_discovery_started_{false};` guard

Modified: `components/geappliances_bridge/geappliances_bridge.cpp`

- In `loop()`, trigger discovery start once steady state is reached. `check_steady_state()` is called from the startup HSM (not `loop()`), and returns `true` only on the first transition by short-circuiting on `!steady_state_reached_`. After the HSM transitions to `startup_state_running`, `check_steady_state()` is never called again. The `loop()` code checks `steady_state_reached_` directly, guarded by `ha_discovery_started_` for durability:

  ```cpp
    if (this->steady_state_reached_ && !this->ha_discovery_started_ && this->generate_device_config_) {
      this->ha_discovery_started_ = true;
      this->ha_discovery_manager_.configure(
        this->device_identity_manager_.get_device_id(),
        this->device_identity_manager_.get_model_number(),
        this->device_identity_manager_.get_serial_number(),
        &this->erd_cache_,
        &this->mqtt_client_adapter_
      );
      this->ha_discovery_manager_.start();
    }
  ```

- In `loop()`, place the discovery manager signal **after** the existing `erd_cache_publisher_` signal block. Guard on the discovery manager's state to avoid signaling before it has entered PUBLISHING:

  ```cpp
  // Place after erd_cache_mqtt_publisher_signal_work block
  if (ha_discovery_manager_.is_publishing()) {
  #ifdef USE_ESP_IDF
    ha_discovery_manager_.signal_work();
  #else
    ha_discovery_manager_.run();  // inline drain for non-ESP-IDF
  #endif
  }
  ```

- In `teardown()`, call `ha_discovery_manager_.cleanup()` **before** `erd_cache_mqtt_publisher_destroy()`. Insert it after the bridge destroys and before the ERD cache publisher destroy. The discovery manager holds pointers to both `erd_cache_` and `mqtt_client_adapter_`, which are destroyed later in the teardown sequence.
- Wire `mqtt_client_adapter_` to the discovery manager via `configure()`.

Modified: `components/geappliances_bridge/__init__.py`

- Remove the deprecation warning for `generate_device_config`
- In `to_code()`, run `generate_ha_discovery.py` and `compress_ha_discovery.py` as subprocesses to produce `ha_discovery_data.h` before ESPHome compiles. This follows the same pattern as `load_appliance_types()` which reads JSON and generates C++ inline, but uses subprocesses since the generation pipeline is multi-step (JSON → JSONL → compressed C header).
- Fallback: if subprocess approach proves unreliable in ESPHome's build sandbox, the header can be pre-generated and committed, with a manual `make` step to regenerate.

## Phase 5: Build System Integration

Modified: `Makefile` — Add `ha_discovery_data.h` as a build dependency, following the existing pattern for `erd_lists.h` and `appliance_api_feature_lists.h` (lines 82–94). The rule runs `generate_ha_discovery.py` then `compress_ha_discovery.py`. Dependencies: `appliance_api_erd_definitions.json`, `scripts/generate_ha_discovery.py`, `scripts/compress_ha_discovery.py`.

## Publishing Flow

1. Bridge waits for ERD registration to settle (10s quiet window in subscription mode, or polling list complete in polling mode)
2. Embedded JSONL byte arrays are decompressed, parsed, and filtered against the device's registered ERDs
3. Each matching entity is pushed onto a queue
4. The main loop drains the queue at 50ms intervals (one entity per interval), publishing each as a retained MQTT message
5. A nullptr sentinel signals completion

## Rate Limiting and Timing

- HA discovery quiet period: 10 seconds after last new ERD seen (subscription mode)
- Entity publish interval: 50ms between successive discovery publishes

## Entity Filtering

Only entities whose ERD ID is present in the device's registered ERD set are published. For paired request/status entities, **both** the request ERD and the status ERD must be registered for the entity to be published — the appliance must support both the command and the state feedback.

## Files Summary

| File | Action |
|------|--------|
| `scripts/generate_ha_discovery.py` | New — JSONL generation |
| `scripts/compress_ha_discovery.py` | New — Compress JSONL to C byte arrays |
| `ha_discovery/*.jsonl` | Generated — Entity definitions (intermediate, not committed) |
| `components/geappliances_bridge/ha_discovery_data.h` | Generated — Compressed byte arrays embedded in firmware |
| `components/geappliances_bridge/ha_discovery_manager.h` | New — Manager header |
| `components/geappliances_bridge/ha_discovery_manager.cpp` | New — Manager implementation |
| `components/geappliances_bridge/geappliances_bridge.h` | Modified — Add manager member |
| `components/geappliances_bridge/geappliances_bridge.cpp` | Modified — Wire into loop and teardown |
| `components/geappliances_bridge/__init__.py` | Modified — Build integration |
| `scripts/generate_erd_lists.py` | Modified — Call generate_ha_discovery |
| `Makefile` | Modified — Add build dependency for JSONL generation and compression |

## Risks & Mitigations

1. **Flash size**: Compressed JSONL adds to firmware size. Mitigation: zlib compression should reduce JSONL significantly. Only categories matching the device's ERD range are decompressed at runtime.
2. **MQTT publish blocking**: Resolved by using a background task for MQTT publishing (same pattern as `erd_cache_mqtt_publisher`). The main `loop()` only signals work, never blocks on the IDF MQTT mutex. WDT is fed in `loop()` between the protocol stack and HSM signals as it already is today.
3. **JSONL generation correctness**: Value templates derived from data types must match what Home Assistant expects. Validate against joshualongenecker's existing JSONL output. Multi-field ERDs generate separate entities per field.
4. **No re-discovery on appliance change**: Discovery payloads are published once at steady state. If the appliance changes (different model connected), there is no mechanism to re-publish discovery. This is accepted for the initial implementation; re-discovery can be added later if needed.
5. **FreeRTOS task portability**: All FreeRTOS task spawning code is guarded with `#ifdef USE_ESP_IDF`. Non-ESP-IDF builds (simulator, tests) fall back to inline execution from `loop()`.
6. **No network dependency**: Embedded approach means discovery works offline. Firmware update required to add new entity definitions.
