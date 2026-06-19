# Gemma Code Review Fix Plan

## Status: ✅ COMPLETE

All Phase 1 fixes implemented and verified. Phase 2 fixes determined unnecessary after code review.

## Problem Statement
The codebase exhibits systemic heap fragmentation risks on the ESP32 due to the use of dynamic containers (`std::set`, `std::vector`) and frequent small heap allocations for ERD data in the cache.

## Identified Issues
1. **Heap-allocated Sets (High Risk)**: Use of `new std::set<tiny_erd_t>()` in `erd_bridge_subscribe.cpp` and `erd_bridge_poll.cpp`.
2. **Cache Buffer Churn (High Risk)**: Frequent `new (std::nothrow) uint8_t[]` allocations in `erd_cache.cpp` for data > 16 bytes.
3. **Dynamic Feature Lists (Medium Risk)**: Use of `std::set` and `std::vector` in `feature_bit_manager.cpp`.
4. **Cache Update Logic (Medium Risk)**: ~~Incorrect `update_required` flag calculation in `erd_cache.cpp` when heap allocation fails.~~ **INVALID** — existing code correctly handles truncation and `update_required` flags on allocation failure.
5. **MQTT Mutex Jitter (Low Risk)**: ~~Potential jitter in `geappliances_bridge.cpp` due to frequent blocking on the MQTT mutex.~~ **INVALID** — publisher is already gated by `update_required` flags; a "dirty" flag would be redundant.

## Proposed Solutions

### Phase 1: Memory Hard (Highest Priority) — ✅ COMPLETE

#### Replace Dynamic Sets
Created `erd_set_t` struct in `erd_bridge_common.h` with a fixed-capacity array (`tiny_erd_t data[645]`) and helper functions (`erd_set_init`, `erd_set_contains`, `erd_set_insert`, `erd_set_clear`). Replaced `void* erd_set` in both `erd_bridge_subscribe` and `erd_bridge_poll` with direct `erd_set_t` members.

#### Optimize Feature Lists
Converted `std::set<tiny_erd_t>` and `std::vector<tiny_erd_t>` in `feature_bit_manager.cpp` to fixed-capacity arrays (`tiny_erd_t valid_erds_[645]`). Updated `erd_registry.h/cpp` and `ha_discovery_manager.h/cpp` to use raw pointer APIs (`const tiny_erd_t* erds, uint16_t count`) instead of `std::set`.

#### Cache Memory Pool
Implemented a fixed-block memory pool in `erd_cache.cpp` with pre-allocated blocks (`pool_blocks[4][200][128]`). Replaced `uses_heap`/`heap_data` with `uses_pool`/`pool_data` in `erd_cache_entry_t`. All ERD data buffers (17–128 bytes) are now served from the pool, eliminating `new`/`delete` churn during normal operation.

### Phase 2: Logic & Performance Correctness — ❌ NOT NEEDED

#### Fix Cache Update Flags
**Not implemented.** After review, the existing `erd_cache_update` logic correctly handles truncation fallbacks and sets `update_required` based on the final data size. No bug exists.

#### Reduce MQTT Mutex Contention
**Not implemented.** The `erd_cache_mqtt_publisher` is already gated by per-entry `update_required` flags — it only publishes when data has changed. Adding a "dirty" flag would be redundant.

## Changes Summary

### Core files (18 files changed, +713 −494)
| File | Change |
|------|--------|
| `erd_bridge_common.h` | Added `erd_set_t` struct and `arm_timer` template; wrapped tiny headers in `extern "C"` |
| `erd_bridge_subscribe.h/cpp` | Replaced `void* erd_set` with `erd_set_t erd_set`; added `extern "C"` for tiny headers |
| `erd_bridge_poll.h/cpp` | Replaced `void* erd_set` with `erd_set_t erd_set`; replaced dynamic `erd_polling_list` with fixed array `tiny_erd_t[POLLING_LIST_MAX_SIZE]`; added `extern "C"` for tiny headers |
| `erd_cache.h/cpp` | Added memory pool (`pool_blocks[4][200][128]`); replaced `uses_heap`/`heap_data` with `uses_pool`/`pool_data`; implemented `pool_alloc`/`pool_free` |
| `erd_cache_mqtt_publisher.cpp` | Updated to use `entry->uses_pool ? entry->pool_data : entry->inline_data` |
| `feature_bit_manager.h/cpp` | Replaced `std::set`/`std::vector` with fixed `tiny_erd_t valid_erds_[645]` |
| `erd_registry.h/cpp` | Replaced `std::set<tiny_erd_t>` with fixed array; `set_valid_erds` now takes raw pointer + count |
| `ha_discovery_manager.h/cpp` | Replaced `std::set<tiny_erd_t>` members with fixed arrays; updated all APIs to use raw pointers |
| `geappliances_bridge_bridge_init.cpp` | Updated all callers to use new raw pointer APIs |
| `erd_poll_list_builder.h/cpp` | Updated to use raw pointer APIs for feature bit ERDs |
| `geappliances_bridge.h/cpp` | Updated struct fields for new API signatures |

### Test files (7 files changed)
| File | Change |
|------|--------|
| `erd_bridge_subscribe_test.cpp` | Updated to use `erd_set_t` direct member access |
| `erd_bridge_poll_test.cpp` | Updated to use `erd_set_t` direct member access |
| `erd_cache_mqtt_publisher_test.cpp` | Updated `uses_heap` → `uses_pool`, `heap_data` → `pool_data` |
| `feature_bit_manager_test.cpp` | Updated to use raw pointer APIs |
| `erd_poll_list_builder_test.cpp` | Added `config.feature_bit_valid_erds` wiring |
| `esphome_mqtt_client_adapter_test.cpp` | Updated for new API signatures |
| Simulation tests (3 files) | Fixed `extern "C"` linkage for tiny headers |

## Verification
- **All 248 unit tests pass** with 0 failures, 0 ignored, 0 filtered out.
- **No heap allocations** remain in the critical paths: bridge ERD sets, cache data buffers, or feature bit lists.
- **Expected fragmentation reduction**: Elimin
  - `gea-esphome-haieridu`: 24.2% → estimated <8% (eliminates ~200+ `new`/`delete` cycles per poll cycle)
  - `gea-esphome-refer`: 22.9% → estimated <8%
  - `gea-esphome-combi`: 6.9% → estimated <3% (already low due to smaller ERD set)

