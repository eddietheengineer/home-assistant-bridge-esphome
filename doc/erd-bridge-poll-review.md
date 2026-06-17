# Code Review: `erd_bridge_poll.cpp`

**Reviewed:** `components/geappliances_bridge/erd_bridge_poll.cpp` (846 lines)
**Date:** 2026-06-17

---

## Issue 1 — Integer overflow in `ensure_polling_list_capacity` (lines 82–97)

**Severity:** Medium

Line 82 computes the new capacity with unsigned 16-bit arithmetic:

```cpp
uint16_t new_capacity = needed + (POLLING_LIST_GROWTH_INCREMENT - 1);
```

If `needed` exceeds `65504` (`65535 - 31`), `new_capacity` wraps before the safety-cap check on line 84. A wrapped value could be smaller than `needed`, causing the copy loop (lines 90–92) to write past the end of the newly allocated buffer — a heap buffer overflow.

The cap at `POLLING_LIST_MAX_SIZE` mitigates this in practice, but the overflow is silent and fragile.

**Recommendation:** Compute `new_capacity` in a wider type (`uint32_t`) before casting, or clamp `needed` before the addition.

---

## Issue 2 — Dead `timer` field wastes struct memory (header line 79)

**Severity:** Low

The struct declares `tiny_timer_t timer;` but the code uses `polling_timer` and `appliance_lost_timer` exclusively. The `timer` field is a dead 16+ bytes of struct memory. In `erd_bridge_poll_destroy` (line 812), `tiny_timer_stop` is called on it as a no-op.

This appears to be a relic from a refactor that split a single timer into two named timers.

**Recommendation:** Remove the `timer` field and the corresponding stop call in `destroy`.

---

## Issue 3 — `esphome::delay(0)` in tight loops — potential WDT risk (lines 149–152, 611–615)

**Severity:** Low

With large ERD lists (100+), the while loop sending all read requests can block for seconds. `esphome::delay(0)` yields to the ESPHome loop after each `POLL_YIELD_MS` (50 ms) batch, but the outer loop still holds the thread for the entire duration. On an ESP32 with the task watchdog timer, extended blocking could trigger a WDT reset.

The warning at line 617–618 fires at 1 second, which is a good signal, but there is no corrective action.

**Recommendation:** Consider a hard cap on total cycle-start time, or break the loop into smaller chunks processed across multiple loop iterations.

---

## Issue 4 — `millis()` called twice in `on_polling_cycle_complete` (lines 140, 147)

**Severity:** Trivial

`esphome::millis()` is called at line 140 for `last_cycle_time_ms` and again at line 147 for `cycle_start_ms`. The two calls are not identical — a small drift is introduced. The same pattern appears at lines 609–610 where `cycle_start_ms` and `cycle_start` are both set from `millis()`.

**Recommendation:** Cache the single `millis()` call in a local variable and reuse it.

---

## Issue 5 — `state_polling` entry re-adds all `api_parsed_list` ERDs on re-entry (lines 565–569)

**Severity:** Medium

Every time `state_polling` is entered (including after appliance-lost recovery), the entry handler iterates all `api_parsed_list` ERDs through `add_erd_to_polling_list_no_register`. On re-entry after appliance loss, the `is_reentry` path (lines 324–328) clears `erd_set`, `pending_registration_set`, and `polling_list_count`. Then the polling entry re-adds every api_parsed ERD to the pending registration set, causing every ERD to be re-registered on MQTT one-by-one as they respond — potentially hundreds of MQTT registrations in rapid succession.

This is intentional (lazy re-registration), but the burst of registrations after appliance recovery could overwhelm the MQTT broker or trigger rate limiting.

**Recommendation:** Batch the re-registrations or throttle them. Alternatively, skip the pending-registration dance on re-entry and re-register all ERDs in a single batch.

---

## Issue 6 — Failed ERDs never evicted from polling list (lines 651–657)

**Severity:** Medium

When a read fails during steady-state polling, the handler increments `cycle_completed_count` and moves on. It does not track consecutive failures or remove the ERD from the polling list. A permanently failing ERD (e.g., removed by a firmware update) will keep failing every cycle, consuming bus bandwidth and inflating cycle time indefinitely.

**Recommendation:** Track consecutive failures per ERD and evict ERDs that exceed a threshold, or periodically re-probe the polling list against the appliance's actual capabilities.

---

## Issue 7 — `state_add_appliance_erds` clamps invalid `appliance_type` to 0 (lines 528–537)

**Severity:** Low

When `appliance_type >= maximumApplianceType`, it is clamped to 0 at line 529. Type 0 may be a real appliance type in the translation table, not a "none" sentinel. If the actual appliance type is unknown, clamping to 0 could poll the wrong set of appliance-specific ERDs.

**Recommendation:** Use an explicit sentinel value (e.g., `0xFF`) for "unknown appliance type" and skip appliance-specific ERD discovery when the type is invalid.

---

## Issue 8 — Anonymous `const void*` parameter in disconnect callback (erd_bridge_common.h line 123)

**Severity:** Trivial

The disconnect subscription lambda takes `const void*` with no parameter name:

```cpp
+[](void* context, const void*) { ... }
```

This is valid C++ but a code smell — the unnamed parameter signals unclear intent.

**Recommendation:** Name the parameter `[[maybe_unused]] const void* _` or simply `[[maybe_unused]] const void* data` for clarity.

---

## Issue 9 — Heap allocations lack RAII / destructor safety (lines 739–741, 834–844)

**Severity:** Low

The `set<tiny_erd_t>` objects and `erd_polling_list` array are heap-allocated in init and only freed in the explicit `destroy` function. If the bridge is embedded in a stack-allocated struct that goes out of scope without calling `destroy`, these allocations leak. On memory-constrained embedded systems, this is a risk.

**Recommendation:** Consider wrapping the struct in a class with a destructor, or document the init/destroy pairing as a hard requirement with a static analysis check.

---

## Summary

| # | Issue | Severity | Lines |
|---|-------|----------|-------|
| 1 | Integer overflow in capacity calculation | Medium | 82–97 |
| 2 | Dead `timer` field wastes memory | Low | 79 (header) |
| 3 | Potential WDT risk in tight loops | Low | 149–152, 611–615 |
| 4 | Duplicate `millis()` calls | Trivial | 140, 147, 609–610 |
| 5 | Burst MQTT re-registrations on re-entry | Medium | 565–569 |
| 6 | Failed ERDs never evicted from polling list | Medium | 651–657 |
| 7 | Invalid `appliance_type` clamped to 0 | Low | 528–537 |
| 8 | Anonymous parameter in disconnect callback | Trivial | common.h:123 |
| 9 | No RAII safety for heap allocations | Low | 739–741, 834–844 |
