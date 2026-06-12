# Codebase Review Plan

## Context
Comprehensive code review of the home-assistant-bridge-esphome ESPHome component that bridges GE appliances to Home Assistant via MQTT. The review covers all C++ implementation files, Python component, test infrastructure, and library interfaces. The goal is to identify bugs, memory safety issues, design problems, and test gaps.

## Approach
This is a read-only review. No code changes are proposed. The review examined ~50 files across the codebase.

## Findings

### CRITICAL (3)

**C1. `mqtt_bridge_polling.cpp:90` — `delete[]` on null pointer in destroy**
`mqtt_bridge_polling_destroy()` calls `delete[] self->erd_polling_list` unconditionally at line 835. If the polling list was never allocated (first `ensure_polling_list_capacity` never ran), `erd_polling_list` is null and `delete[] nullptr` is technically defined in C++ but signals a logic error. More importantly, if `ensure_polling_list_capacity` allocated but the struct was partially zeroed, the pointer could be dangling. Add explicit null check.

**C2. `geappliances_bridge.cpp:336-379` — GEA2 tight loop blocks ESPHome main task 200ms**
The `while (millis() - loop_start_ms < GEA2_LOOP_DURATION_MS)` busy loop blocks the ESPHome main task for 200ms per `loop()` call. While `esp_task_wdt_reset()` is called inside, ESPHome's own component watchdog can fire. The 400ms hard cap helps but doesn't fix the blocking design.

**C3. `geappliances_bridge_startup_hsm.cpp:33` — Global mutable `g_bridge_services`**
Static `IBridgeServices* g_bridge_services` is set once via `set_bridge_services()` and never cleared. Makes testing fragile and could cause issues on reboots.

### HIGH (4)

**H1. `feature_bit_manager.cpp:147` — Silent data truncation**
`copy_size = (size <= 8u) ? size : 8u` silently truncates ERD data >8 bytes with no warning.

**H2. `geappliances_bridge_bridge_init.cpp:137` — Fragile line continuation**
`&& \` line continuation at wide column. Trailing whitespace breaks the condition.

**H3. `__init__.py:182` — 10-second network timeout during config validation**
`urllib.request.urlopen(url, timeout=10)` blocks ESPHome build if GitHub is unreachable.

**H4. `mqtt_bridge_polling.cpp:554-557` — Synchronous polling cycle loop**
`while` loop sends all ERD reads synchronously; with 100+ ERDs this blocks.

### MEDIUM (13)

**M1.** `geappliances_bridge.h:152-158` — MQTT FSM state doesn't reflect reconnect reality
**M2.** `autodiscovery_manager.cpp:216` — Broadcast read could hit null client in fallback path
**M3.** `geappliances_bridge.cpp:438` — `custom_erd_subscription_seen_erds_` grows unboundedly
**M4.** `mqtt_bridge_polling.cpp:736-738` — `reinterpret_cast` for STL containers as `void*`
**M5.** `ha_discovery_manager.cpp:171-172` — Hardcoded heap thresholds may fail on ESP32-C3
**M6.** `__init__.py:317-321` — External library deps without version pinning
**M7.** `device_identity_manager.cpp:91` — `pending_request_id_` never correlated
**M8.** `geappliances_bridge.cpp:202-207` — Disconnect notification only on state transition
**M9.** `mqtt_bridge_polling.cpp:189-233` — No null check on `data` in signal handlers
**M10.** `test/src/esphome_stubs.cpp:9-72` — Stale appliance type mapping in tests
**M11.** `geappliances_bridge_bridge_init.cpp:146-158` — Pragma soup for unused variable
**M12.** `__init__.py:281-306` — Generic ID validation error messages
**M13.** `mqtt_bridge_common.h:76-82` — Unnecessary polymorphic lambda `+[]`

## Verification
Build with `make test` and run the CppUTest suite. Run ESPHome config validation with `esphome config test.yaml`.
