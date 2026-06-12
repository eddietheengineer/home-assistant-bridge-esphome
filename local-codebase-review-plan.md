# Codebase Review Plan

## Context
Comprehensive code review of the home-assistant-bridge-esphome ESPHome component that bridges GE appliances to Home Assistant via MQTT. The review covers all C++ implementation files, Python component, test infrastructure, and library interfaces. The goal is to identify bugs, memory safety issues, design problems, and test gaps.

## Status
Last reviewed: 2026-06-12.  ~50 files examined across the codebase.

## Findings

### FIXED (1)

**H2. `geappliances_bridge_bridge_init.cpp:237` — Fragile line continuation**
`&& \` line continuation at wide column. Trailing whitespace breaks the condition.
**Fix:** Split into three lines without backslash continuation.

### DISMISSED — Not Issues (7)

**C1. `mqtt_bridge_polling.cpp:852` — `delete[]` on potentially null pointer**
`mqtt_bridge_polling_destroy()` guards with `if (self->erd_polling_list != nullptr)` before `delete[]`. Already fixed in current code.

**C2. `geappliances_bridge.cpp:336` — GEA2 tight loop blocks 200ms**
By design. The GEA2 protocol at 19200 baud requires a ~200ms TX→RX cycle. Hard safety cap at 400ms prevents runaway. `esp_task_wdt_reset()` is called inside the loop. Documented in doc/geappliances_bridge.md §13.

**C3. `geappliances_bridge_startup_hsm.cpp:33` — Global mutable `g_bridge_services`**
By design. The HSM state functions are free functions that need a back-pointer to the bridge. Set once during init, never cleared. The alternative (passing context through every HSM signal) would require changing the tiny_hsm API.

**H1. `feature_bit_manager.cpp:147` — Silent data truncation**
Now logs a warning: `ESP_LOGW(TAG, "Feature bit ERD 0x%04X: data truncated from %u to %u bytes", ...)`. Already fixed in current code.

**M9. `mqtt_bridge_polling.cpp:216` — No null check on `data` in signal handlers**
`handle_discovery_list_signals` guards at line 216: `if (data == nullptr && signal != signal_timer_expired) { return deferred; }`. The `signal_timer_expired` case does not dereference `args`. Already safe.

**M11. Pragma soup for unused variables**
Variables guarded by pragmas (`mode_name`, `mode_str`, `phase_str`, `feature_name`, `erd_names`) ARE used in ESP_LOG macros that may be compiled out at certain log levels. The pragma push/pop pattern is the correct way to handle this — `(void)x` would suppress the warning but the variable wouldn't be used if the log is compiled out, defeating the purpose.

**M3. `custom_erd_subscription_seen_erds_` grows unboundedly**
Bounded by protocol: max ~200 distinct ERDs per appliance. Memory impact is negligible (~4 KB). Cleared on bridge re-init.

**M6. External library deps without version pinning**
`cg.add_library(..., None)` is ESPHome's convention for "use the commit at the URL's ref". The `#develop` ref on tiny-gea-api pins to that branch. Documented in code comments.

### REMAINING — Design Trade-offs (3)

**C2 (design). GEA2 tight loop blocks ESPHome main task 200ms**
Acceptable trade-off. The GEA2 protocol at 19200 baud requires a full TX→RX cycle within one loop() call. The 400ms hard cap and `esp_task_wdt_reset()` inside the loop prevent watchdog resets.

**M4. `mqtt_bridge_polling.cpp:50-52` — `reinterpret_cast` for STL containers as `void*`**
Necessary pattern: C structs cannot hold C++ types directly. The `new`/`delete` pairs are matched and null-guarded. Alternative would require rewriting the C struct definitions.

**M5. `ha_discovery_manager.cpp:171-172` — Hardcoded heap thresholds**
`HA_FETCH_MIN_FREE_HEAP = 110*1024` and `HA_FETCH_STACK_SIZE = 49152` are ESP32-specific. ESP32-C3 has less PSRAM, but the check gracefully skips HA discovery if heap is insufficient rather than crashing.

## Verification
Build with `make test` — all 203 tests pass (1073 checks).
