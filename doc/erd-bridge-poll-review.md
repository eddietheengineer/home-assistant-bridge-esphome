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

## Issue 10 — `cycle_start_ms` set twice in timer-expired path (lines 649, 150)

**Severity:** Trivial

In `signal_polling_timer_expired` (line 649), `cycle_start_ms` is set from `esphome::millis()` before calling `send_cycle_reads`. Then in `on_polling_cycle_complete` (line 150), when `immediate` is true, `cycle_start_ms` is set again from a fresh `millis()` call. The two calls introduce small drift in the measured cycle time.

**Recommendation:** Cache the single `millis()` call in a local variable and reuse it across the timer-expired path.

---

## Issue 11 — Redundant clear in `state_add_appliance_api_feature_erds` (lines 474–478)

**Severity:** Low

When `api_parsed_list` is set, `state_add_appliance_api_feature_erds` clears `erd_set`, `pending_registration_set`, and `polling_list_count` (lines 474-478). On the **first entry** path, these are already empty (freshly allocated in init). On the **re-entry** path (appliance lost), `state_identify_appliance` already clears these fields (lines 354-358) before transitioning directly to `state_polling`, skipping `state_add_appliance_api_feature_erds` entirely. The clear is dead code in both cases.

**Recommendation:** Remove the redundant clear block (lines 474-478).

---

## Issue 12 — Dual clearing points create maintenance hazard (lines 432–435, 474–478)

**Severity:** Low

`state_add_common_erds` clears `erd_set`, `pending_registration_set`, `erd_cache`, and `polling_list_count` on entry (lines 432-435). `state_add_appliance_api_feature_erds` also clears these for the api_parsed path (lines 474-478). This dual-clearing pattern means a new discovery path added in the future may not know where the clear belongs.

**Recommendation:** Extract a `clear_discovery_state()` helper and call it from the canonical entry points (`state_add_common_erds` for full-discovery, `state_identify_appliance` re-entry for api_parsed).

---

## Issue 13 — `send_next_read_request` naming ambiguity (lines 162–172)

**Severity:** Low

`send_next_read_request` reads from `self->appliance_erd_list` and checks against `self->appliance_erd_list_count`. It is structurally similar to `send_next_poll_read_request` which reads from `erd_polling_list`. The two functions operate on different lists (discovery vs steady-state) but the naming doesn't make this distinction obvious.

**Recommendation:** Add a comment to `send_next_read_request` explicitly stating it is only used during discovery phases, distinguishing it from `send_next_poll_read_request`.

---

## Issue 14 — Failed ERDs in `erd_set` blocks re-probe via custom list (line 291)

**Severity:** Medium

`handle_discovery_list_signals` inserts failed ERDs into `erd_set` (line 291) to exclude them from the polling list. But `erd_set` serves dual purpose: deduplication AND exclusion. If a failed ERD from `state_add_common_erds` appears in `state_add_custom_erds` (or `state_probe_api_parsed_erds`), the dedup check in `add_erd_to_polling_list` will silently skip it — even though it was only marked as failed in a previous discovery phase, not successfully registered. This prevents custom ERDs from being re-probed if they share an ERD code with a failed standard ERD.

**Recommendation:** Use a separate `excluded_erds_set` for permanently rejected ERDs, keeping `erd_set` as a pure deduplication set. Alternatively, clear `erd_set` between discovery phases (but this risks losing legitimate dedup info).

---

## Issue 15 — `send_cycle_reads` timing scope vs cycle scope (lines 239, 252)

**Severity:** Trivial

`send_cycle_reads` has its own `start` variable (line 239) for measuring send-phase elapsed time. The `cycle_start_ms` is set outside `send_cycle_reads` (in `on_polling_cycle_complete` or timer handler). The naming is correct but could be clearer — `cycle_start_ms` = cycle start, `start` in `send_cycle_reads` = send phase start.

**Recommendation:** Rename the local `start` to `send_start` for clarity.

---

## Issue 16 — `POLLING_LIST_MAX_SIZE` is a hidden dependency (line 86)

**Severity:** Low

`ensure_polling_list_capacity` references `POLLING_LIST_MAX_SIZE` (line 86) but it's not defined in `erd_bridge_poll.cpp` or `erd_bridge_poll.h`. It comes from `erd_lists.h` via `erd_bridge_common.h`. This hidden dependency could cause confusion during maintenance.

**Recommendation:** Define `POLLING_LIST_MAX_SIZE` in `erd_bridge_poll.h` or add a comment indicating where it comes from.

---

## Issue 17 — Uninitialized health metrics in init (lines 742–808)

**Severity:** Low

`erd_bridge_poll_init_impl` does not initialize `cycle_start_ms`, `last_cycle_time_ms`, or `cycle_count`. These struct members will contain whatever was in memory. If health monitoring code reads `last_cycle_time_ms` before the first cycle completes, it will return garbage.

**Recommendation:** Initialize `cycle_start_ms = 0`, `last_cycle_time_ms = 0`, `cycle_count = 0` in `erd_bridge_poll_init_impl`.

