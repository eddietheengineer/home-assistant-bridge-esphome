# ESPHome Time Source

## Purpose

Minimal adapter that provides ESPHome's `millis()` as the `i_tiny_time_source` interface for the tiny-gea-api timer group.

## Public API

| Function | Description |
|----------|-------------|
| `esphome_time_source_init()` | Returns a static `i_tiny_time_source_t` instance |

## Dependencies

- `esphome::millis()` — ESPHome's millisecond timer
- `i_tiny_time_source` — interface from tiny-gea-api

## Key Design Decisions

- **Static singleton**: The instance is a file-scope static — no heap allocation, no lifecycle management needed.
- **Trivial implementation**: The entire module is a single function that wraps `millis()` in the `i_tiny_time_source_api_t` interface.

## Testing

Not tested in isolation; the time source is used by the timer group which is exercised through all bridge operations.
