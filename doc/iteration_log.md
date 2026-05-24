# Iteration Log

This file tracks issues found during iterative improvement of the codebase and how they were fixed.

## Goals
- Professional, robust code
- Non-blocking timers (main loop never interrupted)
- Robust testing
- Modular architecture
- Documentation kept up to date
- Every commit passes all 4 verification gates (tests, compile, flash, runtime)

---

## Iteration 1 — Baseline Analysis (in progress)

### Date: 2026-05-24

### Issues Found

#### Issue 1: Legacy member duplication — god class anti-pattern
**Severity:** High
**Location:** `geappliances_bridge.h` — ~200+ lines of legacy members alongside extracted managers
**Description:** The `GeappliancesBridge` class still contains large blocks of "legacy members retained for backward compatibility" that duplicate state in `DeviceIdentityManager`, `FeatureBitManager`, `AutodiscoveryManager`, and `HaDiscoveryManager`. Every code path must call `sync_*_legacy_members_()` to keep both copies in sync. This doubles the mutation points and is error-prone.
**Fix:** Remove all legacy members and sync functions. Update all callers to use the managers directly.
**Status:** Planned

#### Issue 2: Potential blocking in GEA2 tight loop
**Severity:** Critical
**Location:** `geappliances_bridge.cpp:280-314` — `run_protocol_stack_()`
**Description:** The GEA2 tight loop runs for 200ms wall-clock time in a busy loop. While `esp_task_wdt_reset()` is called inside, this still blocks the ESPHome main loop for 200ms, which can starve other components. The comment acknowledges this is intentional for 19200 baud timing, but there's no safeguard if the loop runs longer than expected (e.g., if `millis()` jumps).
**Fix:** Add a hard cap with exponential backoff. If the loop exceeds GEA2_LOOP_DURATION_MS by more than 2x, log a warning and break. Also verify the inner `while (s_gea2_last_ms < now_ms)` loop cannot spin indefinitely.
**Status:** Planned

#### Issue 3: `millis()` overflow not handled in timers
**Severity:** Medium
**Location:** Multiple places using `millis() - start_ms` pattern
**Description:** The code uses `millis() - start_time` for timeout checks. While unsigned subtraction handles overflow correctly in C++, there are places that use absolute comparisons or store timestamps that could cause issues after ~49 days of uptime.
**Fix:** Audit all timer comparisons. Ensure they all use the subtraction pattern. Add explicit comments documenting the overflow safety.
**Status:** Planned

#### Issue 4: Missing tests for timer non-blocking behavior
**Severity:** High
**Location:** `test/tests/` — no tests verify that timers don't block
**Description:** There are no unit tests that verify the timer subsystem behaves correctly under edge cases: timer expiry during heavy load, timer callback that triggers another timer, or timer group starvation when multiple timers are period-0.
**Fix:** Add tests for timer group behavior, especially the dual-adapter scenario where two period-0 UART poll timers compete.
**Status:** Planned

#### Issue 5: `appliance_api_feature_lists.h` is hand-edited instead of regenerated
**Severity:** Medium
**Location:** `components/geappliances_bridge/appliance_api_feature_lists.h`
**Description:** The feature lists header shows manual edits (42 insertions, 42 deletions in last commit) rather than being regenerated from the JSON source. The Makefile has a rule to regenerate it, but the manual changes may diverge from the source of truth.
**Fix:** Re-run `generate_erd_lists.py` to regenerate from JSON. Add a test that validates the generated output matches expectations.
**Status:** Planned

#### Issue 6: No error handling for `active_erd_client_` being nullptr
**Severity:** Medium
**Location:** `geappliances_bridge_startup_hsm.cpp:176` — device_id phase entry
**Description:** `bridge->active_erd_client_` is passed to `device_identity_manager_.init()` but it's only set during `initialize_mqtt_bridge_()`, which happens in phase 6. However, phase 3 (device_id) runs before phase 6. The device identity manager may receive a nullptr.
**Fix:** Audit the init sequence. Either ensure `active_erd_client_` is set earlier or add null guards.
**Status:** Planned

#### Issue 7: `geappliances_bridge.h` is 314 lines — too large for a header
**Severity:** Low
**Location:** `geappliances_bridge.h`
**Description:** The main header is 314 lines with 300+ member variables. This makes compilation slow and the class hard to reason about.
**Fix:** Continue extracting managers. Move buffer members to the managers that own them. Consider pimpl pattern for implementation details.
**Status:** Planned

#### Issue 8: Duplicate code in HSM state handlers
**Severity:** Medium
**Location:** `geappliances_bridge_startup_hsm.cpp` — device_id_complete and device_id_failed handlers
**Description:** Lines 230-242 duplicate the same 5 lines of code (sync fields + notify sensors + transition). This is a maintenance hazard — if the sync logic changes, it must be updated in 4 places.
**Fix:** Extract a `finalize_device_id_()` helper method.
**Status:** Planned

#### Issue 9: `teardown()` may double-destroy polling bridge
**Severity:** High
**Location:** `geappliances_bridge.cpp:602-631`
**Description:** The teardown logic has complex conditional paths for destroying `mqtt_bridge_polling_`. If `custom_erd_polling_started_` is true AND `is_poll_mode` is true AND `!custom_erd_polling_started_` is false, it skips the second destroy. But if `custom_erd_polling_started_` is false and `is_poll_mode` is true, it destroys. The logic is confusing and could lead to double-free or memory leaks.
**Fix:** Simplify teardown with a clear ownership model. Track which bridge was actually initialized.
**Status:** Planned

#### Issue 10: No test coverage for the startup HSM state machine
**Severity:** High
**Location:** `test/tests/` — no HSM tests
**Description:** The startup HSM is the core orchestration mechanism but has no dedicated unit tests. All 213 tests cover individual managers, but not the state machine transitions, signal handling, or timeout behavior.
**Fix:** Add tests for each HSM state, covering normal transitions, timeout paths, and signal handling.
**Status:** Planned

---

## Next Steps

1. Start with Issue 8 (duplicate code) — quick win, low risk
2. Fix Issue 6 (nullptr active_erd_client_) — correctness issue
3. Fix Issue 9 (teardown double-destroy) — potential crash
4. Address Issue 2 (GEA2 blocking) — critical for non-blocking requirement
5. Fix Issue 3 (millis overflow) — robustness
6. Address Issue 1 (legacy members) — large refactoring, do incrementally
7. Add tests for Issues 4 and 10
8. Regenerate headers (Issue 5)
9. Reduce header size (Issue 7)

Each fix will be committed with all 4 verification gates passing.
