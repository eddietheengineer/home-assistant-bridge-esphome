# Fix Plan: Code Review Findings

**Date:** 2026-06-22
**Branch:** `plan/code-review-fixes`
**Source:** `doc/code_review.md`
**Reviewed by:** MemorySafetyReviewer, ArchitectureReviewer

---

## Status Legend

- ✅ **DONE** — already fixed in current code
- ❌ **REJECTED** — proposed fix is architecturally wrong
- 🔴 **P1** — must fix before next release
- 🟡 **P2** — next sprint
- 🔵 **P3** — future improvement

---

## Previously Identified (from code_review.md)

### ~~1. Fix `erd_cache_destroy()` memory leak~~ ✅ DONE

**Status:** Already fixed in commit `0fc6405`. `erd_cache.cpp:67-80` now iterates entries and `delete[]`s `ext_data` when `uses_heap` is true before calling `erd_cache_init()`. Additionally, `erd_cache_init()` has a defensive guard checking `self->initialized` and freeing heap entries from a previous init.

**Verdict:** No action needed.

---

### ~~2. Pin library dependencies~~ ✅ DONE

**Status:** Already fixed in commit `0fc6405`. Libraries are pinned to specific commit SHAs.

**Verdict:** No action needed.

---

### ~~3. Reset `subscribe_failure_count` on success~~ ✅ DONE

**Status:** Already fixed. `erd_bridge_subscribe.cpp:148` has `self->subscribe_failure_count = 0;` in `state_subscribed` entry handler.

**Verdict:** No action needed.

---

### ~~4. Add retry logic for feature bit ERD reads~~ ❌ REJECTED

**Status:** Architecturally wrong. The GEA client already has `request_retries=10` (`geappliances_bridge.cpp:24`). A `read_failed` event means the client exhausted all 10 retries — the appliance genuinely didn't respond. Feature bit ERDs are optional; if 0x0093 fails, the appliance doesn't support that feature group. Skipping is the correct behavior. The comparison to `DeviceIdentityManager` is misleading: identity ERDs MUST succeed for the bridge to function; feature ERDs are optional.

**Regression risk:** Adding retry risks the manager getting stuck retrying an unsupported ERD, blocking the entire feature bit sequence and preventing the bridge from reaching steady state.

**Verdict:** No action needed. Current design is correct.

---

### 5. Add write bridge timeout 🔴 P1

**File:** `erd_write_bridge.cpp:76-115`, `erd_write_bridge.h:43-54`

Once a write is queued, the bridge stays in `state_writing` until `signal_write_completed` or `signal_write_failed`. If the appliance becomes unresponsive, neither signal arrives and the bridge is permanently stuck, blocking all future writes.

