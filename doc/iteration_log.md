# Iteration Log

This file tracks issues found during iterative improvement of the codebase and how they were fixed.

## Goals
- Professional, robust code
- Non-blocking timers (main loop never interrupted)
- Well-tested behavior
- Modular code structure
- Up-to-date documentation

## Iteration 1 — Baseline Fixes (COMPLETED)

### Date: 2026-05-24
### Commit: 2d1e7b1

### Issues Fixed

#### 1. Feature bits phase timeout causes permanent HSM hang (CRITICAL)
**File:** `geappliances_bridge_startup_hsm.cpp`, `feature_bit_manager.h`, `feature_bit_manager.cpp`

**Problem:** When the feature_bits phase timed out (120 s), the HSM called `sync_feature_bit_legacy_members_()` but never set `feature_bit_manager_.is_complete()` to true. The HSM checks `is_complete()` to decide whether to transition to `bridge_init`. Since it stayed false, the HSM was permanently stuck in the feature_bits state.

**Fix:** Added `FeatureBitManager::mark_timed_out()` which forces the manager into `FEATURE_BIT_STATE_COMPLETE`, triggers immediate parsing of whatever data was collected, and sets `parse_pending_` to false so `is_complete()` returns true. Updated the HSM timeout handler to call this before syncing.

#### 2. GEA2 tight loop has no safety cap against runaway execution
**File:** `geappliances_bridge.cpp`

**Problem:** The GEA2 tight loop runs for `GEA2_LOOP_DURATION_MS` (200 ms) with no upper bound. If `millis()` jumps (e.g., after deep sleep wake) or `interface_run` stalls, the loop could run indefinitely, starving the ESPHome watchdog.

**Fix:** Added a hard cap of `GEA2_LOOP_DURATION_MS * 2` (400 ms) with a break condition inside the loop. Also added a cap of 1000 interrupts on the inner msec-catchup loop to prevent replaying accumulated boot time.

#### 3. millis() overflow vulnerability in ha_discovery_manager cleanup
**File:** `ha_discovery_manager.cpp`

**Problem:** `uint32_t deadline = millis() + 5000` wraps incorrectly when `millis()` is near `UINT32_MAX`. The `millis() < deadline` check would then be true forever, causing an infinite loop.

**Fix:** Changed to `uint32_t start = millis()` with `millis() - start < 5000`, which wraps correctly due to unsigned arithmetic.

#### 4. Confusing teardown logic with potential for double-free
**File:** `geappliances_bridge.cpp`, `geappliances_bridge.h`, `geappliances_bridge_bridge_init.cpp`

**Problem:** Teardown used complex conditional logic (`custom_erd_polling_started_`, `is_poll_mode`, `subscription_mode_active_`) to decide which bridges to destroy. The logic was correct but extremely hard to reason about and prone to future bugs.

**Fix:** Added explicit `subscription_bridge_initialized_` and `polling_bridge_initialized_` flags set during init and cleared during destroy. Teardown now simply checks these flags.

#### 5. Misleading comment on active_erd_client_ initialization
**File:** `geappliances_bridge.h`

**Problem:** Comment said "set during initialize_mqtt_bridge_()" but it's actually set during `sync_autodiscovery_legacy_members_()` when autodiscovery completes.

**Fix:** Updated comment to reflect actual initialization path.

#### 6. Dead code in bridge class (duplicated by managers)
**File:** `geappliances_bridge_device_id.cpp`, `geappliances_bridge.h`

**Problem:** `try_read_erd_with_retry_()`, `bytes_to_string_()`, and `sanitize_for_mqtt_topic_()` were defined in the bridge class but never called. Also `queue_retry_count_`, `LOG_EVERY_N_RETRIES`, and `MAX_QUEUE_RETRIES` were unused in the bridge.

**Fix:** Removed all dead methods and unused member variables from the bridge class.

#### 7. Duplicated device ID finalization code (5x) in startup HSM
**File:** `geappliances_bridge_device_id.cpp`, `geappliances_bridge_startup_hsm.cpp`, `geappliances_bridge.h`

**Problem:** The same 4-line sequence (sync device_id, generated_device_id, check for empty, notify sensors) was duplicated across 5 branches in the device_id state handler.

**Fix:** Extracted `finalize_device_id_(bool sync_all)` helper method.

---

## Verification
- All 213 unit tests pass
- All 5 device configurations compile successfully (zonelinec6, haieridu, dishwasher, waterheaterc3, combi)
- All 5 devices flashed and running successfully

## Issues Remaining

### 8. Legacy member duplication — bridge mirrors manager state
**File:** `geappliances_bridge.h`

**Problem:** The bridge maintains parallel copies of state owned by managers (e.g., `device_id_`, `appliance_type_`, `model_number_`, `serial_number_`, `feature_bit_erd_*`, `appliance_api_valid_erds_`). This is a maintenance burden and risk of inconsistency.

**Status:** Noted for future refactoring. Legacy sync methods retained for backward compatibility with `dump_config()` and sensor updates.

### 9. Module documentation not updated for new managers
**File:** `doc/module_descriptions/`

**Problem:** Module descriptions don't cover `DeviceIdentityManager`, `FeatureBitManager`, `AutodiscoveryManager`, or the startup HSM in detail.

**Status:** To be addressed.

### 10. No tests for startup HSM timeout paths
**File:** `test/tests/`

**Problem:** The feature_bits timeout path (fixed in Issue 1) and device_id timeout path have no unit tests.

**Status:** To be addressed.