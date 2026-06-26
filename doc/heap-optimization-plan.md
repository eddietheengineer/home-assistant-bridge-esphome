# Heap Optimization Plan

## Problem Statement

ESP32-C3 devices (320 KB SRAM, no PSRAM) hit dangerously low heap during HA Discovery:

| Device | Platform | Min Free Heap | Fragmentation | Status |
|--------|----------|---------------|---------------|--------|
| gea-esphome-dishwasher | ESP32-C3 | 6,384 B | **48.2%** | **Critical** |
| gea-esphome-waterheaterc3 | ESP32-C3 | 13,084 B | **48.4%** | **Critical** |
| gea-esphome-haieridu | ESP32-C3 | 20,488 B | **46.8%** | Low |
| gea-esphome-refer | ESP32-C3 | 23,812 B | **42.9%** | Low |
| gea-esphome-zonelinec6 | ESP32-C6 | 119,372 B | 18.4% | OK |
| gea-esphome-haieroduc6 | ESP32-C6 | 122,412 B | 21.1% | OK |
| gea-esphome-combi | ESP32-C6 | 134,648 B | 12.8% | OK |

ESP32-C6 devices have 512 KB SRAM vs. 320 KB on C3, which is why they have ample headroom.

**Fragmentation correlation**: ESP32-C3 devices consistently show 42-48% fragmentation vs 12-21% on ESP32-C6. This confirms that the tight memory budget on C3 amplifies TLSF allocator fragmentation — every small allocation (std::string, MQTT callback closures, log buffers) creates holes that can't be coalesced.

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

### Fragmentation Measurement

Fragmentation ratio = `(1 - largest_free_block / total_free) * 100`.

Baseline measurements during HA Discovery:

| Device | Fragmentation |
|--------|---------------|
| ESP32-C3 (all) | 42.9 - 48.4% |
| ESP32-C6 (all) | 12.8 - 21.1% |

After each optimization phase, re-measure to validate improvement.

```c
// In ha_discovery_manager_run() or a dedicated diagnostic build:
size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
ESP_LOGI(TAG, "Heap: free=%u, largest_block=%u, fragmentation_ratio=%.1f%%",
    (unsigned)free_heap, (unsigned)largest_free,
    (1.0 - (double)largest_free / free_heap) * 100.0);
```

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

**Risk**: Stack overflow on ESP32-C3 is a hard crash with no recovery.

**Mitigation**: Before shrinking, measure actual stack watermark:
```c
// In build_task(), after work is done but before vTaskDelete:
UBaseType_t high_watermark = uxTaskGetStackHighWaterMark(NULL);
ESP_LOGI(TAG, "build_task stack high_watermark: %lu words (%lu bytes)",
    (unsigned long)high_watermark, (unsigned long)(high_watermark * sizeof(StackType_t)));
```
If watermark is >1 KB (256 words on ESP32), keep at 4 KB or investigate the deep call. If <1 KB, 2 KB is safe.

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

