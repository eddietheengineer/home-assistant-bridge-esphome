# Heap Optimization Plan

## Problem Statement

ESP32-C3 devices (320 KB SRAM, no PSRAM) hit dangerously low heap during HA Discovery:

| Device | Platform | Min Free Heap | Status |
|--------|----------|---------------|--------|
| gea-esphome-dishwasher | ESP32-C3 | 6,384 B | **Critical** |
| gea-esphome-waterheaterc3 | ESP32-C3 | 13,084 B | **Critical** |
| gea-esphome-haieridu | ESP32-C3 | 20,488 B | Low |
| gea-esphome-refer | ESP32-C3 | 23,812 B | Low |
| gea-esphome-zonelinec6 | ESP32-C6 | 119,372 B | OK |
| gea-esphome-haieroduc6 | ESP32-C6 | 122,412 B | OK |
| gea-esphome-combi | ESP32-C6 | 134,648 B | OK |

ESP32-C6 devices have 512 KB SRAM vs. 320 KB on C3, which is why they have ample headroom.

## Memory Accounting (ESP32-C3, 320 KB total)

| Category | Size |
|----------|------|
| ESPHome core (WiFi/MQTT/HTTP/logging) | ~100 KB |
| Bridge static buffers (GEA2 + GEA3) | ~28 KB |
| Discovery phase additions | ~66 KB |
| MQTT internal (during discovery) | ~22 KB |
| Other state/ERD arrays | ~12 KB |
| **Total committed** | **~228 KB** |
| **Theoretical free** | **~92 KB** |
| **Actual free (dishwasher)** | **6 KB** |

The ~86 KB gap between theoretical and actual is **heap fragmentation** from ESP-IDF's TLSF allocator under many small allocations (MQTT callbacks, std::string, lambda closures, log buffers).

---

## Optimization A: Reduce Discovery Buffers (20 KB savings, low risk)

### A1. `cleanup_topic_queue`: 64×128 → 16×128 (**6 KB**)

**File**: `components/geappliances_bridge/ha_discovery_manager.h`

**Change**:
```c
// Before
#define HA_DISCOVERY_CLEANUP_QUEUE_SIZE 64
// After
#define HA_DISCOVERY_CLEANUP_QUEUE_SIZE 16
```

**Rationale**: The queue holds topic names during cleanup batch publishing. The callback adds to the queue and `cleanup_flush_queue()` drains it. With 16 entries, the worst case is one extra flush cycle. The queue is `char cleanup_topic_queue[HA_DISCOVERY_CLEANUP_QUEUE_SIZE][128]` — reducing from 64 to 16 saves 48 × 128 = 6,144 bytes.

### A2. `payload_buf`: 16 KB → 8 KB (**8 KB**)

**File**: `components/geappliances_bridge/ha_discovery_manager.h`

**Change**:
```c
// Before
#define HA_DISCOVERY_PAYLOAD_BUF_SIZE 16384
// After
#define HA_DISCOVERY_PAYLOAD_BUF_SIZE 8192
```

**Rationale**: The largest HA discovery payload is a select entity with many options, ~2-3 KB. 8 KB gives 3-4× headroom. Lines that overflow already log a warning and skip (`goto too_large` in `process_jsonl_line`).

### A3. `decomp_buf`: 16 KB → 14 KB (**2 KB**)

**File**: `components/geappliances_bridge/ha_discovery_manager.h`

**Change**:
```c
// Before
#define HA_DISCOVERY_DECOMP_BUF_SIZE 16384
// After
#define HA_DISCOVERY_DECOMP_BUF_SIZE 14336
```

**Rationale**: The compression script targets 14 KB decompressed chunks (`CHUNK_TARGET_DECOMPRESSED = 14000`). 14,336 (14×1024) gives margin for the last partial chunk.

### A4. `line_buf`: 16 KB → 14 KB (**2 KB**)

**File**: `components/geappliances_bridge/ha_discovery_manager.h`

**Change**:
```c
// Before
#define HA_DISCOVERY_LINE_BUF_SIZE 16384
// After
#define HA_DISCOVERY_LINE_BUF_SIZE 14336
```

**Rationale**: Max JSONL line is bounded by chunk size (~14 KB).

### A5. Discovery task stack: 4 KB → 2 KB (**2 KB**)

**File**: `components/geappliances_bridge/ha_discovery_manager.cpp`

**Change**:
```c
// Before
static constexpr int STACK_SIZE_BIG = 4 * 1024;
// After
static constexpr int STACK_SIZE_BIG = 2 * 1024;
```

**Rationale**: `build_task()` only calls `build_sorted_erd_list()` (iterates cache entries, does insertion sort) and `build_device_json()` (snprintf into struct buffer). Both use struct buffers, minimal stack. The fallback `STACK_SIZE_SMALL` is already 2 KB.

---

## Optimization B: Early Resource Deallocation (4 KB savings, low risk)

**File**: `components/geappliances_bridge/ha_discovery_manager.cpp`

**Change**: In `ha_discovery_manager_run()`, after the build task completes (when transitioning from `ha_discovery_state_building` to `ha_discovery_state_cleaning`), immediately free `task_stack` and `task_tcb`:

```c
// After setting build_done = true and before transitioning to cleaning:
free(self->task_stack);
free(self->task_tcb);
self->task_stack = NULL;
self->task_tcb = NULL;
```

**Rationale**: The build task allocates 4 KB stack + 304 B TCB, then deletes itself. Currently `cleanup_resources()` is only called at the very end of discovery. Freeing immediately after build completes gives back 4,400 bytes during the cleanup + discovery phases — the period of highest memory pressure.

