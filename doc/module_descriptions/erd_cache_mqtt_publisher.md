# ERD Cache MQTT Publisher

## Purpose

Scans the shared ERD cache and publishes updated ERDs to MQTT topics with `retain=true`. On ESP-IDF platforms, publishing runs in a FreeRTOS background task to avoid blocking the ESPHome main loop on the IDF MQTT mutex. On non-ESP-IDF platforms, `erd_cache_mqtt_publisher_loop()` is called directly from the main loop.

## Public API

| Function | Description |
|----------|-------------|
| `erd_cache_mqtt_publisher_init(self, cache, mqtt_client, device_id)` | Initialize with cache, MQTT client, and device ID. Subscribes to MQTT connect/disconnect events. |
| `erd_cache_mqtt_publisher_destroy(self)` | Stop the background task, unsubscribe events, free semaphores. |
| `erd_cache_mqtt_publisher_start(self)` | Start the background publishing task (ESP-IDF only; no-op otherwise). |
| `erd_cache_mqtt_publisher_stop(self)` | Stop the background publishing task (ESP-IDF only; no-op otherwise). Clean shutdown via `done_semaphore` handshake, then yields for idle task TCB cleanup. |
| `erd_cache_mqtt_publisher_signal_work(self)` | Signal the background task that there is work to do (ESP-IDF only; no-op otherwise). |
| `erd_cache_mqtt_publisher_loop(self, max_publishes, max_ms)` | Publish up to `max_publishes` updated ERDs within `max_ms` milliseconds. Used on non-ESP-IDF platforms; on ESP-IDF this runs inside the background task. |
| `erd_cache_mqtt_publisher_on_connected(self)` | Called when MQTT broker connects. Sets `mqtt_connected = true`. |
| `erd_cache_mqtt_publisher_on_disconnected(self)` | Called when MQTT broker disconnects. Sets `mqtt_connected = false`, increments `missed_loops`. |
| `erd_cache_mqtt_publisher_set_time_fn(self, get_time_ms)` | Override the time source (defaults to `esphome::millis`). Useful for testing. |
| `erd_cache_mqtt_publisher_get_publish_rate(self)` | Returns the number of ERD publishes in the last 60 seconds, then resets the window. |

## Architecture

### ESP-IDF: Background Task

On ESP-IDF, the publisher runs as a FreeRTOS task (`erd_mqtt_pub`) with:
- **Stack**: 2048 bytes (static allocation via `xTaskCreateStatic`)
- **Priority**: 2
- **Scheduling**: Waits on a binary semaphore (`work_semaphore`) with a 100 ms timeout. The main loop signals work via `erd_cache_mqtt_publisher_signal_work()`.

**Concurrent task safety:**
- `state_mutex` protects shared state (`mqtt_connected`, `cache`, `mqtt_client`, `device_id`, `get_time_ms`) from torn reads during context switches between the background task and the main loop.
- `done_semaphore` provides a clean shutdown handshake: the task gives the semaphore before calling `vTaskDelete`, so `stop()` can wait for true termination.

**Task lifecycle:**
1. `init()` creates three semaphores: `work_semaphore`, `state_mutex`, `done_semaphore`
2. `start()` creates the static task
3. Main loop calls `signal_work()` when cache entries are updated
4. Task wakes, acquires `state_mutex` to check `mqtt_connected` and dependency validity
5. Task drains all available cache updates (no per-loop budget in background task)
6. `stop()` sets `task_running = false`, signals work, waits on `done_semaphore` (1 s timeout), then yields for idle task TCB cleanup (100 ms)
7. `destroy()` calls `stop()`, unsubscribes events, deletes semaphores

### Non-ESP-IDF: Main Loop

On non-ESP-IDF platforms, `erd_cache_mqtt_publisher_loop()` is called directly from the main loop with `max_publishes` and `max_ms` budget parameters.

## Publishing Flow

1. Iterate cache entries with `update_required = true` via `erd_cache_get_next_updated()`
2. `get_next_updated()` skips entries whose `publish_cooldown > 0` (rate limited), keeping `update_required = true` for retry
3. For each eligible entry, determine the data pointer (inline, pool, or heap)
4. Build the MQTT topic: `geappliances/{device_id}/erd/0x{ERD:04x}/value`
5. Convert the binary data to a hex string
6. Publish via `mqtt_client_publish_raw()` with `retain = true`
7. Call `erd_cache_mark_published()` to reload the publish cooldown timer

## MQTT Connect/Disconnect Handling

The publisher subscribes to `mqtt_client_on_mqtt_connect` and `mqtt_client_on_mqtt_disconnect` events:
- On connect: sets `mqtt_connected = true`, signals work to wake the background task
- On disconnect: sets `mqtt_connected = false`, increments `missed_loops`

When `mqtt_connected` is false, the publisher skips publishing and increments `missed_loops` (main loop) or continues the wait loop (background task).

## Pre-allocated Buffers

The ESP-IDF background task uses pre-allocated buffers to avoid stack overflow:
- `task_topic[128]` — MQTT topic string
- `task_hex[512]` — hex payload (max 255 bytes of data = 510 hex chars + null)

## Stats

| Field | Description |
|-------|-------------|
| `total_published` | Total ERD publishes since init |
| `missed_loops` | Loop iterations skipped while MQTT disconnected |
| `publish_count_window` | Publishes in the last 60 s window (reset by `get_publish_rate()`) |

## Dependencies

- `erd_cache.h` — shared ERD cache (read-only iteration)
- `i_mqtt_client.h` — MQTT publish interface and connect/disconnect events
- `i_tiny_event.h` — event subscription for MQTT connect/disconnect
- `freertos/FreeRTOS.h`, `freertos/task.h`, `freertos/semphr.h` — FreeRTOS task and semaphore APIs (ESP-IDF only)

## Key Design Decisions

- **Background task on ESP-IDF**: Publishing runs in a dedicated FreeRTOS task to avoid blocking the ESPHome main loop on the IDF MQTT mutex. This was a critical fix — synchronous publishing during startup (50+ ERDs to flush) caused task watchdog timeouts.
- **Concurrent task safety**: The ESP32-C3 is single-core, but the background task and main loop share state through context switches. `state_mutex` protects shared state from torn reads, and `done_semaphore` ensures safe shutdown.
- **Pre-allocated buffers**: The task uses stack-allocated buffers (`task_topic`, `task_hex`) to avoid heap allocation during publishing.
- **Semaphore-based signaling**: The main loop signals work via `work_semaphore`; the task waits with a 100 ms timeout, so it wakes periodically even without explicit signals.
- **Graceful degradation**: If semaphore creation fails, the task exits immediately. If `state_mutex` creation fails, the task reads shared state without protection (acceptable for single-core or low-contention scenarios).
- **No cache ownership**: The publisher does not own the cache — it only reads from it. Cache lifecycle is managed by `GeappliancesBridge`.
- **No MQTT lifecycle ownership**: The publisher does not manage MQTT connections — it reacts to connect/disconnect events from `EsphomeMqttClientAdapter`.

## Testing

Covered by unit tests in `test/tests/erd_cache_mqtt_publisher_test.cpp` (20+ tests covering initialization, publish flow, MQTT connect/disconnect handling, rate limiting, and publish rate tracking). Also covered by integration tests through the full bridge publishing flow.
