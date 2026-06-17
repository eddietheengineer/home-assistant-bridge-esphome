# Plan: Rename mqtt_bridge → erd_bridge_subscribe, mqtt_bridge_polling → erd_bridge_poll

## Motivation

`mqtt_bridge` and `mqtt_bridge_polling` no longer handle MQTT directly — they manage the appliance-to-ERD-cache data path for subscription and polling modes respectively. The names are misleading.

## Rename Mapping

| Old | New |
|-----|-----|
| `mqtt_bridge.h` | `erd_bridge_subscribe.h` |
| `mqtt_bridge.cpp` | `erd_bridge_subscribe.cpp` |
| `mqtt_bridge_polling.h` | `erd_bridge_poll.h` |
| `mqtt_bridge_polling.cpp` | `erd_bridge_poll.cpp` |
| `mqtt_bridge_common.h` | `erd_bridge_common.h` |
| `mqtt_bridge_t` | `erd_bridge_subscribe_t` |
| `mqtt_bridge_polling_t` | `erd_bridge_poll_t` |
| `mqtt_bridge_init()` | `erd_bridge_subscribe_init()` |
| `mqtt_bridge_destroy()` | `erd_bridge_subscribe_destroy()` |
| `mqtt_bridge_polling_init()` | `erd_bridge_poll_init()` |
| `mqtt_bridge_polling_init_at_address()` | `erd_bridge_poll_init_at_address()` |
| `mqtt_bridge_polling_destroy()` | `erd_bridge_poll_destroy()` |
| `mqtt_bridge_initialized_` | `erd_bridge_initialized_` |
| `mqtt_bridge_` (member) | `erd_bridge_subscribe_` |
| `mqtt_bridge_polling_` (member) | `erd_bridge_poll_` |
| `initialize_mqtt_bridge()` / `initialize_mqtt_bridge_()` | `initialize_erd_bridge()` / `initialize_erd_bridge_()` |

## File-by-file changes

### 1. Rename source files (git mv)
- `components/geappliances_bridge/mqtt_bridge.h` → `erd_bridge_subscribe.h`
- `components/geappliances_bridge/mqtt_bridge.cpp` → `erd_bridge_subscribe.cpp`
- `components/geappliances_bridge/mqtt_bridge_polling.h` → `erd_bridge_poll.h`
- `components/geappliances_bridge/mqtt_bridge_polling.cpp` → `erd_bridge_poll.cpp`
- `components/geappliances_bridge/mqtt_bridge_common.h` → `erd_bridge_common.h`

### 2. `erd_bridge_subscribe.h` (formerly `mqtt_bridge.h`)
- `#ifndef mqtt_bridge_h` → `#ifndef erd_bridge_subscribe_h`
- `#define mqtt_bridge_h` → `#define erd_bridge_subscribe_h`
- `} mqtt_bridge_t;` → `} erd_bridge_subscribe_t;`
- `void mqtt_bridge_init(` → `void erd_bridge_subscribe_init(`
- `mqtt_bridge_t* self,` → `erd_bridge_subscribe_t* self,`
- `void mqtt_bridge_destroy(` → `void erd_bridge_subscribe_destroy(`
- `mqtt_bridge_t* self);` → `erd_bridge_subscribe_t* self);`
- Update doc comment: "see mqtt_bridge_polling.h" → "see erd_bridge_poll.h"

### 3. `erd_bridge_subscribe.cpp` (formerly `mqtt_bridge.cpp`)
- `#include "mqtt_bridge_common.h"` → `#include "erd_bridge_common.h"`
- All `mqtt_bridge_t` → `erd_bridge_subscribe_t`
- `TAG = "mqtt_bridge"` → `TAG = "erd_bridge_subscribe"`
- `mqtt_bridge_init(` → `erd_bridge_subscribe_init(`
- `mqtt_bridge_destroy(` → `erd_bridge_subscribe_destroy(`
- All `reinterpret_cast<mqtt_bridge_t*>` → `reinterpret_cast<erd_bridge_subscribe_t*>`
- `container_of(mqtt_bridge_t, ...)` → `container_of(erd_bridge_subscribe_t, ...)`
- Update doc comment references to `erd_bridge_poll.cpp` and `erd_bridge_common.h`

### 4. `erd_bridge_poll.h` (formerly `mqtt_bridge_polling.h`)
- `#ifndef mqtt_bridge_polling_h` → `#ifndef erd_bridge_poll_h`
- `#define mqtt_bridge_polling_h` → `#define erd_bridge_poll_h`
- `} mqtt_bridge_polling_t;` → `} erd_bridge_poll_t;`
- `void mqtt_bridge_polling_init(` → `void erd_bridge_poll_init(`
- `mqtt_bridge_polling_t* self,` → `erd_bridge_poll_t* self,`
- `void mqtt_bridge_polling_init_at_address(` → `void erd_bridge_poll_init_at_address(`
- `void mqtt_bridge_polling_destroy(` → `void erd_bridge_poll_destroy(`
- Update all doc comments referencing `mqtt_bridge.h` → `erd_bridge_subscribe.h`
- Update doc comments referencing `mqtt_bridge_polling_init()` → `erd_bridge_poll_init()`
- Update doc comments referencing `mqtt_bridge_polling_init_at_address()` → `erd_bridge_poll_init_at_address()`

