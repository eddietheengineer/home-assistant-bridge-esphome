# Plan: Remove MQTT Client Dependency from Polling and Subscribe Bridges

## Goal

Both `erd_bridge_poll` and `erd_bridge_subscribe` should have **zero** direct interaction with the MQTT client. Their only output is the shared ERD cache; the ERD cache publisher handles all MQTT publishing. Write requests are accepted as a temporary gap — they will be handled by a future `erd_write_bridge` (see `doc/erd_write_bridge.md`).

## Changes by File

### 1. `erd_bridge_poll.h`

| Change | Detail |
|--------|--------|
| Remove `#include "i_mqtt_client.h"` | No longer needed |
| Remove `i_mqtt_client_t* mqtt_client` field | No longer stored |
| Remove `tiny_event_subscription_t mqtt_write_request_subscription` | No longer subscribes to write requests |
| Remove `tiny_event_subscription_t mqtt_disconnect_subscription` | No longer subscribes to disconnect |
| Remove `bool only_publish_on_change` | Cache handles change detection |
| Remove `void* pending_registration_set` | Registration is a cache concept, not MQTT |
| Update module goal comment | Remove MQTT references |

### 2. `erd_bridge_poll.cpp`

| Change | Detail |
|--------|--------|
| Remove `add_erd_to_polling_list()` MQTT registration | Remove `mqtt_client_register_erd()` call; the function becomes a pure polling-list operation |
| Remove `add_erd_to_polling_list_no_register()` | Merge into `add_erd_to_polling_list()` — no distinction needed without deferred registration |
| Remove `pending_registration_set()` helper | No longer needed |
| Remove `mqtt_client_update_erd()` from `handle_discovery_list_signals()` | Discovery values written to cache instead |
| Remove `mqtt_client_update_erd()` from `state_polling` `signal_read_completed` | Replace with `erd_cache_update()` (already called for change detection) |
| Remove `mqtt_client_register_erd()` from `state_polling` `signal_read_completed` | No deferred registration |
| Remove `handle_write_result()` from ERD client activity callback | Write results handled by write bridge |
| Remove `setup_write_request_subscription()` from init | No longer subscribes |
| Remove `setup_disconnect_subscription()` from init | No longer subscribes |
| Remove `signal_mqtt_disconnected` handler from `state_polling` | Dead code |
| Remove `signal_write_requested` handler from `poll_state_top` | Dead code |
| Remove `mqtt_client` parameter from `erd_bridge_poll_init_impl()` | No longer stored |
| Remove `only_publish_on_change` parameter from `erd_bridge_poll_init_impl()` | No longer used |
| Remove `mqtt_client` from init calls | `erd_bridge_poll_init()` and `erd_bridge_poll_init_legacy()` |
| Remove `mqtt_client` unsubscribe from `erd_bridge_poll_destroy()` | No longer subscribed |
| Remove `pending_registration_set` from `erd_bridge_poll_init_impl()` | No longer allocated |
| Remove `pending_registration_set` from `erd_bridge_poll_destroy()` | No longer freed |
| Remove `#include <set>` if only used for `pending_registration_set` | `erd_set` still needs `<set>` |

### 3. `erd_bridge_subscribe.h`

| Change | Detail |
|--------|--------|
| Remove `#include "i_mqtt_client.h"` | No longer needed |
| Remove `i_mqtt_client_t* mqtt_client` field | No longer stored |
| Remove `tiny_event_subscription_t mqtt_write_request_subscription` | No longer subscribes |
| Remove `tiny_event_subscription_t mqtt_disconnect_subscription` | No longer subscribes |
| Update `erd_bridge_subscribe_init()` signature | Remove `mqtt_client` parameter |
| Update module goal comment | Remove MQTT references |

### 4. `erd_bridge_subscribe.cpp`