---

## Optimization C: Eliminate std::string Heap Allocations (fragmentation fix, low risk)

### C1. MQTT adapter `device_id`

**File**: `components/geappliances_bridge/esphome_mqtt_client_adapter.h`

**Change**:
```c
// Before
std::string* device_id;
// After
const char* device_id;
```

**File**: `components/geappliances_bridge/esphome_mqtt_client_adapter.cpp`

**Change in `esphome_mqtt_client_adapter_init()`**:
```c
// Before
if (self->device_id != nullptr) {
    delete self->device_id;
    self->device_id = nullptr;
}
self->device_id = new std::string(device_id);

// After
self->device_id = device_id;
```

**Change in `esphome_mqtt_client_adapter_destroy()`**:
```c
// Before
if (self->device_id != nullptr) {
    delete self->device_id;
    self->device_id = nullptr;
}
// After
self->device_id = nullptr;
```

**Change all usages of `self->device_id->c_str()` to `self->device_id`**.

**Rationale**: `device_id` is a stable string from `DeviceIdentityManager` that lives for the adapter's lifetime. The `new std::string` heap allocation is unnecessary and contributes to fragmentation.

### C2. Bridge `configured_device_id_`

**File**: `components/geappliances_bridge/geappliances_bridge.h`

**Change**:
```cpp
// Before
std::string configured_device_id_;
// After
char configured_device_id_[64];
```

**Rationale**: Device IDs are short strings (< 64 chars). Using a fixed buffer eliminates a heap allocation. All setter/getter methods need updating to use `strcpy`/`strcmp` instead of `std::string` methods.

---

## Optimization D: Reduce GEA2 Buffers (14 KB savings, medium risk)

### D1. `gea2_send_queue_buffer_`: 10 KB → 4 KB (**6 KB**)

**File**: `components/geappliances_bridge/geappliances_bridge.h`

**Change**:
```cpp
// Before
uint8_t gea2_send_queue_buffer_[10000];
// After
uint8_t gea2_send_queue_buffer_[4096];
```

**Rationale**: GEA2 at 19200 baud sends small commands. 4 KB handles burst of ~40 read requests in flight.

### D2. `gea2_client_queue_buffer_`: 8 KB → 4 KB (**4 KB**)

**File**: `components/geappliances_bridge/geappliances_bridge.h`

**Change**:
```cpp
// Before
uint8_t gea2_client_queue_buffer_[8096];
// After
uint8_t gea2_client_queue_buffer_[4096];
```

**Rationale**: Same as GEA3 — 4 KB handles the polling bridge needs.

### D3. `client_queue_buffer_`: 8 KB → 4 KB (**4 KB**)

**File**: `components/geappliances_bridge/geappliances_bridge.h`

**Change**:
```cpp
// Before
uint8_t client_queue_buffer_[8192];
// After
uint8_t client_queue_buffer_[4096];
```

**Rationale**: The comment says it was increased from 1024 to 8192 to prevent ring-buffer overflow when polling bridge and subscription bridge share the ERD client. But 4 KB should be sufficient for ~31 reads × 6 bytes = 186 bytes of actual read data, plus subscription acks. The overflow issue was with polling+subscription sharing the client — 4 KB gives ~500 request slots.

---

## Impact Projection

### Conservative (A + B + C): ~25 KB savings

| Device | Current | After | Improvement |
|--------|---------|-------|-------------|
| dishwasher | 6,384 B | 31,984 B | +25,600 B |
| waterheaterc3 | 13,084 B | 38,684 B | +25,600 B |
| haieridu | 20,488 B | 46,088 B | +25,600 B |
| refer | 23,812 B | 49,412 B | +25,600 B |

### Aggressive (A + B + C + D): ~39 KB savings

| Device | Current | After | Improvement |
|--------|---------|-------|-------------|
| dishwasher | 6,384 B | 46,320 B | +39,936 B |
| waterheaterc3 | 13,084 B | 53,020 B | +39,936 B |
| haieridu | 20,488 B | 60,424 B | +39,936 B |
| refer | 23,812 B | 63,748 B | +39,936 B |

---

## Implementation Order

1. **Phase 1 (A)**: Reduce discovery buffer sizes in `ha_discovery_manager.h` and `ha_discovery_manager.cpp`. Test compile for all devices.
2. **Phase 2 (B)**: Add early deallocation in `ha_discovery_manager_run()`. Test that discovery still completes.
3. **Phase 3 (C)**: Replace `std::string*` with `const char*` in MQTT adapter. Update all call sites.
4. **Phase 4 (D)**: Reduce GEA2 buffer sizes. Test with GEA2 appliances to ensure no ring-buffer overflow.

## Verification

After each phase:
1. Compile all device configurations (ESP32-C3 and ESP32-C6).
2. Flash to the most constrained device (dishwasher on ESP32-C3).
3. Monitor `Heap Min Free` during HA Discovery phase.
4. Verify all discovery entities are published (no `too_large` warnings).
5. Verify GEA2 communication stability (no ring-buffer overflow).

## Risks

- **A2 (payload_buf 8 KB)**: If any single entity payload exceeds 8 KB, it will be silently skipped with a warning. Mitigation: monitor logs for `Payload too large` warnings.
- **D3 (client_queue_buffer 4 KB)**: If polling + subscription share the ERD client and queue depth exceeds 4 KB, ring-buffer overflow corrupts adjacent heap metadata. Mitigation: monitor for `prvCheckTasksWaitingTermination` crashes.