### 5. `erd_bridge_poll.cpp` (formerly `mqtt_bridge_polling.cpp`)
- `#include "mqtt_bridge_common.h"` → `#include "erd_bridge_common.h"`
- All `mqtt_bridge_polling_t` → `erd_bridge_poll_t`
- `TAG = "mqtt_bridge_polling"` → `TAG = "erd_bridge_poll"`
- `mqtt_bridge_polling_init(` → `erd_bridge_poll_init(`
- `mqtt_bridge_polling_init_at_address(` → `erd_bridge_poll_init_at_address(`
- `mqtt_bridge_polling_destroy(` → `erd_bridge_poll_destroy(`
- `mqtt_bridge_polling_init_impl(` → `erd_bridge_poll_init_impl(`
- All `reinterpret_cast<mqtt_bridge_polling_t*>` → `reinterpret_cast<erd_bridge_poll_t*>`
- `container_of(mqtt_bridge_polling_t, ...)` → `container_of(erd_bridge_poll_t, ...)`
- Update doc comment referencing `mqtt_bridge_polling_init_at_address` → `erd_bridge_poll_init_at_address`

### 6. `erd_bridge_common.h` (formerly `mqtt_bridge_common.h`)
- `#include "mqtt_bridge.h"` → `#include "erd_bridge_subscribe.h"`
- `#include "mqtt_bridge_polling.h"` → `#include "erd_bridge_poll.h"`
- Update doc comments: `mqtt_bridge.cpp` → `erd_bridge_subscribe.cpp`, `mqtt_bridge_polling.cpp` → `erd_bridge_poll.cpp`
- Update doc comments: `mqtt_bridge.h` → `erd_bridge_subscribe.h`, `mqtt_bridge_polling.h` → `erd_bridge_poll.h`

### 7. `geappliances_bridge.h`
- `#include "mqtt_bridge.h"` → `#include "erd_bridge_subscribe.h"`
- `#include "mqtt_bridge_polling.h"` → `#include "erd_bridge_poll.h"`
- `bool mqtt_bridge_initialized_{false};` → `bool erd_bridge_initialized_{false};`
- `void initialize_mqtt_bridge() override;` → `void initialize_erd_bridge() override;`
- `void initialize_mqtt_bridge_();` → `void initialize_erd_bridge_();`
- `mqtt_bridge_t mqtt_bridge_;` → `erd_bridge_subscribe_t erd_bridge_subscribe_;`
- `mqtt_bridge_polling_t mqtt_bridge_polling_;` → `erd_bridge_poll_t erd_bridge_poll_;`
- Update comment: "see mqtt_bridge_polling.cpp" → "see erd_bridge_poll.cpp"
- Update comments about `mqtt_bridge_` and `mqtt_bridge_polling_`

