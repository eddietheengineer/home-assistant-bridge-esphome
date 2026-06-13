# GE Appliances Bridge Codebase Review — Improvement Plan

**Date:** 2026-06-13  
**Scope:** Application code only (no library changes)  
**Goal:** A+ code quality  

---

## Table of Contents

1. [Architecture & Design Improvements](#1-architecture--design-improvements)
2. [Memory Management Improvements](#2-memory-management-improvements)
3. [Performance Optimizations](#3-performance-optimizations)
4. [Code Quality & Maintainability](#4-code-quality--maintainability)
5. [Safety & Reliability](#5-safety--reliability)
6. [Testing Improvements](#6-testing-improvements)

---

## 1. Architecture & Design Improvements

### 1.1 Replace `using namespace std;` with Explicit Namespaces

**Severity:** Medium  
**Files:** `mqtt_bridge_polling.cpp:30`, `mqtt_bridge.cpp:16`

**Problem:**  
Both files use `using namespace std;` which pollutes the global namespace and can cause name collisions with other libraries or future ESPHome updates.

**Solution:**  
Replace with explicit `std::` prefixes or targeted using declarations:

```cpp
// Before:
using namespace std;

// After (at top of file):
using std::map;
using std::set;
using std::string;
using std::vector;
```

**Implementation:**
- `mqtt_bridge_polling.cpp`: Add using declarations for `std::map`, `std::set`, `std::string`, `std::vector`, `std::isxdigit`
- `mqtt_bridge.cpp`: Add using declaration for `std::set`

---

### 1.2 Consolidate Duplicate State Machine Signal Handlers

**Severity:** Low  
**Files:** `mqtt_bridge_polling.cpp`

**Problem:**  
Lines 649-704 contain significant code duplication in `state_polling`. The cycle completion logic is repeated 4 times (lines 649-673, 682-704, and similar patterns in signal_read_failed).

**Solution:**  
Extract the cycle completion logic into a helper function:

```cpp
static void handle_cycle_completion(mqtt_bridge_polling_t* self)
{
  self->last_cycle_time_ms = (uint32_t)(esphome::millis() - self->cycle_start_ms);
  self->cycle_count++;
  
  if (self->restart_pending) {
    self->restart_pending = false;
    self->erd_index = 0;
    self->cycle_completed_count = 0;
    self->cycle_start_ms = esphome::millis();
    arm_polling_timer(self, self->polling_interval_ms);
    while (self->erd_index < self->polling_list_count) {
      send_poll_read_requests_bounded(self, POLL_YIELD_MS);
      esphome::delay(0);
    }
  } else if (!self->polling_timer_armed) {
    self->erd_index = 0;
    self->cycle_completed_count = 0;
    self->cycle_start_ms = esphome::millis();
    while (self->erd_index < self->polling_list_count) {
      send_poll_read_requests_bounded(self, POLL_YIELD_MS);
      esphome::delay(0);
    }
  }
  // else: timer still armed — wait for it to fire and restart.
}
```

**Implementation:**
1. Add `handle_cycle_completion()` helper before `state_polling`
2. Replace the 4 duplicated blocks with calls to the helper
3. Verify existing tests pass

---

### 1.3 Simplify Feature Bit Manager's State Machine

**Severity:** Low  
**Files:** `feature_bit_manager.cpp`

**Problem:**  
Lines 253-268 and 295-313 have duplicate switch statements mapping states to ERD values. The same mapping appears in `queue_erd_read_()`, `get_expected_erd_()`, and `skip_to_next_erd_()`.

**Solution:**  
Create a static state-to-ERD mapping table:

```cpp
static constexpr struct {
  FeatureBitState state;
  tiny_erd_t erd;
  const char* name;
} STATE_ERD_MAP[] = {
  { FEATURE_BIT_STATE_READING_0008, ERD_APPLIANCE_TYPE, "appliance type" },
  { FEATURE_BIT_STATE_READING_0001, ERD_MODEL_NUMBER, "model number" },
  { FEATURE_BIT_STATE_READING_0002, ERD_SERIAL_NUMBER, "serial number" },
  // ... all states
};
```

**Implementation:**
1. Define the mapping table as static constexpr
2. Replace switch statements with linear search or binary search through the table
3. Reduces code by ~60 lines and eliminates duplication

---

## 2. Memory Management Improvements

### 2.1 Replace Raw `new`/`delete` with Smart Pointers

**Severity:** High  
**Files:** `esphome_mqtt_client_adapter.cpp`, `mqtt_bridge_polling.cpp`, `mqtt_bridge.cpp`

**Problem:**  
Multiple files use raw `new`/`delete` for heap allocations. While cleanup is handled in destroy functions, exceptions during initialization could leak memory.

**Locations:**
- `esphome_mqtt_client_adapter.cpp:179-180`: `new std::string`, `new std::map`
- `mqtt_bridge_polling.cpp:787-789`: `new set`, `new map`
- `mqtt_bridge.cpp:164`: `new set`
- `mqtt_bridge_polling.cpp:91`: `new tiny_erd_t[new_capacity]`

**Solution:**  
Replace with `std::unique_ptr` where possible:

```cpp
// Before:
self->device_id = new std::string(device_id);
self->pending_updates = new std::map<tiny_erd_t, PendingErdUpdate>();

// After:
self->device_id = std::make_unique<std::string>(device_id);
self->pending_updates = std::make_unique<std::map<tiny_erd_t, PendingErdUpdate>>();
```

**Implementation:**
1. Add `#include <memory>` to affected files
2. Change member types to `std::unique_ptr<T>`
3. Replace `new` with `std::make_unique`
4. Remove manual `delete` calls from destroy functions
5. Verify tests pass

**Note:** For `mqtt_bridge_polling.cpp:91`, use `std::vector<tiny_erd_t>` instead of manual array management.

---

### 2.2 Use `std::vector` Instead of Manual Array Management

**Severity:** Medium  
**Files:** `mqtt_bridge_polling.cpp`

**Problem:**  
Lines 80-101 manage a dynamic array with manual `new[]`/`delete[]`, capacity tracking, and growth logic. This is error-prone and verbose.

**Current code:**
```cpp
static void ensure_polling_list_capacity(mqtt_bridge_polling_t* self, uint16_t needed)
{
  if (needed <= self->polling_list_capacity) {
    return;
  }
  uint16_t new_capacity = needed + (POLLING_LIST_GROWTH_INCREMENT - 1);
  if (new_capacity > POLLING_LIST_MAX_SIZE) {
    new_capacity = POLLING_LIST_MAX_SIZE;
  }
  tiny_erd_t* new_list = new tiny_erd_t[new_capacity];
  if (self->erd_polling_list) {
    for (uint16_t i = 0; i < self->polling_list_count; i++) {
      new_list[i] = self->erd_polling_list[i];
    }
    delete[] self->erd_polling_list;
  }
  self->erd_polling_list = new_list;
  self->polling_list_capacity = new_capacity;
}
```

**Solution:**  
Replace with `std::vector<tiny_erd_t>`:

```cpp
// In mqtt_bridge_polling.h:
// Change:
tiny_erd_t* erd_polling_list;
uint16_t polling_list_count;
uint16_t polling_list_capacity;

// To:
std::vector<tiny_erd_t> erd_polling_list;
```

**Implementation:**
1. Change struct member to `std::vector<tiny_erd_t>`
2. Replace `ensure_polling_list_capacity()` with `reserve()` call
3. Replace `erd_polling_list[count++] = erd` with `erd_polling_list.push_back(erd)`
4. Replace `erd_polling_list_count` with `erd_polling_list.size()`
5. Remove manual `delete[]` in destroy function

---

### 2.3 Add RAII Guard for Timer Operations

**Severity:** Low  
**Files:** All files using `tiny_timer_*` functions

**Problem:**  
Timers are manually started and stopped. If code exits early (exception or early return), timers may remain armed.

**Solution:**  
Create an RAII timer guard:

```cpp
class TimerGuard {
  tiny_timer_group_t* group_;
  tiny_timer_t* timer_;
  bool armed_;
  
public:
  TimerGuard(tiny_timer_group_t* group, tiny_timer_t* timer) 
    : group_(group), timer_(timer), armed_(false) {}
    
  ~TimerGuard() {
    if (armed_) {
      tiny_timer_stop(group_, timer_);
    }
  }
  
  void arm(tiny_timer_ticks_t ticks, void* context, 
           tiny_timer_callback_t callback) {
    tiny_timer_start(group_, timer_, ticks, context, callback);
    armed_ = true;
  }
};
```

**Implementation:**
1. Add `TimerGuard` class to `mqtt_bridge_common.h`
2. Use guards in timer-heavy functions

---

## 3. Performance Optimizations

### 3.1 Optimize String Concatenation in ERD Update

**Severity:** Medium  
**Files:** `esphome_mqtt_client_adapter.cpp`

**Problem:**  
Lines 99-105 build hex string with repeated small allocations:

```cpp
std::string payload;
payload.reserve(size * 2);
for (uint8_t i = 0; i < size; i++) {
  char hex[3];
  snprintf(hex, sizeof(hex), "%02X", bytes[i]);
  payload += hex;  // Repeated allocation
}
```

**Solution:**  
Use direct character append with lookup table:

```cpp
static constexpr char HEX_CHARS[] = "0123456789ABCDEF";

// In update_erd():
std::string payload;
payload.reserve(size * 2);
for (uint8_t i = 0; i < size; i++) {
  payload.push_back(HEX_CHARS[(bytes[i] >> 4) & 0x0F]);
  payload.push_back(HEX_CHARS[bytes[i] & 0x0F]);
}
```

**Implementation:**
1. Add `HEX_CHARS` table as static constexpr
2. Replace `snprintf` loop with direct character appends
3. Expected improvement: ~5x faster hex conversion

---

### 3.2 Pre-allocate Topic Buffer for MQTT Operations

**Severity:** Low  
**Files:** `esphome_mqtt_client_adapter.cpp`

**Problem:**  
`build_topic()` creates a new string on every call. Topic format is predictable.

**Solution:**  
Use stack-allocated buffer with `snprintf`:

```cpp
static const char* TOPIC_TEMPLATE = "geappliances/%s/erd/0x%04x/value";

void update_erd(i_mqtt_client_t* _self, tiny_erd_t erd, const void* value, uint8_t size) {
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  char topic_buf[128];
  int len = snprintf(topic_buf, sizeof(topic_buf), TOPIC_TEMPLATE,
                     self->device_id->c_str(), erd);
  if (len >= sizeof(topic_buf)) {
    ESP_LOGW(TAG, "Topic buffer overflow");
    return;
  }
  
  // Use topic_buf directly instead of std::string
  // Note: requires changes to pending update storage
}
```

**Implementation:**
1. Add topic buffer size constant (128 bytes)
2. Change `PendingErdUpdate` struct to use `std::array<char, 128>` for topic
3. Update all topic construction to use stack buffers
4. Verify all topic lengths fit within buffer

---

### 3.3 Optimize Feature Bit Parsing with Bulk Operations

**Severity:** Low  
**Files:** `feature_bit_manager.cpp`

**Problem:**  
Lines 416-428 parse common features one descriptor at a time. For large feature sets, this is slow.

**Solution:**  
Increase `COMMON_PARSE_PER_CALL` or use `std::set::insert` with range:

```cpp
// Current:
for (uint16_t i = start; i < end; i++) {
  const auto& desc = common_feature_descriptors[i];
  if (common_bits & desc.bit_mask) {
    for (uint16_t j = 0; j < desc.erd_count; j++) {
      this->valid_erds_.insert(desc.erds[j]);
    }
  }
}

// Optimized:
std::vector<tiny_erd_t> batch_erds;
for (uint16_t i = start; i < end; i++) {
  const auto& desc = common_feature_descriptors[i];
  if (common_bits & desc.bit_mask) {
    batch_erds.insert(batch_erds.end(), desc.erds, desc.erds + desc.erd_count);
  }
}
this->valid_erds_.insert(batch_erds.begin(), batch_erds.end());
```

**Implementation:**
1. Add batch collection vector
2. Collect all ERDs for the tick, then bulk insert
3. Expected improvement: 2-3x faster for large feature sets

---

## 4. Code Quality & Maintainability

### 4.1 Add Missing Virtual Destructor to GeappliancesBridge

**Severity:** Medium  
**Files:** `geappliances_bridge.h`

**Problem:**  
`GeappliancesBridge` inherits from `Component` but doesn't declare a virtual destructor. This is safe because `Component` has a virtual destructor, but explicit declaration improves clarity and catches future issues.

**Solution:**  
Add virtual destructor:

```cpp
class GeappliancesBridge : public Component, public IBridgeServices {
 public:
  // ... existing members ...
  
  virtual ~GeappliancesBridge() = default;
};
```

**Implementation:**
1. Add `virtual ~GeappliancesBridge() = default;` to class definition
2. No runtime behavior change, purely defensive

---

### 4.2 Improve Error Handling for MQTT Client Operations

**Severity:** Medium  
**Files:** `esphome_mqtt_client_adapter.cpp`

**Problem:**  
Lines 116-122 silently drop updates when queue is full. No metric or callback to track this.

**Solution:**  
Add overflow counter and warning threshold:

```cpp
// In esphome_mqtt_client_adapter.h:
size_t dropped_updates_{0};
static constexpr size_t DROP_THRESHOLD = 100;

// In update_erd():
if (self->pending_updates == nullptr) {
  ESP_LOGW(TAG, "Pending updates queue not initialized, dropping ERD update for 0x%04X", erd);
} else {
  self->dropped_updates_++;
  if (self->dropped_updates_ % DROP_THRESHOLD == 0) {
    ESP_LOGW(TAG, "Dropped %zu ERD updates in last %zu period",
             self->dropped_updates_, DROP_THRESHOLD);
    self->dropped_updates_ = 0;
  }
}
```

**Implementation:**
1. Add `dropped_updates_` counter to adapter struct
2. Add periodic warning logging
3. Optionally expose via ESPHome sensor

---

### 4.3 Add Input Validation for MQTT Write Requests

**Severity:** Medium  
**Files:** `esphome_mqtt_client_adapter.cpp`

**Problem:**  
Lines 264-272 validate hex payload but don't check for excessively large payloads.

**Solution:**  
Add maximum payload size check:

```cpp
static constexpr size_t MAX_MQTT_WRITE_PAYLOAD = 255;

// Before hex decoding:
if (payload.length() > MAX_MQTT_WRITE_PAYLOAD * 2) {
  ESP_LOGW(TAG, "MQTT write payload too large: %zu bytes", payload.length());
  return;
}
```

**Implementation:**
1. Add `MAX_MQTT_WRITE_PAYLOAD` constant
2. Add length check before hex decoding
3. Update tests to verify rejection

---

### 4.4 Standardize Logging Format

**Severity:** Low  
**Files:** All source files

**Problem:**  
Log messages use inconsistent formats:
- Some use `ESP_LOGD` for debug
- Some use `ESP_LOGI` for informational
- Some use `ESP_LOGW` for warnings

**Solution:**  
Define a logging policy:
- `ESP_LOGE`: Fatal errors (null pointers, allocation failures)
- `ESP_LOGW`: Recoverable errors (queue full, timeouts, invalid input)
- `ESP_LOGI`: Important state changes (connection, discovery complete)
- `ESP_LOGD`: Routine operations (reads, writes, state transitions)
- `ESP_LOGV`: Detailed tracing (byte-level operations)

**Implementation:**
1. Add logging policy to `geappliances_bridge_constants.h`
2. Review and standardize log levels across all files
3. Add consistent formatting for ERD numbers (`0x%04X`)

---

## 5. Safety & Reliability

### 5.1 Add Stack Overflow Protection for HA Discovery Task

**Severity:** High  
**Files:** `ha_discovery_manager.cpp`

**Problem:**  
Lines 172-173 allocate 48KB stack for HA discovery task but don't verify free heap before allocation.

**Solution:**  
Add comprehensive heap checks:

```cpp
static constexpr size_t HA_FETCH_MIN_FREE_HEAP = 150 * 1024;  // Increased from 110KB
static constexpr uint32_t HA_FETCH_STACK_SIZE = 49152;

size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
size_t required = HA_FETCH_STACK_SIZE + sizeof(StaticTask_t) + 1024;  // Extra overhead

if (free_heap < HA_FETCH_MIN_FREE_HEAP || largest_block < required) {
  ESP_LOGW(TAG, "HA discovery: insufficient heap (free=%zu, largest=%zu, required=%zu)",
           free_heap, largest_block, required);
  this->state_ = HA_DISCOVERY_COMPLETE;
  return;
}
```

**Implementation:**
1. Increase `HA_FETCH_MIN_FREE_HEAP` to 150KB
2. Add required size calculation with overhead
3. Add detailed logging for heap state
4. Verify stack usage with `uxTaskGetStackHighWaterMark` in task

---

### 5.2 Add Mutex Protection for Shared Data

**Severity:** Medium  
**Files:** `esphome_mqtt_client_adapter.cpp`, `ha_discovery_manager.cpp`

**Problem:**  
HA discovery task and main loop access shared data without synchronization:
- `pending_updates` map in MQTT adapter
- `seen_erds_` set in HA discovery manager

**Solution:**  
Add `std::mutex` protection:

```cpp
// In esphome_mqtt_client_adapter.h:
std::mutex pending_mutex_;

// In update_erd():
{
  std::lock_guard<std::mutex> lock(self->pending_mutex_);
  if (self->pending_updates != nullptr && self->pending_updates->size() < MAX_PENDING_UPDATES) {
    (*self->pending_updates)[erd] = {topic, payload};
  }
}

// In drain_pending_updates():
{
  std::lock_guard<std::mutex> lock(self->pending_mutex_);
  // ... drain logic
}
```

**Implementation:**
1. Add `#include <mutex>` to affected files
2. Add mutex members to structs
3. Wrap shared data access with lock guards
4. Verify no deadlocks in existing code paths

---

### 5.3 Add Timeout Protection for Long-Running Operations

**Severity:** Medium  
**Files:** `geappliances_bridge.cpp`

**Problem:**  
GEA2 tight loop (lines 324-378) runs for 200ms and can block the ESPHome watchdog.

**Solution:**  
Add per-iteration timeout check:

```cpp
while (millis() - loop_start_ms < GEA2_LOOP_DURATION_MS) {
  // Safety break: if we've exceeded the hard cap, exit immediately.
  if (millis() - loop_start_ms >= GEA2_LOOP_HARD_CAP_MS) {
    ESP_LOGW(TAG, "GEA2 tight loop exceeded hard cap (%u ms), breaking",
             static_cast<unsigned>(GEA2_LOOP_HARD_CAP_MS));
    break;
  }
  
  // Additional check: if main loop is blocking for too long, break
  if (esphome::millis() - last_loop_time_ > 1000) {
    ESP_LOGW(TAG, "Main loop blocked for >1s, breaking GEA2 tight loop");
    break;
  }
  
  // ... existing loop body
}
```

**Implementation:**
1. Add `last_loop_time_` tracking variable
2. Update in `loop()` entry
3. Add timeout check in tight loop
4. Verify watchdog doesn't trigger

---

## 6. Testing Improvements

### 6.1 Add Integration Test for HA Discovery

**Severity:** Medium  
**Files:** `test/tests/`

**Problem:**  
HA discovery has complex task management and HTTP fetching but minimal test coverage.

**Solution:**  
Create comprehensive test file:

```cpp
// test/tests/ha_discovery_manager_test.cpp
#include "CppUTest/CommandLineTestRunner.h"
#include "ha_discovery_manager.h"
#include "mocks.h"  // Mock MQTT client, HTTP client

TEST_GROUP(HaDiscoveryManagerTest) {
  HaDiscoveryManager manager;
  MockMqttClient mqtt_client;
  MockHttpClient http_client;
};

TEST(HaDiscoveryManagerTest, TestHeapCheckPreventsTaskCreation) {
  // Mock low heap condition
  // Verify task is not created
}

TEST(HaDiscoveryManagerTest, TestTaskTerminationOnCleanup) {
  // Create task
  // Call cleanup
  // Verify task terminates
}

TEST(HaDiscoveryManagerTest, TestQueueDrainOnCleanup) {
  // Create task with items in queue
  // Call cleanup
  // Verify all items are freed
}
```

**Implementation:**
1. Create mock objects for MQTT client, HTTP client
2. Add test cases for:
   - Heap check prevention
   - Task termination
   - Queue drain on cleanup
   - Entity publishing
   - Error handling
3. Run tests with sanitizers

---

### 6.2 Add Test for MQTT Adapter Overflow

**Severity:** Medium  
**Files:** `test/tests/esphome_mqtt_client_adapter_test.cpp`

**Problem:**  
No tests verify behavior when pending updates queue is full.

**Solution:**  
Add overflow test:

```cpp
TEST(EsphomeMqttClientAdapterTest, TestQueueOverflow) {
  esphome_mqtt_client_adapter_t adapter;
  esphome_mqtt_client_adapter_init(&adapter, "test_device");
  
  // Fill queue to capacity
  for (size_t i = 0; i < MAX_PENDING_UPDATES + 10; i++) {
    uint8_t data = 0x42;
    mqtt_client_update_erd(&adapter.interface, i, &data, 1);
  }
  
  // Verify queue is bounded
  TEST_ASSERT_EQUAL(MAX_PENDING_UPDATES,
    esphome_mqtt_client_adapter_get_pending_update_count(&adapter));
  
  esphome_mqtt_client_adapter_destroy(&adapter);
}
```

**Implementation:**
1. Add test case to existing test file
2. Verify queue capacity enforcement
3. Run with memory sanitizers

---

### 6.3 Add Test for Feature Bit Manager Timer Expiry

**Severity:** Medium  
**Files:** `test/tests/feature_bit_manager_test.cpp`

**Problem:**  
No tests verify timer-driven parsing behavior.

**Solution:**  
Add timer tests:

```cpp
TEST(FeatureBitManagerTest, TestParseTimerCompletion) {
  FeatureBitManager manager;
  MockErdClient erd_client;
  MockTimerGroup timer_group;
  
  manager.init(&erd_client.interface, 0x42, &timer_group.interface);
  manager.start();
  
  // Simulate ERD reads
  // Advance timer
  // Verify parsing completes
  TEST_ASSERT_TRUE(manager.is_complete());
}
```

**Implementation:**
1. Add timer mock objects
2. Create test cases for:
   - Timer start/stop
   - Parsing completion
   - Timer expiry handling
3. Verify with sanitizers

---

## 7. Documentation

### 7.1 Add Module Documentation to All Files

**Severity:** Low  
**Files:** All source files

**Problem:**  
Some files lack comprehensive module documentation.

**Solution:**  
Add standard module header:

```cpp
/*!
 * @file
 * @brief Brief description of file purpose.
 *
 * MODULE GOAL: What this file accomplishes
 *
 * Responsibilities:
 *   - What this file is responsible for
 *
 * NOT responsible for:
 *   - What this file delegates to others
 *
 * Dependencies:
 *   - What this file depends on
 */
```

**Implementation:**
1. Review all source files
2. Add missing module documentation
3. Standardize format

---

## Implementation Priority

| Priority | ID | Description | Effort |
|----------|-----|-------------|--------|
| P0 | 2.1 | Replace raw new/delete with smart pointers | Medium |
| P0 | 5.1 | Add stack overflow protection for HA discovery | Low |
| P1 | 5.2 | Add mutex protection for shared data | Medium |
| P1 | 4.2 | Improve error handling for MQTT operations | Low |
| P1 | 4.3 | Add input validation for write requests | Low |
| P2 | 1.1 | Replace using namespace std | Low |
| P2 | 1.2 | Consolidate duplicate state machine code | Medium |
| P2 | 2.2 | Use std::vector for array management | Medium |
| P2 | 3.1 | Optimize string concatenation | Low |
| P3 | 1.3 | Simplify feature bit manager state machine | Medium |
| P3 | 2.3 | Add RAII timer guards | Low |
| P3 | 3.2 | Pre-allocate topic buffers | Low |
| P3 | 3.3 | Optimize feature bit parsing | Low |
| P3 | 4.1 | Add virtual destructor | Low |
| P3 | 4.4 | Standardize logging format | Low |
| P3 | 5.3 | Add timeout protection | Low |
| P3 | 7.1 | Add module documentation | Low |

---

## Verification Steps

Each improvement should be verified by:

1. **Build verification:** `make clean && make`
2. **Unit tests:** `make test`
3. **Sanitizer checks:** Run with AddressSanitizer and UndefinedBehaviorSanitizer
4. **Integration test:** Flash to ESP32 and verify functionality
5. **Memory profiling:** Check heap usage before and after

---

## Notes

- All changes should be made incrementally, one improvement at a time
- Each change should be tested independently before proceeding
- Library code in `lib/` directory should not be modified
- Generated files (`erd_lists.h`, `appliance_api_feature_lists.h`, `ha_discovery_config.h`) should be regenerated after changes to source data
