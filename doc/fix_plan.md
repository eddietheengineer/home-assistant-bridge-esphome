# Fix Plan: Code Review Findings

**Date:** 2026-06-22
**Branch:** `plan/code-review-fixes`
**Source:** `doc/code_review.md`
**Reviewed by:** MemorySafetyReviewer, ArchitectureReviewer, ImplDetailReviewer, TestStrategyReviewer

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

### ~~5. Add write bridge timeout~~ ❌ REJECTED

**Status:** Already handled at the `tiny-gea-api` level. The GEA client has `request_retries=10` with `request_timeout=250ms` per attempt. A write that reaches `signal_write_failed` has already exhausted all retries — the appliance genuinely didn't respond. Adding a bridge-level timeout duplicates retry logic that the client already owns.

**Verdict:** No action needed. The tiny-gea-api retry mechanism is the correct layer for handling unresponsive appliances.

---

### ~~6. Add autodiscovery timeout~~ ❌ REJECTED

**Status:** Infinite retry is the correct behavior. If the adapter can't communicate with an appliance, nothing else matters — the bridge has no purpose without a discovered device. A timeout would give up on a transient condition that might resolve moments later (e.g., appliance powering on, network flap). The `AutodiscoveryManager` is explicitly designed to retry indefinitely (`autodiscovery_manager.h:7-8`), and that design is correct.

**Verdict:** No action needed. The current infinite retry behavior is the right approach.
---

### 7. Synchronize write payload buffer 🔴 P1
**⚠️ Test gap:** The MQTT subscribe callback path (`esphome_mqtt_client_adapter.cpp:150-178`) is **completely untested** — the existing test `subscribe_write_topic_is_noop` is a no-op because `global_mqtt_client` is null in tests. The race condition is untestable in single-threaded unit tests. Verification relies on code inspection of `tiny_event_publish()` synchronous delivery (confirmed: `tiny_event_subscription.c` calls subscriber callback synchronously before returning).

**Test:** Add test wiring `MqttTestDouble` as `global_mqtt_client` to exercise the subscribe callback. Simulate rapid successive messages and verify first message data is intact. This requires new test infrastructure.

## Newly Identified Issues

### M1. Global `g_bridge_services` in startup HSM 🟡 P2

**File:** `geappliances_bridge_startup_hsm.cpp:32`

File-scope static global `g_bridge_services` makes the HSM non-reentrant and non-testable. Breaks if multiple bridge instances ever exist or if the HSM is used before `set_bridge_services()` is called.

**⚠️ Implementation constraint:** `tiny_hsm_t` (`lib/tiny/include/tiny_hsm.h:103-106`) has **no `user_data` field** — only `configuration` and `current`. The proposed "pass through HSM's user_data field" is infeasible. Two alternatives:

1. **Embed the HSM in a wrapper struct** that holds both the `tiny_hsm_t` and the `IBridgeServices*` pointer, using `container_of` to recover the wrapper from the HSM pointer (same pattern used by `erd_bridge_poll_t` and `erd_write_bridge_t`).
2. **Keep the global but make it safer** — add a null-check in `services_from_hsm()` and log an error if called before `set_bridge_services()`.

**Recommendation:** Option 1 — create a `startup_hsm_wrapper_t` struct embedding `tiny_hsm_t hsm` and `IBridgeServices* services`, using `container_of` in `services_from_hsm()`. This is the same pattern used throughout the codebase.

**Regression risk:** Low — only one bridge instance exists in practice. Requires updating all `services_from_hsm()` call sites.
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

## Test Infrastructure Assessment

### Framework
CppUTest with CppUMock. Strengths: `tiny_timer_group_double_t` with `elapse_time()`, `mqtt_client_double_t` with trigger methods, `tiny_gea3_erd_client_double_t` with mockable read/write, `esphome_hal_double` with controllable `millis()`, `MockBridgeServices` comprehensive mock.

### Weaknesses
| Gap | Severity | Impact |
|-----|----------|--------|
| MQTT subscribe callback path untested | HIGH | P1#7 fix cannot be stress-tested |
| No GEA2/GEA3 tight loop tests | MEDIUM | P2#M2 re-init fix hard to verify |
| No re-init tests | MEDIUM | P2#M2 cannot verify reset behavior |
| Race conditions untestable in single-threaded tests | MEDIUM | P1#7 verification relies on code inspection |

### Test Recommendations by Fix
| Fix | Feasibility | Notes |
| P1#7 Write payload buffer sync | LOW-MEDIUM | Requires new `MqttTestDouble` infrastructure; race untestable in single-threaded tests |
| P2#M1 Remove global | HIGH | All 10 `startup_hsm_test.cpp` tests need mechanical update; `MockBridgeServices` works unchanged |
| P2#M2 GEA2 global reset | MEDIUM | No `loop()` tests exist; needs structural member-check test or full GEA2 mock |
| P2#M3 Feature bit threshold | HIGH | Existing `read_failed` patterns in `feature_bit_manager_test.cpp` provide exact structure needed |
## Summary

| # | Fix | Severity | Priority | Files | Status |
|---|-----|----------|----------|-------|--------|
| ~~1~~ | ~~`erd_cache_destroy()` memory leak~~ | ~~🔴 CRITICAL~~ | ~~P1~~ | ~~`erd_cache.cpp`~~ | ✅ DONE |
| ~~2~~ | ~~Pin library dependencies~~ | ~~🟠 HIGH~~ | ~~P1~~ | ~~`__init__.py`~~ | ✅ DONE |
| ~~3~~ | ~~Reset `subscribe_failure_count`~~ | ~~🟡 MEDIUM~~ | ~~P1~~ | ~~`erd_bridge_subscribe.cpp`~~ | ✅ DONE |
| ~~4~~ | ~~Feature bit read retry~~ | ~~🟡 MEDIUM~~ | ~~P2~~ | ~~`feature_bit_manager.cpp`~~ | ❌ REJECTED |
| ~~5~~ | ~~Write bridge timeout~~ | ~~🟡 MEDIUM~~ | ~~🔴 P1~~ | ~~`erd_write_bridge.cpp`, `erd_write_bridge.h`~~ | ❌ REJECTED |
| ~~6~~ | ~~Autodiscovery timeout~~ | ~~🟡 MEDIUM~~ | ~~🔴 P1~~ | ~~`geappliances_bridge_startup_hsm.cpp`~~ | ❌ REJECTED |
| 7 | Write payload buffer sync | 🟡 MEDIUM | 🔴 P1 | `esphome_mqtt_client_adapter.cpp` | |
| M1 | Remove global `g_bridge_services` | 🟡 MEDIUM | 🟡 P2 | `geappliances_bridge_startup_hsm.cpp` | |
| M2 | Reset GEA2 global state on re-init | 🟡 MEDIUM | 🟡 P2 | `geappliances_bridge.cpp` | |
| M3 | Feature bit minimum threshold | 🟡 MEDIUM | 🟡 P2 | `feature_bit_manager.cpp` | |
| M4 | Main loop blocking | 🟠 HIGH | 🔵 P3 | `geappliances_bridge.cpp` | No change needed |
| M5 | Polling budget enforcement | 🔵 LOW | 🔵 P3 | `erd_bridge_poll.cpp` | No change needed |
| ~~9~~ | Queue write requests | 🟡 MEDIUM | 🔵 P3 | `erd_write_bridge.cpp` | |
| ~~10~~ | Diagnostic sensors | 🔵 LOW | 🔵 P3 | multiple | |