| Change | Detail |
|--------|--------|
| Remove `mqtt_client_register_erd()` from `sub_state_top` `signal_subscription_publication_received` | Registration is a cache concept |
| Remove `mqtt_client_update_erd()` from `sub_state_top` `signal_subscription_publication_received` | Cache handles publishing |
| Remove `signal_write_requested` handler from `sub_state_top` | Write requests handled by write bridge |
| Remove `handle_write_result()` from ERD client activity callback | Write results handled by write bridge |
| Remove `setup_write_request_subscription()` from init | No longer subscribes |
| Remove `setup_disconnect_subscription()` from init | No longer subscribes |
| Remove `signal_mqtt_disconnected` handler from `state_subscribed` | Dead code |
| Remove `mqtt_client` parameter from `erd_bridge_subscribe_init()` | No longer stored |
| Remove `mqtt_client` unsubscribe from `erd_bridge_subscribe_destroy()` | No longer subscribed |
| Update `state_subscribing` `signal_mqtt_disconnected` references in comments | Remove or rewrite |

### 5. `erd_bridge_common.h`

| Change | Detail |
|--------|--------|
| Remove `handle_write_result()` function | No longer used by either bridge |
| Remove `setup_write_request_subscription()` template | No longer used |
| Remove `setup_disconnect_subscription()` template | No longer used |
| Remove `signal_mqtt_disconnected` from shared signal enum | No longer used |
| Remove `signal_write_requested` from shared signal enum | No longer used |
| Remove `#include "i_mqtt_client.h"` | No longer needed |
| Update module goal comment | Remove MQTT references |

### 6. `geappliances_bridge_bridge_init.cpp`

| Change | Detail |
|--------|--------|
| Remove `&this->mqtt_client_adapter_.interface` from all `erd_bridge_poll_init()` calls (3 sites) | Parameter removed |
| Remove `this->polling_only_publish_on_change_` from all `erd_bridge_poll_init()` calls (3 sites) | Parameter removed |
| Remove `&this->mqtt_client_adapter_.interface` from `erd_bridge_subscribe_init()` call (1 site) | Parameter removed |

### 7. `geappliances_bridge.cpp`

| Change | Detail |
|--------|--------|
| Replace `erd_registry_.registered_erds().size()` in state change log (line 393) | Use `erd_cache_get_count(&erd_cache_)` instead |
| `teardown()` | No other changes — `erd_bridge_poll_destroy()` / `erd_bridge_subscribe_destroy()` signatures unchanged |

### 8. Test files
| File | Changes |
| `test/tests/erd_bridge_poll_test.cpp` | Remove `&mqtt_client.interface` from all `erd_bridge_poll_init()` calls; remove `only_publish_on_change` argument; remove `mqtt_client_double_init()` calls and `mqtt_client` local variable (4 test groups) |
| `test/tests/erd_bridge_subscribe_test.cpp` | Remove `&mqtt_client.interface` from all `erd_bridge_subscribe_init()` calls; remove `mqtt_client_double_init()` calls (2 test groups); remove `should_trigger_write_request()` and `should_trigger_mqtt_disconnect()` helpers and all callers |
| `test/simulation/appliance_simulation_examples.cpp` | Remove `&mqtt_client.interface` from subscribe init; remove `mqtt_client_double_init()`; remove `mqtt_client_double_trigger_write_request()` and `mqtt_client_double_trigger_mqtt_disconnect()` calls |
| `test/simulation/application_level_test.cpp` | Remove `&mqtt_client.interface` from both init helpers; remove `only_publish_on_change` arg from poll init; remove `mqtt_client_double_init()`; remove `mqtt_client_double_trigger_write_request()` call |
| `test/simulation/configuration_tests.cpp` | Remove `&mqtt_client.interface` from all init calls; remove `only_publish_on_change` arg from poll init; remove `mqtt_client_double_init()` calls (3 groups); remove `mqtt_client_double_trigger_write_request()` call |

### 9. `doc/spec/erd_bridge_poll.md`

| Change | Detail |
|--------|--------|
| Remove all MQTT references from overview, responsibilities, state machine, discovery, and polling sections | Bridge has zero MQTT interaction |
| Update "Not Responsible For" | Add "Any MQTT behavior (publishing, write requests, disconnect handling)" |
| Remove `only_publish_on_change` parameter from init signature | No longer exists |
| Remove `mqtt_client` parameter from init signature | No longer exists |
| Remove `pending_registration_set` from data structures | No longer exists |
| Remove `signal_mqtt_disconnected` and `signal_write_requested` from state machine | No longer handled |