### 8. `geappliances_bridge.cpp`
- All `mqtt_bridge_initialized_` → `erd_bridge_initialized_`
- All `mqtt_bridge_` → `erd_bridge_subscribe_` (but NOT `mqtt_bridge_polling_` — that's a separate rename)
- All `mqtt_bridge_polling_` → `erd_bridge_poll_`
- `mqtt_bridge_destroy(` → `erd_bridge_subscribe_destroy(`
- `mqtt_bridge_polling_destroy(` → `erd_bridge_poll_destroy(`
- `void GeappliancesBridge::initialize_mqtt_bridge()` → `void GeappliancesBridge::initialize_erd_bridge()`
- `initialize_mqtt_bridge_();` → `initialize_erd_bridge_();`

### 9. `geappliances_bridge_bridge_init.cpp`
- `void GeappliancesBridge::initialize_mqtt_bridge_()` → `void GeappliancesBridge::initialize_erd_bridge_()`
- All `mqtt_bridge_initialized_` → `erd_bridge_initialized_`
- All `mqtt_bridge_` → `erd_bridge_subscribe_` (but NOT `mqtt_bridge_polling_`)
- All `mqtt_bridge_polling_` → `erd_bridge_poll_`
- `mqtt_bridge_polling_init(` → `erd_bridge_poll_init(`
- `mqtt_bridge_polling_init_at_address(` → `erd_bridge_poll_init_at_address(`
- `mqtt_bridge_init(` → `erd_bridge_subscribe_init(`
- `mqtt_bridge_destroy(` → `erd_bridge_subscribe_destroy(`
- `mqtt_bridge_polling_destroy(` → `erd_bridge_poll_destroy(`
- Update doc comments referencing `initialize_mqtt_bridge_()` → `initialize_erd_bridge_()`

### 10. `geappliances_bridge_startup_hsm.cpp`
- `svc->initialize_mqtt_bridge();` → `svc->initialize_erd_bridge();`

### 11. `i_bridge_services.h`
- `virtual void initialize_mqtt_bridge() = 0;` → `virtual void initialize_erd_bridge() = 0;`
- Update doc comment

### 12. `i_mqtt_client.h`
- Update doc comment: `mqtt_bridge` → `erd_bridge_subscribe`, `mqtt_bridge_polling` → `erd_bridge_poll`

### 13. `gea2_erd_client_adapter.h`
- Update doc comment: `mqtt_bridge_polling` → `erd_bridge_poll`

### 14. `Makefile`
- `components/geappliances_bridge/mqtt_bridge.cpp` → `components/geappliances_bridge/erd_bridge_subscribe.cpp`
- `components/geappliances_bridge/mqtt_bridge_polling.cpp` → `components/geappliances_bridge/erd_bridge_poll.cpp`

### 15. Test files — `test/tests/mqtt_bridge_test.cpp`
- Rename to `test/tests/erd_bridge_subscribe_test.cpp`
- `#include "mqtt_bridge.h"` → `#include "erd_bridge_subscribe.h"`
- `TEST_GROUP(mqtt_bridge)` → `TEST_GROUP(erd_bridge_subscribe)`
- All `mqtt_bridge_t` → `erd_bridge_subscribe_t`
- All `mqtt_bridge_init(` → `erd_bridge_subscribe_init(`
- All `mqtt_bridge_destroy(` → `erd_bridge_subscribe_destroy(`
- `TEST_GROUP(mqtt_bridge_dual)` → `TEST_GROUP(erd_bridge_subscribe_dual)`
- All `TEST(mqtt_bridge, ...)` → `TEST(erd_bridge_subscribe, ...)`
- All `TEST(mqtt_bridge_dual, ...)` → `TEST(erd_bridge_subscribe_dual, ...)`

### 16. Test files — `test/tests/mqtt_bridge_polling_test.cpp`
- Rename to `test/tests/erd_bridge_poll_test.cpp`
- `#include "mqtt_bridge_polling.h"` → `#include "erd_bridge_poll.h"`
- `TEST_GROUP(mqtt_bridge_polling)` → `TEST_GROUP(erd_bridge_poll)`
- `TEST_GROUP(mqtt_bridge_polling_api_list)` → `TEST_GROUP(erd_bridge_poll_api_list)`
- `TEST_GROUP(mqtt_bridge_polling_custom_erds)` → `TEST_GROUP(erd_bridge_poll_custom_erds)`
- `TEST_GROUP(mqtt_bridge_polling_sequential)` → `TEST_GROUP(erd_bridge_poll_sequential)`
- All `mqtt_bridge_polling_t` → `erd_bridge_poll_t`
- All `mqtt_bridge_polling_init(` → `erd_bridge_poll_init(`
- All `mqtt_bridge_polling_init_at_address(` → `erd_bridge_poll_init_at_address(`
- All `mqtt_bridge_polling_destroy(` → `erd_bridge_poll_destroy(`
- All `TEST(mqtt_bridge_polling, ...)` → `TEST(erd_bridge_poll, ...)`
- All `TEST(mqtt_bridge_polling_api_list, ...)` → `TEST(erd_bridge_poll_api_list, ...)`
- All `TEST(mqtt_bridge_polling_custom_erds, ...)` → `TEST(erd_bridge_poll_custom_erds, ...)`
- All `TEST(mqtt_bridge_polling_sequential, ...)` → `TEST(erd_bridge_poll_sequential, ...)`

### 17. Test files — `test/tests/startup_hsm_test.cpp`
- `void initialize_mqtt_bridge() override {}` → `void initialize_erd_bridge() override {}`

### 18. Test files — `test/simulation/appliance_simulation_examples.cpp`
- `#include "mqtt_bridge.h"` → `#include "erd_bridge_subscribe.h"`
- `#include "mqtt_bridge_polling.h"` → `#include "erd_bridge_poll.h"`
- `mqtt_bridge_t mqtt_bridge;` → `erd_bridge_subscribe_t erd_bridge_subscribe;`
- `mqtt_bridge_polling_t mqtt_bridge_polling;` → `erd_bridge_poll_t erd_bridge_poll;`
- `mqtt_bridge_destroy(&mqtt_bridge);` → `erd_bridge_subscribe_destroy(&erd_bridge_subscribe);`
- `mqtt_bridge_polling_destroy(&mqtt_bridge_polling);` → `erd_bridge_poll_destroy(&erd_bridge_poll);`
- `initialize_mqtt_bridge_subscription_mode()` → `initialize_erd_bridge_subscription_mode()`
- `mqtt_bridge_init(` → `erd_bridge_subscribe_init(`
- `&mqtt_bridge,` → `&erd_bridge_subscribe,`
- `initialize_mqtt_bridge_polling_mode()` → `initialize_erd_bridge_polling_mode()`
- `mqtt_bridge_polling_init(` → `erd_bridge_poll_init(`
- `&mqtt_bridge_polling,` → `&erd_bridge_poll,`
- Update all call sites of these renamed helpers
- Update doc comment: "in mqtt_bridge" → "in erd_bridge_subscribe"

### 19. Test files — `test/simulation/application_level_test.cpp`
- Same pattern as #18: rename includes, types, variables, functions, and call sites.

### 20. Test files — `test/simulation/configuration_tests.cpp`
- Same pattern as #18: rename includes, types, variables, functions, and call sites.
- `bridge_a` / `bridge_b` variables keep their names (they're local) but type changes to `erd_bridge_subscribe_t`.
- Update doc comment: "mqtt_bridge_init now accepts" → "erd_bridge_subscribe_init now accepts"

### 21. Test files — `test/simulation/IMPLEMENTATION_SUMMARY.md`
- Update references: `mqtt_bridge` → `erd_bridge_subscribe`, `mqtt_bridge_polling` → `erd_bridge_poll`

## ⚠️ Critical: `mqtt_bridge_` vs `mqtt_bridge_polling_` ambiguity

When replacing `mqtt_bridge_` in `geappliances_bridge.h` and `geappliances_bridge.cpp`, the search for `mqtt_bridge_` will also match `mqtt_bridge_polling_`. The replacements must be ordered:

1. **First** replace `mqtt_bridge_polling_` → `erd_bridge_poll_` (longer string, more specific)
2. **Then** replace `mqtt_bridge_` → `erd_bridge_subscribe_` (shorter string, will not re-match the already-renamed poll variants)

This ordering prevents `erd_bridge_poll_` from being corrupted into `erd_bridge_subscribe_poll_`.

## Risks

- This is a mechanical rename — no logic changes.
- The `#include` guards, type names, and function names must be consistent across all files.
- Build must pass after every rename pass to catch missed references.
- The `mqtt_client_double.{h,cpp}` test double is unaffected — it implements `i_mqtt_client_t`, not the bridge types.
- `i_mqtt_client.h` keeps its name — it's an interface definition, not a bridge implementation.

## Execution order

1. `git mv` the 5 source files
2. Rename types/functions in the renamed files themselves (sections 2–6)
3. Update all callers in `components/geappliances_bridge/` (sections 7–13) — **respect the polling-before-subscribe ordering**
4. Update `Makefile` (section 14)
5. Update all callers in `test/` (sections 15–21)
6. Build and run tests

## ⚠️ Tool usage note

When creating a todo list from this plan, do NOT use JSON Schema `propertyNames` in any tool call parameters. The harness does not support it. Use simple string arrays for task items.

## Current Status (2026-06-17)

### ✅ Completed

| Section | Status |
|---------|--------|
| 1. Rename source files (git mv) | Done — all 5 files renamed |
| 2. `erd_bridge_subscribe.h` | Done — guard, type, init/destroy functions, doc comments |
| 3. `erd_bridge_subscribe.cpp` | Done — includes, types, TAG, functions, casts, container_of, doc comments |
| 4. `erd_bridge_poll.h` | Done — guard, type, init/destroy functions, doc comments |
| 5. `erd_bridge_poll.cpp` | Done — includes, types, TAG, functions, casts, container_of, doc comments |
| 6. `erd_bridge_common.h` | Done — includes updated to new header names, doc comments updated |
| 7. `geappliances_bridge.h` | Done — includes, member types, flags, method declarations, comments |
| 8. `geappliances_bridge.cpp` | Done — all member references, function calls |
| 9. `geappliances_bridge_bridge_init.cpp` | Done — method definition, all member references, function calls |
| 10. `geappliances_bridge_startup_hsm.cpp` | Done — `initialize_erd_bridge()` call |
| 11. `i_bridge_services.h` | Done — virtual method renamed |
| 12. `i_mqtt_client.h` | Done — doc comment updated |
| 13. `gea2_erd_client_adapter.h` | Done — doc comment updated |
| 14. Makefile | Done — source file paths updated |

### ❌ Remaining

None — all sections complete.

### Summary

The full rename is complete: all 5 source files renamed, all types/functions renamed in component code and test code, all callers updated. Zero `mqtt_bridge` references remain in the codebase. All 242 tests pass.
