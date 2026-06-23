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

### ~~7. Synchronize write payload buffer~~ ✅ DONE

**Status:** Fixed in commit `e58e301`. The MQTT subscribe callback now decodes the hex payload into a local stack buffer (`uint8_t local_buffer[32]`) instead of the shared `write_payload_buffer_` member, preventing a race when rapid successive messages arrive. Verified: `tiny_event_publish()` is synchronous (subscriber callback runs to completion before return), so the stack buffer remains valid for the duration of event delivery.

**Verdict:** No further action needed.

## Newly Identified Issues

### ~~M1. Global `g_bridge_services` in startup HSM~~ ✅ DONE

**Status:** Fixed in commit `e58e301`. Replaced file-scope static `g_bridge_services` with `startup_hsm_wrapper_t` struct embedding `tiny_hsm_t hsm` and `IBridgeServices* services`, using `container_of` in `services_from_hsm()`. Same pattern used by `erd_write_bridge_t` and `erd_bridge_poll_t`. All call sites updated to use `startup_hsm_wrapper_init()` and `wrapper.hsm`.

**Verdict:** No further action needed.

### ~~M2. Global GEA2 state not reset on re-init~~ ✅ DONE

**Status:** Fixed in commit `e58e301`. Moved `s_gea2_last_ms` and `s_gea2_tick_count` from file-scope statics to class members (`gea2_last_ms_` and `gea2_tick_count_`) with zero initialization, so they reset on re-init (deep sleep wake, ESPHome reconfiguration). Added `friend` declaration for `gea2_tick_ticks()` accessor.

**Verdict:** No further action needed.
### ~~M3. Feature bit manager has no failure threshold~~ ✅ DONE

**Status:** Fixed in commit `e58e301`. Added `FEATURE_BIT_STATE_FAILED` enum value. The manager now requires at least `ERD_COMMON_FEATURE_API` (0x0092) to succeed before transitioning to PARSING. If it fails, enters FAILED state and falls back to full ERD list polling (no feature filtering). `is_feature_bits_complete()` returns true for FAILED state.

**Verdict:** No further action needed.
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
### ~~9. Queue write requests~~ ❌ REJECTED

**Status:** Out of scope for this bridge. The write bridge is a thin relay — its job is to forward a single write request to the ERD client and report the result. The GEA3 ERD client already has its own internal queue and retry logic. Adding a write queue at the bridge layer duplicates queuing that the client already owns, adds complexity (ordering, cancellation, memory), and provides no benefit for the single-appliance use case.

**Verdict:** No action needed. The current single-write-at-a-time design is correct.

---


## Test Infrastructure Assessment

### Framework
CppUTest with CppUMock. Strengths: `tiny_timer_group_double_t` with `elapse_time()`, `mqtt_client_double_t` with trigger methods, `tiny_gea3_erd_client_double_t` with mockable read/write, `esphome_hal_double` with controllable `millis()`, `MockBridgeServices` comprehensive mock.

### Weaknesses
| Gap | Severity | Impact |
|-----|----------|--------|
| MQTT subscribe callback path untested | HIGH | P1#7 fix verified by code inspection only; `tiny_event_publish()` synchronous delivery confirmed |
| No GEA2/GEA3 tight loop tests | MEDIUM | P2#M2 re-init fix verified by code inspection only |
| No re-init tests | MEDIUM | P2#M2 cannot verify reset behavior in tests |

### Test Recommendations by Fix
| Fix | Feasibility | Notes |
|-----|-----------|-------|
| P2#M2 GEA2 global reset | MEDIUM | No `loop()` tests exist; needs structural member-check test or full GEA2 mock |
## Summary

| # | Fix | Severity | Priority | Files | Status |
|---|-----|----------|----------|-------|--------|
| ~~1~~ | ~~`erd_cache_destroy()` memory leak~~ | ~~🔴 CRITICAL~~ | ~~P1~~ | ~~`erd_cache.cpp`~~ | ✅ DONE |
| ~~2~~ | ~~Pin library dependencies~~ | ~~🟠 HIGH~~ | ~~P1~~ | ~~`__init__.py`~~ | ✅ DONE |
| ~~3~~ | ~~Reset `subscribe_failure_count`~~ | ~~🟡 MEDIUM~~ | ~~P1~~ | ~~`erd_bridge_subscribe.cpp`~~ | ✅ DONE |
| ~~4~~ | ~~Feature bit read retry~~ | ~~🟡 MEDIUM~~ | ~~P2~~ | ~~`feature_bit_manager.cpp`~~ | ❌ REJECTED |
| ~~5~~ | ~~Write bridge timeout~~ | ~~🟡 MEDIUM~~ | ~~🔴 P1~~ | ~~`erd_write_bridge.cpp`, `erd_write_bridge.h`~~ | ❌ REJECTED |
| ~~6~~ | ~~Autodiscovery timeout~~ | ~~🟡 MEDIUM~~ | ~~🔴 P1~~ | ~~`geappliances_bridge_startup_hsm.cpp`~~ | ❌ REJECTED |
| ~~7~~ | ~~Write payload buffer sync~~ | ~~🟡 MEDIUM~~ | ~~🔴 P1~~ | ~~`esphome_mqtt_client_adapter.cpp`~~ | ✅ DONE |
| ~~M1~~ | ~~Remove global `g_bridge_services`~~ | ~~🟡 MEDIUM~~ | ~~🟡 P2~~ | ~~`geappliances_bridge_startup_hsm.cpp`~~ | ✅ DONE |
| ~~M2~~ | ~~Reset GEA2 global state on re-init~~ | ~~🟡 MEDIUM~~ | ~~🟡 P2~~ | ~~`geappliances_bridge.cpp`~~ | ✅ DONE |
| ~~M3~~ | ~~Feature bit minimum threshold~~ | ~~🟡 MEDIUM~~ | ~~🟡 P2~~ | ~~`feature_bit_manager.cpp`~~ | ✅ DONE |
| M4 | Main loop blocking | 🟠 HIGH | 🔵 P3 | `geappliances_bridge.cpp` | No change needed |
| M5 | Polling budget enforcement | 🔵 LOW | 🔵 P3 | `erd_bridge_poll.cpp` | No change needed |
| ~~9~~ | ~~Queue write requests~~ | ~~🟡 MEDIUM~~ | ~~🔵 P3~~ | ~~`erd_write_bridge.cpp`~~ | ❌ REJECTED |
| ~~10~~ | Diagnostic sensors | 🔵 LOW | 🔵 P3 | multiple | |