### 10. `doc/spec/erd_bridge_subscribe.md` (if it exists) or `doc/module_descriptions/erd_bridge_subscribe.md`

| Change | Detail |
|--------|--------|
| Same treatment as polling bridge spec | Remove all MQTT references |

## What Stays

- `erd_cache_t* erd_cache` — the sole output mechanism for both bridges
- `i_tiny_gea3_erd_client_t* erd_client` — the sole input mechanism
- `tiny_timer_group_t*` — for timers
- `tiny_hsm_t` — state machine
- `erd_set` — deduplication set (still used by both bridges)
- `on_discovery_complete` callback — still used by polling bridge
- `api_parsed_list` / `custom_erd_list` — still used by polling bridge

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Tests that verify MQTT publish behavior will break | Those assertions move to the cache publisher tests; bridge tests verify cache state instead |
| Write request tests in subscribe bridge will break | Write tests removed; write handling is a temporary gap until `erd_write_bridge` is implemented |
| Disconnect tests will break | Disconnect is handled by the cache publisher; remove the tests |
| `erd_registry_.registered_erds()` used by `on_discovery_complete` | Replaced: the callback iterates the cache via `erd_cache_get_next_entry()` to build the ERD set for HA discovery |

## Resolved Decisions

1. **`erd_registry_.registered_erds()`** — The `on_discovery_complete` callback currently calls `bridge->erd_registry_.registered_erds()` for HA discovery. The registry is populated by `mqtt_client_register_erd()` calls inside the bridges. After removing those calls, the callback will iterate the cache directly using a new `erd_cache_get_next_entry()` function (iterates all valid entries, not just updated ones) to build the ERD set for HA discovery.

2. **Write requests** — Accepted as a temporary gap. The `erd_write_bridge` design is documented in `doc/erd_write_bridge.md` for future implementation.

3. **`erd_bridge_poll_init_legacy`** — Already removed in a prior commit.

## New Cache API

Add `erd_cache_get_next_entry()` to `erd_cache.h` / `erd_cache.cpp`:

```c
// Returns the next valid entry in the cache, iterating all entries.
// Caller provides an iterator (uint16_t) initialized to 0.
// Returns NULL when no more valid entries remain (resets iterator to 0).
// Unlike erd_cache_get_next_updated(), this does NOT require update_required=true
// and does NOT clear any flags — it is a read-only iteration.
erd_cache_entry_t* erd_cache_get_next_entry(erd_cache_t* self, uint16_t* iterator);
```

This is used by the `on_discovery_complete` callback to enumerate all cached ERDs for HA discovery registration, replacing the `erd_registry_.registered_erds()` call.

## Additional Changes

### 11. `erd_cache.h` / `erd_cache.cpp`

| Change | Detail |
|--------|--------|
| Add `erd_cache_get_next_entry()` | New read-only iterator over all valid cache entries |

### 12. `geappliances_bridge_bridge_init.cpp` — `on_discovery_complete` callback

| Change | Detail |
|--------|--------|
| Replace `erd_registry_.registered_erds()` | Iterate cache via `erd_cache_get_next_entry()` to build `std::set<tiny_erd_t>` for `ha_discovery_manager_.set_registered_erds()` |
| Replace `erd_registry_.registered_erds()` at HA discovery init (line 220) | Same: iterate cache instead of registry |

### 13. `geappliances_bridge.cpp` — debug log

| Change | Detail |
|--------|--------|
| Replace `erd_registry_.registered_erds().size()` in state change log (line 393) | Use `erd_cache_get_count(&erd_cache_)` instead |

## Order of Operations

1. Add `erd_cache_get_next_entry()` to cache API
2. Remove MQTT calls from both bridges (sections 1-4)
3. Remove shared MQTT helpers from `erd_bridge_common.h` (section 5)
4. Update production callers in `geappliances_bridge_bridge_init.cpp` (sections 6, 12)
5. Update `geappliances_bridge.cpp` debug log (section 13)
6. Update all test files (section 8)
7. Update spec documents (sections 9-10)
8. Run tests, fix any remaining issues
