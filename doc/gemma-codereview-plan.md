# Gemma Code Review Fix Plan

## Problem Statement
The codebase exhibits systemic heap fragmentation risks on the ESP32 due to the use of dynamic containers (`std::set`, `std::vector`) and frequent small heap allocations for ERD data in the cache. Additionally, there is a logic bug in `erd_cache` regarding update flags during allocation failure.

## Identified Issues
1. **Heap-allocated Sets (High Risk)**: Use of `new std::set<tiny_erd_t>()` in `erd_bridge_subscribe.cpp` and `erd_bridge_poll.cpp`.
2. **Cache Buffer Churn (High Risk)**: Frequent `new (std::nothrow) uint8_t[]` allocations in `erd_cache.cpp` for data > 16 bytes.
3. **Dynamic Feature Lists (Medium Risk)**: Use of `std::set` and `std::vector` in `feature_bit_manager.cpp`.
4. **Cache Update Logic (Medium Risk)**: Incorrect `update_required` flag calculation in `erd_cache.cpp` when heap allocation fails.
5. **MQTT Mutex Jitter (Low Risk)**: Potential jitter in `geappliances_bridge.cpp` due to frequent blocking on the MQTT mutex.

## Proposed Solutions

### Phase 1: Memory Hard (Highest Priority)
- **Replace Dynamic Sets**: Migrate- the `std::set` in `erd_bridge_poll.cpp` and `erd_bridge_subscribe.cpp` with a fixed-capacity array or a bitmask if the ERD range allows.
- **Optimize Feature Lists**: Convert `std::set` and `std::vector` in `feature_bit_manager.cpp` to fixed-capacity arrays. the maximum expected ERD count.
- **Cache Memory Pool**: Implement a small, fixed-block memory pool for ERD data in `erd_cache.cpp` to replace raw `new` calls for buffers between 17 and 64 bytes.

### Phase 2: Logic & Performance Correctness
- **Fix Cache Update Flags**: Reorder the logic in `erd_cache_update` to ensure `update_required` is determined *after* the final data size is confirmed (considering truncation).
- **Reduce MQTT Mutex Contention**: Introduce a timer or a "dirty" flag in `geappliances_bridge.cpp` to call `erd_cache_mqtt_publisher_loop` only when data has actually changed or a maximum interval has passed.

## Verification Plan
- **Unit Tests**: Run `feature_bit_manager_test.cpp` and `erd_cache_mqtt_publisher_test.cpp` to ensure no regressions in functionality.
- **Memory Profiling**: Monitor heap fragmentation on actual hardware duringsimulation after replacing dynamic containers.
- **Regression Testing**: Verify ERD polling and subscription flows still function with fixed-capacity containers.