**Double-free safety**: `cleanup_resources()` already null-checks before freeing (lines 939-946):
```c
if (self->task_stack) {
    free(self->task_stack);
    self->task_stack = NULL;
}
if (self->task_tcb) {
    free(self->task_tcb);
    self->task_tcb = NULL;
}
```
Setting both to `NULL` after the early free makes the later `cleanup_resources()` call a no-op. No double-free risk.

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
const char* configured_device_id_{nullptr};
```

**Rationale**: The reviewer correctly noted that `char configured_device_id_[64]` increases the `GeappliancesBridge` struct size by ~36 bytes net (64 bytes inline minus 28-byte `std::string` SSO buffer), which is counterproductive on a memory-constrained device. Using `const char*` instead:

- Points to the same stable string from ESPHome's YAML config (the `set_device_id(const std::string&)` setter receives a `std::string` from ESPHome, but we can store just the pointer to its internal buffer — ESPHome keeps the string alive for the component's lifetime)
- Adds only a pointer (4 bytes on 32-bit) to the struct instead of a 28-byte `std::string`
- Net struct size **reduction** of ~24 bytes
- Eliminates the heap allocation entirely

**Setter change**:
```cpp
// Before
void set_device_id(const std::string &device_id) { this->configured_device_id_ = device_id; }
// After
// Lifetime: stores a pointer into the caller's std::string. Safe because
// ESPHome's YAML config strings are stored as std::string members on the
// component or as string literals, outliving the bridge component.
// Do NOT pass a temporary std::string (e.g. set_device_id("literal") is fine
// via implicit conversion, but set_device_id(std::string("x")) is NOT).
void set_device_id(const std::string &device_id) { this->configured_device_id_ = device_id.c_str(); }
```

**All usage sites** change from `.empty()` / `.c_str()` to `!= nullptr` / direct use:
- `geappliances_bridge.cpp:171-172`: `!this->configured_device_id_.empty()` → `this->configured_device_id_ != nullptr`
- `geappliances_bridge.cpp:510-511`: same pattern
- `geappliances_bridge.cpp:667`: pass `configured_device_id_` (already a `const char*`)
- `device_identity_manager.cpp:27,32,34,79,80`: update `configured_device_id_` type from `std::string` to `const char*`

### C3. DeviceIdentityManager strings

**File**: `components/geappliances_bridge/device_identity_manager.h`

**Change**:
```cpp
// Before
std::string configured_device_id_;
std::string generated_device_id_;
std::string model_number_;
// After
const char* configured_device_id_{nullptr};
char generated_device_id_[64];
char model_number_[64];
```

**Rationale**: `generated_device_id_` and `model_number_` are constructed from ERD data at runtime. Device IDs are short (< 64 chars). `model_number_` from ERD 0x0001 is typically 8-16 chars. Using fixed buffers eliminates 2 heap allocations. The `configured_device_id_` is `const char*` pointing to the YAML-provided string (see C2).

**Note**: `get_device_id()` currently returns `std::string`. This can remain as a temporary conversion for callers, or callers can be updated to accept `const char*`.

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

**Risk**: Ring-buffer overflow corrupts adjacent heap metadata — the hardest class of embedded bug to debug. The current 8 KB was specifically increased to fix a real overflow issue.

**Mitigation**: Before shrinking, add queue depth telemetry:
```c
// In the ERD client ring buffer implementation, log high-water mark:
static size_t g_queue_high_water = 0;
// On each enqueue:
if (queue_used > g_queue_high_water) {
    g_queue_high_water = queue_used;
}
// Log periodically:
ESP_LOGI(TAG, "client_queue high_water: %u bytes", (unsigned)g_queue_high_water);
```
If high-water mark is consistently < 2 KB, 4 KB is safe. If it approaches 4 KB, keep at 8 KB and find other savings.

---

## Impact Projection

### Arithmetic

- **A** saves 20 KB of raw static buffer bytes (6+8+2+2+2).
- **B** saves 4 KB by returning heap earlier (not raw bytes, but earlier availability).
- **C** saves ~1 KB of actual heap allocations (the `new std::string` objects) but reduces **fragmentation** — the 42-48% fragmentation on ESP32-C3. Reducing allocation count from N to N-3 on a TLSF allocator typically improves the largest-free-block ratio measurably.
- **D** saves 14 KB of raw static buffer bytes (6+4+4).

### Conservative (A + B + C): ~25 KB effective savings

The 25 KB figure = A(20 KB raw) + B(4 KB early return) + C(~1 KB raw + fragmentation improvement). The fragmentation improvement from C is the hard-to-quantify portion — it may recover an additional 5-10 KB of usable contiguous heap by reducing the number of small allocations that fragment the TLSF pool.

| Device | Current Heap | Current Frag. | After (est.) | Improvement |
|--------|-------------|---------------|--------------|-------------|
| dishwasher | 6,384 B | 48.2% | ~31,984 B | ~+25,600 B |
| waterheaterc3 | 13,084 B | 48.4% | ~38,684 B | ~+25,600 B |
| haieridu | 20,488 B | 46.8% | ~46,088 B | ~+25,600 B |
| refer | 23,812 B | 42.9% | ~49,412 B | ~+25,600 B |

### Aggressive (A + B + C + D): ~39 KB effective savings

20 + 4 + 14 = 38 KB raw, plus fragmentation improvement from C.

| Device | Current Heap | Current Frag. | After (est.) | Improvement |
|--------|-------------|---------------|--------------|-------------|
| dishwasher | 6,384 B | 48.2% | ~46,320 B | ~+39,936 B |
| waterheaterc3 | 13,084 B | 48.4% | ~53,020 B | ~+39,936 B |
| haieridu | 20,488 B | 46.8% | ~60,424 B | ~+39,936 B |
| refer | 23,812 B | 42.9% | ~63,748 B | ~+39,936 B |

---

## Implementation Order

1. **Phase 0 (Measurement)**: Add fragmentation telemetry (`heap_caps_get_largest_free_block` / `heap_caps_get_free_size`) and stack watermark logging (`uxTaskGetStackHighWaterMark`) to establish baselines.
2. **Phase 1 (A)**: Reduce discovery buffer sizes in `ha_discovery_manager.h` and `ha_discovery_manager.cpp`. Test compile for all devices.
3. **Phase 2 (B)**: Add early deallocation in `ha_discovery_manager_run()`. Test that discovery still completes.
4. **Phase 3 (C)**: Replace `std::string*` / `std::string` with `const char*` / fixed buffers in MQTT adapter, bridge, and device identity manager. Update all call sites.
5. **Phase 4 (D)**: Reduce GEA2 buffer sizes. Test with GEA2 appliances to ensure no ring-buffer overflow.

## Verification

After each phase:
1. Compile all device configurations (ESP32-C3 and ESP32-C6).
2. Flash to the most constrained device (dishwasher on ESP32-C3).
3. Monitor `Heap Min Free` during HA Discovery phase.
4. Log fragmentation metrics (free heap, largest free block, fragmentation ratio).
5. Verify all discovery entities are published (no `too_large` warnings).
6. Verify GEA2 communication stability (no ring-buffer overflow).

## Risks

- **A2 (payload_buf 8 KB)**: If any single entity payload exceeds 8 KB, it will be silently skipped with a warning. Mitigation: monitor logs for `Payload too large` warnings.
- **A5 (task stack 2 KB)**: Stack overflow is a hard crash. Mitigation: measure watermark before shrinking (see A5 mitigation above).
- **D3 (client_queue_buffer 4 KB)**: Ring-buffer overflow corrupts adjacent heap metadata — the hardest class of embedded bug to debug. Mitigation: measure queue high-water mark before shrinking (see D3 mitigation above).

## Future Considerations

### PSRAM

Neither ESP32-C3 nor ESP32-C6 (Xiao modules) have PSRAM. If future hardware uses PSRAM-equipped variants, these buffers could be placed in PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`:
- Discovery buffers (decomp_buf, line_buf, payload_buf) are ideal PSRAM candidates — they are large, sequentially accessed, and not latency-critical.
- GEA2/GEA3 queue buffers should remain in internal SRAM due to DMA/ISR access requirements.