---

## Issue 18 — `current_state_name` and `polling_timer_armed` not initialized (lines 742–808)

**Severity:** Low

`current_state_name` is documented as "initialized to nullptr" (header line 110-111) but init does not set it. `polling_timer_armed` is not initialized either. These should be explicitly set in init for defensive correctness.

**Recommendation:** Set `current_state_name = nullptr` and `polling_timer_armed = false` in `erd_bridge_poll_init_impl`.

---

## Issue 19 — No debug logging for steady-state read failures (lines 687–693)

**Severity:** Low

When a read fails during steady-state polling, the handler increments `cycle_completed_count` and moves on. There's no logging of which ERD failed, no tracking of failure patterns, and no way to distinguish between "not supported" and "timeout" at this level. The `args` data is available but not used in the `signal_read_failed` case for `state_polling`.

**Recommendation:** Add a debug log for failed reads during steady-state polling to aid troubleshooting.

---

## Issue 20 — Hardcoded `false` for cache subscription flag (line 674)

**Severity:** Trivial

**Status:** Resolved. The publish-on-change logic has been moved into the shared ERD cache. The cache has an `only_publish_onchange` field set via `erd_cache_set_only_publish_onchange()`. Discovery-phase reads pass `force_publish = true` (always publish); steady-state polling passes `force_publish = false` (respects the cache setting).
---

## Issue 21 — Write requests not gated on appliance identification (lines 308–311)

**Severity:** Medium

`poll_state_top` processes write requests by directly forwarding them to the ERD client. There's no validation of the ERD, no check that the appliance is identified, and no rate limiting. If a write is requested before `state_identify_appliance` completes, `erd_host_address` is still the broadcast address (0xFF), and the write will be sent to broadcast.

**Recommendation:** Gate writes on `polling_list_complete` or at minimum check that `erd_host_address != tiny_gea_broadcast_address`.

---

## Issue 22 — Broadcast appliance type may not match target (lines 385–388)

**Severity:** Low

In `state_identify_appliance`, the appliance type is read from the broadcast response to ERD 0x0008. The `erd_host_address` is set from `args->address` (the responding device's address). If multiple appliances respond to the broadcast, the first response wins — there's no guarantee it's the correct appliance. This is mitigated by the fact that the bridge typically runs with a single appliance, but the code doesn't defend against multi-appliance environments.

**Recommendation:** Document this limitation in the state handler comment.

---

## Issue 23 — Hardcoded 100ms resume timer in 3 places (lines 154, 643, 654)

**Severity:** Low

The value `100` (milliseconds) is hardcoded in three places as the resume timer interval when `send_cycle_reads` exceeds its time budget. This should be a named constant for consistency and maintainability.

**Recommendation:** Define `POLL_CYCLE_RESUME_MS = 100` as a constant and use it consistently.

---

## Issue 24 — Incorrect comment about failed ERDs in `state_polling` entry (lines 592–593)

**Severity:** Low

The comment at lines 592-593 says "ERDs that did not respond during probe... are not yet in erd_set and are added here with deferred MQTT registration." This is **incorrect**. Failed ERDs during probe ARE added to `erd_set` (line 522 in `state_probe_api_parsed_erds`), so they are correctly excluded by the dedup check in `add_erd_to_polling_list_no_register`. The comment misrepresents the actual behavior.

**Recommendation:** Correct the comment to accurately describe: (1) successfully probed ERDs are already in erd_set + polling list → dedup skips; (2) failed ERDs are in erd_set as exclusions → dedup skips (correctly excluded); (3) this path is primarily useful on re-entry after appliance lost, where erd_set is cleared.

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
| 10 | `cycle_start_ms` set twice in timer path | Trivial | 649, 150 |
| 11 | Redundant clear in `state_add_appliance_api_feature_erds` | Low | 474–478 |
| 12 | Dual clearing points create maintenance hazard | Low | 432–435, 474–478 |
| 13 | `send_next_read_request` naming ambiguity | Low | 162–172 |
| 14 | Failed ERDs in `erd_set` blocks re-probe via custom list | Medium | 291 |
| 15 | `send_cycle_reads` timing scope vs cycle scope | Trivial | 239, 252 |
| 16 | `POLLING_LIST_MAX_SIZE` hidden dependency | Low | 86 |
| 17 | Uninitialized health metrics in init | Low | 742–808 |
| 18 | `current_state_name`/`polling_timer_armed` not initialized | Low | 742–808 |
| 19 | No debug logging for steady-state read failures | Low | 687–693 |
| 20 | Hardcoded `false` for cache subscription flag | Trivial | 674 |
| 21 | Write requests not gated on appliance identification | Medium | 308–311 |
| 22 | Broadcast appliance type may not match target | Low | 385–388 |
| 23 | Hardcoded 100ms resume timer in 3 places | Low | 154, 643, 654 |
| 24 | Incorrect comment about failed ERDs in `state_polling` entry | Low | 592–593 |