**Fix:**
- Add `tiny_timer_t write_timeout_timer` to `erd_write_bridge_t` struct
- Arm a 5s timeout timer in `state_writing` entry (reasonable given GEA client's internal retry budget: 250ms × 10 = 2.5s max)
- Add `signal_write_timeout` handler that transitions to `state_ready` with a failure report
- Disarm timer in `state_writing` exit and on completion/failure

**Test:** Queue a write, never send completion, verify timeout recovery and failure report to MQTT.

---

### 6. Add autodiscovery timeout 🔴 P1

**File:** `geappliances_bridge_startup_hsm.cpp:146-184` (NOT `autodiscovery_manager.cpp`)

No timeout — if no appliance is present, the bridge loops forever in the autodiscovery phase with no user feedback.

**Fix location:** The fix belongs in the **startup HSM**, not the manager. `AutodiscoveryManager` is designed to "retry indefinitely" (per `autodiscovery_manager.h:7-8`) — its job is to find a board. The startup HSM is the orchestrator and should decide when to give up.

**Fix:**
- Add a 60s timeout timer to `startup_state_autodiscovery`
- On timeout, transition to a new `startup_state_failed` state
- Add `startup_state_failed` as a terminal state with a `signal_restart` handler for recovery
- Log a clear error message so users know no appliance was found

**Test:** Verify timeout fires after 60s with no appliance, and that `signal_restart` recovers when one appears.

---

### 7. Synchronize write payload buffer 🔴 P1

**File:** `esphome_mqtt_client_adapter.cpp:150-178`

The MQTT subscribe callback (running on the IDF MQTT task) writes to `write_payload_buffer_` (lines 162-170), then publishes an event with `args.value = self->write_payload_buffer_` (line 175). The write bridge reads `args->value` on the main loop task. This is a real data race — if the MQTT callback fires again before the consumer reads, the buffer is overwritten with a torn read.

**Fix:** Copy the decoded payload to a stack buffer in the callback and set `args.value` to point to it. Since `tiny_event_publish()` delivers synchronously (the subscriber callback runs to completion before `publish()` returns), the stack buffer is safe — it lives for the duration of the callback.

**Test:** Simulate rapid successive MQTT messages and verify no corruption.

---

## Newly Identified Issues

### M1. Global `g_bridge_services` in startup HSM 🟡 P2

**File:** `geappliances_bridge_startup_hsm.cpp:32`

File-scope static global `g_bridge_services` makes the HSM non-reentrant and non-testable. Breaks if multiple bridge instances ever exist or if the HSM is used before `set_bridge_services()` is called.

**Fix:** Pass `IBridgeServices*` through the HSM's user_data field instead of a global. This requires a small API change to `services_from_hsm()`.

**Regression risk:** Low — only one bridge instance exists in practice.

---

### M2. Global GEA2 state not reset on re-init 🟡 P2

**File:** `geappliances_bridge.cpp:43-47, 304-305`

`s_gea2_last_ms` is a file-scope static initialized to 0 as a sentinel. If the bridge re-initializes (deep sleep wake, ESPHome reconfiguration), `s_gea2_last_ms` retains its old value. The sentinel check (`if (s_gea2_last_ms == 0)`) fails, and the msec catchup loop could fire a burst of backlogged interrupts.

**Fix:** Move `s_gea2_last_ms` and `s_gea2_tick_count` to class members and reset in `setup()`.

**Regression risk:** Low — affects edge cases (deep sleep, reconfiguration).

---

### M3. Feature bit manager has no failure threshold 🟡 P2

**File:** `feature_bit_manager.cpp:375-379`

If all 11 feature ERD reads fail, the manager transitions to PARSING with empty buffers. The bridge proceeds to polling with no feature knowledge, potentially polling unsupported ERDs.

**Fix:** Require at least `ERD_COMMON_FEATURE_API` (0x0092) to succeed before transitioning to PARSING. If it fails, enter a FAILED state and fall back to full ERD list polling (no feature filtering).

**Regression risk:** Low — only affects appliances that don't support any feature ERDs.

---

### M4. Main loop blocking — no structural change needed 🔵 P3

**File:** `geappliances_bridge.cpp:300-354` (GEA2 tight loop), `erd_bridge_poll.cpp:206-224`

The GEA2 tight loop requires precise wall-clock timing — the msec interrupt fires once per real millisecond to drive GEA2 interface timers. Moving it to a background task introduces scheduling jitter that breaks the protocol. The current design with `esp_task_wdt_reset()` inside the loop and after `loop()` is the correct approach for ESP32. The actual blocking is: GEA2 = 100ms nominal/200ms cap, GEA3 = 10ms nominal/20ms cap.

**Verdict:** No structural change needed. If blocking is observed, reduce `GEA3_LOOP_DURATION_MS` from 10ms to 5ms as a micro-optimization.

---

### M5. Polling bridge send_cycle_reads() budget 🔵 P3

**File:** `erd_bridge_poll.cpp:206-224`

The 100ms budget is enforced and the function returns false to arm a resume timer. The design is acceptable as-is.

**Verdict:** No change needed.

---

## Summary

| # | Fix | Severity | Priority | Files | Status |
|---|-----|----------|----------|-------|--------|
| ~~1~~ | ~~`erd_cache_destroy()` memory leak~~ | ~~🔴 CRITICAL~~ | ~~P1~~ | ~~`erd_cache.cpp`~~ | ✅ DONE |
| ~~2~~ | ~~Pin library dependencies~~ | ~~🟠 HIGH~~ | ~~P1~~ | ~~`__init__.py`~~ | ✅ DONE |
| ~~3~~ | ~~Reset `subscribe_failure_count`~~ | ~~🟡 MEDIUM~~ | ~~P1~~ | ~~`erd_bridge_subscribe.cpp`~~ | ✅ DONE |
| ~~4~~ | ~~Feature bit read retry~~ | ~~🟡 MEDIUM~~ | ~~P2~~ | ~~`feature_bit_manager.cpp`~~ | ❌ REJECTED |
| 5 | Write bridge timeout | 🟡 MEDIUM | 🔴 P1 | `erd_write_bridge.cpp`, `erd_write_bridge.h` | |
| 6 | Autodiscovery timeout | 🟡 MEDIUM | 🔴 P1 | `geappliances_bridge_startup_hsm.cpp` | |
| 7 | Write payload buffer sync | 🟡 MEDIUM | 🔴 P1 | `esphome_mqtt_client_adapter.cpp` | |
| M1 | Remove global `g_bridge_services` | 🟡 MEDIUM | 🟡 P2 | `geappliances_bridge_startup_hsm.cpp` | |
| M2 | Reset GEA2 global state on re-init | 🟡 MEDIUM | 🟡 P2 | `geappliances_bridge.cpp` | |
| M3 | Feature bit minimum threshold | 🟡 MEDIUM | 🟡 P2 | `feature_bit_manager.cpp` | |
| M4 | Main loop blocking | 🟠 HIGH | 🔵 P3 | `geappliances_bridge.cpp` | No change needed |
| M5 | Polling budget enforcement | 🔵 LOW | 🔵 P3 | `erd_bridge_poll.cpp` | No change needed |
| ~~9~~ | Queue write requests | 🟡 MEDIUM | 🔵 P3 | `erd_write_bridge.cpp` | |
| ~~10~~ | Diagnostic sensors | 🔵 LOW | 🔵 P3 | multiple | |
