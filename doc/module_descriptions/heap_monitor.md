# HeapMonitor

## Purpose

Manages periodic heap monitoring sensor updates. Publishes free heap, minimum free heap, and heap fragmentation (usage percent) values to ESPHome sensors at a configurable interval (default 60 seconds).

## Public API

| Method | Description |
|--------|-------------|
| `init(free_heap, min_free_heap, fragmentation, update_interval_ms)` | Initialize with sensor references and update interval |
| `run()` | Perform heap sensor update if enough time has elapsed since last update |

## Dependencies

- ESP-IDF `esp_get_free_heap_size()`, `esp_get_minimum_free_heap_size()`
- ESP-IDF `heap_caps_get_total_size()`, `heap_caps_get_free_size()`
- ESPHome `sensor::Sensor` — publish values to Home Assistant

## Key Design Decisions

- **Null sensor tolerance**: Any of the three sensors may be `nullptr` — the manager simply skips publishing for that sensor. This allows users to configure only the sensors they care about.
- **Fragmentation as usage percent**: The "heap fragmentation" sensor reports heap usage percentage (`100 - (free/total * 100)`) rather than a fragmentation index. This gives users a simple metric for memory pressure.
- **ESP32-only**: The implementation is gated on `USE_ESP32` — on other platforms, `run()` is a no-op.
- **Default 60-second interval**: Balances between timely detection of memory issues and minimizing overhead.

## Testing

Covered by integration tests in `test/tests/` on ESP32 platforms. The periodic update logic is tested through the main loop's `heap_monitor_.run()` call.
