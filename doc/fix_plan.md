# Fix Plan: Code Review Findings

**Date:** 2026-06-22
**Branch:** `plan/code-review-fixes`
**Source:** `doc/code_review.md`

---

## Priority 1 — Immediate (must fix before next release)

### 1. Fix `erd_cache_destroy()` memory leak 🔴 CRITICAL

**File:** `erd_cache.cpp:67-80`

`erd_cache_destroy()` calls `erd_cache_init()` which zeroes entries without freeing heap-allocated `ext_data`. Every ERD with data > 4 bytes leaks on teardown.

**Fix:** Add a cleanup pass in `erd_cache_destroy()` that iterates all valid entries and `delete[]`s `ext_data` when `uses_heap` is true, before calling `erd_cache_init()`.

**Test:** Add test that inserts heap-allocated entries, calls destroy, and verifies no leak (valgrind/ASan).

---

### 2. Pin library dependencies in `__init__.py` 🟠 HIGH

**File:** `__init__.py:325-327`

Already fixed in commit `0fc6405` — libraries are now pinned to specific commit SHAs. ✅ **DONE**

---

### 3. Reset `subscribe_failure_count` on successful subscription 🟡 MEDIUM

**File:** `erd_bridge_subscribe.cpp:148`

`subscribe_failure_count` increments on failed subscribe attempts but is never reset when a subscribe succeeds. After 3 failures the bridge falls back to polling even if it would eventually stabilize.

**Fix:** Reset `subscribe_failure_count = 0` in `state_subscribed` entry handler (line 148, after `self->current_state = subscription_state_subscribed;`).

**Test:** Add test that simulates 2 failures then a success, verifying no fallback to polling.

---

## Priority 2 — Short-term (next sprint)

### 4. Add retry logic for feature bit ERD reads 🟡 MEDIUM

**File:** `feature_bit_manager.cpp:358-385`

When a feature bit ERD read fails, `skip_to_next_erd_()` advances without retry. This is inconsistent with `DeviceIdentityManager` which retries indefinitely.

**Fix:** In `skip_to_next_erd_()`, add a retry counter (e.g., max 3 retries per ERD) before advancing. On retry, re-queue the same ERD read.

**Test:** Add test that simulates transient failure then success on retry.

---

### 5. Add write bridge timeout 🟡 MEDIUM

**File:** `erd_write_bridge.cpp:76-115`

Once a write is queued, the bridge stays in `state_writing` until completion or failure. If the appliance becomes unresponsive, the bridge is permanently stuck.

**Fix:** Add a timer in `state_writing` entry (e.g., 5s timeout) that transitions to `state_ready` with a failure report if no response arrives.

**Test:** Add test that queues a write, never sends completion, and verifies timeout recovery.

---

### 6. Add autodiscovery timeout or backoff 🟡 MEDIUM

**File:** `autodiscovery_manager.cpp:181-199`

No timeout or backoff — retries indefinitely with fixed 5s windows. If no appliance is present, the bridge loops forever in autodiscovery with no user feedback.

**Fix:** Add a retry counter with exponential backoff (e.g., 5s → 10s → 20s → cap at 60s) and a maximum retry count (e.g., 30) after which the startup HSM transitions to a failed state.

**Test:** Add test that verifies backoff progression and eventual timeout.

---

### 7. Synchronize write payload buffer access 🟡 MEDIUM

**File:** `esphome_mqtt_client_adapter.cpp:156-171`

`write_payload_buffer_` is written in the MQTT subscribe callback (different task on ESP-IDF), then read by the write bridge in the main loop. No synchronization between writer and reader.

**Fix:** Add a mutex or use a double-buffer pattern to prevent torn reads. Simplest: copy the payload into a local buffer in the callback before publishing the event, so the event args point to stable data.

**Test:** Add test that simulates concurrent write to buffer and read from event handler.

---

## Priority 3 — Long-term (future improvements)

### 8. Reduce main loop blocking 🟠 HIGH

**File:** `geappliances_bridge.cpp:300-354` (GEA2 tight loop), `erd_bridge_poll.cpp:206-224` (send_cycle_reads)

GEA2 tight loop blocks 100ms per iteration; `send_cycle_reads` can block 100ms+ with budget. On single-core ESP32 this starves WiFi/MQTT.

**Approach:** Consider moving the GEA2 tight loop to a background task, or breaking it into smaller chunks with explicit yields.

---

### 9. Queue write requests 🟡 MEDIUM

**File:** `erd_write_bridge.cpp:81-85`

Concurrent writes are dropped with a warning. A simple FIFO queue (fixed-size, e.g., 4 entries) would allow batching writes from Home Assistant.

**Approach:** Add a `tiny_queue_t` of pending writes; process one at a time, queuing the rest.

---

### 10. Add diagnostic sensors 🔵 LOW

**Approach:** Expose polling failure count, subscription state, and bridge health as ESPHome sensors for user visibility.

---

## Summary

| # | Fix | Severity | Priority | Files |
|---|-----|----------|----------|-------|
| 1 | Fix `erd_cache_destroy()` memory leak | 🔴 CRITICAL | P1 | `erd_cache.cpp` |
| 2 | Pin library dependencies | 🟠 HIGH | P1 | `__init__.py` | ✅ DONE |
| 3 | Reset `subscribe_failure_count` on success | 🟡 MEDIUM | P1 | `erd_bridge_subscribe.cpp` |
| 4 | Add feature bit read retry | 🟡 MEDIUM | P2 | `feature_bit_manager.cpp` |
| 5 | Add write bridge timeout | 🟡 MEDIUM | P2 | `erd_write_bridge.cpp` |
| 6 | Add autodiscovery timeout/backoff | 🟡 MEDIUM | P2 | `autodiscovery_manager.cpp` |
| 7 | Synchronize write payload buffer | 🟡 MEDIUM | P2 | `esphome_mqtt_client_adapter.cpp` |
| 8 | Reduce main loop blocking | 🟠 HIGH | P3 | `geappliances_bridge.cpp`, `erd_bridge_poll.cpp` |
| 9 | Queue write requests | 🟡 MEDIUM | P3 | `erd_write_bridge.cpp` |
| 10 | Add diagnostic sensors | 🔵 LOW | P3 | multiple |
