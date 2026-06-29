# ERD Cache MQTT Publisher — Specification

## 1. Overview

### 1.1 Purpose

The ERD Cache MQTT Publisher scans the shared ERD cache for entries marked as needing an update and publishes their values to MQTT topics with `retain=true`. On ESP-IDF platforms, publishing runs in a FreeRTOS background task to avoid blocking the ESPHome main loop on the IDF MQTT mutex. On non-ESP-IDF platforms, `erd_cache_mqtt_publisher_loop()` is called directly from the main loop.

### 1.2 Responsibilities

- Iterate the ERD cache for entries with `update_required=true`
- Build the MQTT topic from device ID and ERD number
- Convert binary ERD data to a hex-encoded string
- Publish via `mqtt_client_publish_raw()` with `retain=true`
- Pause publishing when MQTT is disconnected; resume on reconnect
- Maintain publish statistics (total count, missed loops, per-window count)

### 1.3 Not Responsible For

- Cache lifecycle (owned by `GeappliancesBridge`)
- MQTT connection lifecycle (owned by `EsphomeMqttClientAdapter`)
- Write commands (out of scope)
- Determining which ERDs need updating (set by the polling/subscription bridges)

---

## 2. Initialization

### 2.1 Primary Init

```c
void erd_cache_mqtt_publisher_init(
    erd_cache_mqtt_publisher_t* self,
    erd_cache_t* cache,
    i_mqtt_client_t* mqtt_client,
    const char* device_id);
```

| Parameter | Description |
|-----------|-------------|
| `self` | Opaque publisher struct (caller-allocated, stack or member). |
| `cache` | Shared ERD cache (read-only iteration). |
| `mqtt_client` | MQTT client interface for publishing and event registration. |
| `device_id` | Device ID string used in MQTT topic construction. |

`init()` MUST:
- Zero the struct with `memset`
- Store all parameters
- Set `publish_index = 0`
- Set `mqtt_connected = true` (optimistic — assume connected at init time)
- Set `get_time_ms = esphome::millis`
- On ESP-IDF: create three semaphores (`work_semaphore`, `state_mutex`, `done_semaphore`)
- Subscribe to `mqtt_client_on_mqtt_disconnect` and `mqtt_client_on_mqtt_connect` events
- If `mqtt_client` is NULL, skip event subscription (safe for partial init)

### 2.2 Destroy

```c
void erd_cache_mqtt_publisher_destroy(erd_cache_mqtt_publisher_t* self);
```

`destroy()` MUST:
- Call `erd_cache_mqtt_publisher_stop()` to terminate the background task (ESP-IDF)
- Unsubscribe from `mqtt_client_on_mqtt_disconnect` and `mqtt_client_on_mqtt_connect`
- On ESP-IDF: delete all three semaphores (`work_semaphore`, `state_mutex`, `done_semaphore`)
- Zero the struct with `memset`

If `mqtt_client` is NULL, `destroy()` skips event unsubscription and zeros the struct immediately.

### 2.3 Start

```c
void erd_cache_mqtt_publisher_start(erd_cache_mqtt_publisher_t* self);
```

On ESP-IDF:
- Guard against already-running task (`task_handle != NULL`)
- Guard against failed semaphore creation (`work_semaphore == NULL`)
- Set `task_running = true`
- Create the static task via `xTaskCreateStatic()` with 2048-byte stack, priority 2, name `"erd_mqtt_pub"`
- On task creation failure: log error, set `task_running = false`

On non-ESP-IDF: no-op.

### 2.4 Stop

```c
void erd_cache_mqtt_publisher_stop(erd_cache_mqtt_publisher_t* self);
```

On ESP-IDF:
- Guard against no task (`task_handle == NULL`)
- Set `task_running = false`
- Signal `work_semaphore` to wake the task so it can exit its loop
- Wait for the task to signal completion via `done_semaphore` (1 s timeout)
- If `done_semaphore` is NULL (creation failed): fallback to polling with 10 ms delays, resetting the task watchdog each iteration, for up to 1 s
- Log a warning if the task does not signal completion within 1 s
- After the task has called `vTaskDelete`, yield to the idle task (100 ms via `vTaskDelay`) so it can run `prvCheckTasksWaitingTermination` and unlink the TCB's list items before `destroy()` memsets the struct
- Set `task_handle = NULL`

On non-ESP-IDF: no-op.

### 2.5 Signal Work

```c
void erd_cache_mqtt_publisher_signal_work(erd_cache_mqtt_publisher_t* self);
```

On ESP-IDF: gives `work_semaphore` (non-blocking). Used by the main loop to notify the background task that cache entries have been updated.

On non-ESP-IDF: no-op.

---

## 3. Architecture

### 3.1 ESP-IDF: Background Task

On ESP-IDF, the publisher runs as a FreeRTOS task (`mqtt_publisher_task`) with:

- **Stack**: 2048 bytes, statically allocated via `xTaskCreateStatic` (no heap allocation)
- **Priority**: 2
- **Name**: `"erd_mqtt_pub"`
- **Scheduling**: Waits on `work_semaphore` with a 100 ms timeout. The main loop signals work via `erd_cache_mqtt_publisher_signal_work()`. The timeout ensures the task wakes periodically even without explicit signals.

**Task loop:**
1. Wait on `work_semaphore` (100 ms timeout)
2. Acquire `state_mutex` (100 ms timeout) to read `mqtt_connected` and validate dependency pointers (`cache`, `mqtt_client`, `device_id`, `get_time_ms`)
3. If not connected or dependencies are invalid: loop back to step 1
4. Drain all available cache updates (no per-loop budget in the background task)
5. On `task_running = false`: signal `done_semaphore`, call `vTaskDelete`
**Concurrent task safety:**
- `state_mutex` protects shared state (`mqtt_connected`, `cache`, `mqtt_client`, `device_id`, `get_time_ms`, `total_published`, `publish_count_window`) from torn reads during context switches between the background task and the main loop
- `done_semaphore` provides a clean shutdown handshake: the task gives the semaphore before calling `vTaskDelete`, so `stop()` can wait for true termination before proceeding
- Stats updates (`total_published`, `publish_count_window`) are performed under `state_mutex` with a 10 ms timeout

**Graceful degradation:**
- If `work_semaphore` creation fails: the task exits immediately via `vTaskDelete`
- If `state_mutex` creation fails: the task reads shared state without protection (acceptable for single-core or low-contention scenarios)
- If `done_semaphore` creation fails: `stop()` falls back to polling with delays

### 3.2 Non-ESP-IDF: Main Loop

On non-ESP-IDF platforms, `erd_cache_mqtt_publisher_loop()` is called directly from the main loop with `max_publishes` and `max_ms` budget parameters. No background task, no semaphores, no mutex.

```c
uint16_t erd_cache_mqtt_publisher_loop(
    erd_cache_mqtt_publisher_t* self,
    uint16_t max_publishes,
    uint32_t max_ms);
```

| Parameter | Description |
|-----------|-------------|
| `max_publishes` | Maximum number of ERDs to publish in this call. |
| `max_ms` | Maximum time budget in milliseconds. |

Returns the number of ERDs actually published.

---

## 4. Publishing Flow

Both the background task (ESP-IDF) and the main-loop function (non-ESP-IDF) follow the same core publishing logic:

1. Call `erd_cache_get_next_updated(self->cache, &self->publish_index)` to get the next entry with `update_required=true`. Returns NULL when no more updates are available.
2. Determine the data pointer:
   - If `entry->uses_heap && entry->ext_data != NULL`: use `entry->ext_data`
   - Otherwise: use `entry->inline_data`
3. Build the MQTT topic: `geappliances/{device_id}/erd/0x{ERD:04x}/value`
   - Topic buffer is 128 bytes; if `snprintf` truncates, log a warning and stop publishing
4. Convert the binary data to a hex string (lowercase, no separators)
   - Hex buffer is 512 bytes (max 255 bytes of data = 510 hex chars + null)
5. Publish via `mqtt_client_publish_raw(self->mqtt_client, topic, hex, data_len * 2, true)` — `retain=true`
6. Measure per-publish elapsed time; log a warning if ≥ 1000 ms
7. Update stats: increment `total_published` and `publish_count_window`

**Budget enforcement (main-loop only):**
- The loop respects `max_publishes` (count limit) and `max_ms` (time budget)
- The background task has no per-loop budget — it drains all available updates in one pass

---

## 5. MQTT Connect/Disconnect Handling

The publisher subscribes to `mqtt_client_on_mqtt_connect` and `mqtt_client_on_mqtt_disconnect` events during `init()`.

### 5.1 On Connect

```c
void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self);
```

- Set `mqtt_connected = true` (under `state_mutex` on ESP-IDF)
- Log info: "MQTT reconnected — resuming ERD cache publishing"
- Call `erd_cache_mqtt_publisher_signal_work()` to wake the background task

### 5.2 On Disconnect

```c
void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self);
```

- Set `mqtt_connected = false` (under `state_mutex` on ESP-IDF)
- Log warning: "MQTT disconnected — pausing ERD cache publishing"

### 5.3 Behavior While Disconnected

- **Main-loop mode**: `erd_cache_mqtt_publisher_loop()` returns 0 immediately and increments `missed_loops`
- **Background task**: the task checks `mqtt_connected` each iteration and skips publishing (loops back to wait)
- **No full re-publish after reconnect**: the publisher relies on retained messages already on the broker. Only newly updated cache entries are published after reconnect.

---

## 6. Pre-allocated Buffers

To avoid heap allocation during publishing, the publisher uses fixed-size buffers:

| Buffer | Size | Purpose |
|--------|------|---------|
| `task_topic` | 128 bytes | MQTT topic string for the background task. Also used as a local stack buffer in the main-loop function. |
| `task_hex` | 512 bytes | Hex-encoded payload for the background task. Also used as a local stack buffer in the main-loop function. |

The 512-byte hex buffer supports up to 255 bytes of binary data (255 × 2 = 510 hex characters + null terminator), which matches the maximum `data_size` field width in the cache entry.

---

## 7. Stats

| Field | Type | Description |
|-------|------|-------------|
| `total_published` | `uint32_t` | Total ERD publishes since `init()`. Incremented after each successful publish. |
| `missed_loops` | `uint32_t` | Number of `erd_cache_mqtt_publisher_loop()` calls skipped while MQTT was disconnected. Only incremented in main-loop mode. |
| `publish_count_window` | `uint32_t` | Publishes in the last 60-second window. Reset to 0 by `get_publish_rate()`. |

### 7.1 Publish Rate

```c
uint32_t erd_cache_mqtt_publisher_get_publish_rate(erd_cache_mqtt_publisher_t* self);
```

Returns `publish_count_window` and resets it to 0. On ESP-IDF, the read-and-reset is performed under `state_mutex` (100 ms timeout).

### 7.2 Time Source Override

```c
void erd_cache_mqtt_publisher_set_time_fn(
    erd_cache_mqtt_publisher_t* self,
    uint32_t (*get_time_ms)(void));
```

Override the time source (defaults to `esphome::millis`). Used for testing to control time progression.

---

## 8. Invariants

1. **No cache ownership:** The publisher holds a raw pointer to the cache but never frees or modifies its lifecycle. Cache ownership belongs to `GeappliancesBridge`.
2. **No MQTT lifecycle ownership:** The publisher does not create, connect, or destroy the MQTT client. It reacts to connect/disconnect events from `EsphomeMqttClientAdapter`.
3. **Concurrent task safety via mutex/semaphores:** On ESP-IDF, shared state accessed by both the main loop and the background task is protected by `state_mutex` to prevent torn reads during context switches. Shutdown uses `done_semaphore` for a guaranteed handshake, followed by a yield to the idle task for TCB cleanup.
4. **Optimistic connection state:** `mqtt_connected` is initialized to `true` in `init()`. If the MQTT client is not yet connected at init time, the first event will correct the state.
5. **Publish index is monotonic:** `publish_index` advances through the cache entries via `erd_cache_get_next_updated()`, wrapping around as the cache implementation dictates.
6. **Retain flag is always true:** All publishes use `retain=true` to ensure the broker retains the last known value for each ERD topic.
7. **Clean destroy:** All event subscriptions are unsubscribed and all semaphores are deleted before the struct is zeroed. Null guards prevent crashes on partial init.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| `erd_cache.h` | Shared ERD cache (read-only iteration via `erd_cache_get_next_updated`) |
| `i_mqtt_client.h` | MQTT publish interface (`mqtt_client_publish_raw`) and connect/disconnect event accessors |
| `i_tiny_event.h` | Event subscription for MQTT connect/disconnect (`tiny_event_subscribe`, `tiny_event_unsubscribe`) |
| `esphome/core/hal.h` | Time source (`esphome::millis`) |
| `esphome/core/log.h` | Logging (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`) |
| `freertos/FreeRTOS.h`, `freertos/task.h`, `freertos/semphr.h` | FreeRTOS task and semaphore APIs (ESP-IDF only) |
| `esp_task_wdt.h` | Task watchdog reset (ESP-IDF only, used in `stop()` fallback) |

---

## 10. Known Limitations

1. **Broker restart loses retained messages:** If the MQTT broker restarts and loses its retained message store, the publisher does not re-publish all cached ERDs. Only newly updated entries will be published after reconnect. A full re-publish mechanism would require tracking which ERDs have been published and re-sending them after reconnect.
2. **No publish retry:** If `mqtt_client_publish_raw()` fails (e.g., internal queue full), the entry is not retried. The cache entry remains marked as `update_required=true` and will be picked up on the next iteration.
3. **Single device ID:** The publisher is configured with one device ID at init time. Supporting multiple devices would require multiple publisher instances.
4. **Hex encoding is CPU-intensive:** Converting binary data to hex via `snprintf` per byte is simple but not optimal for large payloads. A lookup table or bit-manipulation approach would be faster.
5. **Background task has no publish budget:** The ESP-IDF background task drains all available updates in one pass. If the cache has many updates, this could block the task for an extended period. The 1000 ms slow-publish warning provides visibility but no enforcement.
6. **Semaphore failure is degraded, not fatal:** If any semaphore creation fails in `init()`, the publisher continues with reduced safety (no mutex protection, no clean shutdown handshake). This is acceptable for the single-core ESP32-C3 target where context switches provide natural serialization, but could lead to data races on dual-core ESP32 variants.

---

## 11. Pause / Resume / First Round Done

The publisher supports temporarily pausing and resuming publishing, with a mechanism to detect when a full cache round has completed after resuming. These functions are primarily used to reduce MQTT queue contention during Home Assistant discovery cleanup.

### 11.1 Pause

```c
void erd_cache_mqtt_publisher_pause(erd_cache_mqtt_publisher_t* self);
```

- Sets `paused = true` and resets `first_round_done = false`
- **Thread-safe on ESP-IDF**: acquires `state_mutex` (100 ms timeout) before modifying state; falls back to unprotected write if mutex creation failed
- **Non-ESP-IDF**: sets `paused = true` and `first_round_done = false` directly (no mutex)
- The `paused` flag is checked by the ESP-IDF background task in `mqtt_publisher_task()`: when `paused` is `true`, the task skips the drain loop and returns to waiting on `work_semaphore`
- The non-ESP-IDF `erd_cache_mqtt_publisher_loop()` does **not** check `paused` — pause/resume has no effect in main-loop mode. This is intentional: the main loop controls its own pacing via `max_publishes` and `max_ms` budgets.

### 11.2 Resume

```c
void erd_cache_mqtt_publisher_resume(erd_cache_mqtt_publisher_t* self);
```

- Sets `paused = false`
- **Thread-safe on ESP-IDF**: acquires `state_mutex` (100 ms timeout) before modifying state; falls back to unprotected write if mutex creation failed
- **On ESP-IDF**: after clearing `paused`, calls `erd_cache_mqtt_publisher_signal_work()` to wake the background task so it can immediately resume draining
- **Non-ESP-IDF**: sets `paused = false` directly (no-op in practice since the main loop doesn't check the flag)
- Does **not** reset `first_round_done` — the flag is set by the publish loop itself when a full round completes

### 11.3 First Round Done

```c
bool erd_cache_mqtt_publisher_first_round_done(erd_cache_mqtt_publisher_t* self);
```

- Returns `true` if the publisher has completed a full cache round since the last `pause()` or `on_connected()` call
- **Thread-safe on ESP-IDF**: acquires `state_mutex` (100 ms timeout) to read `first_round_done`; falls back to unprotected read if mutex creation failed
- **Non-ESP-IDF**: reads `first_round_done` directly

**How `first_round_done` is set:**
- The background task (ESP-IDF) sets `first_round_done = true` after a drain pass where `publish_index` wraps back to 0 (lines 120–122 in implementation)
- The main-loop function (non-ESP-IDF) sets `first_round_done = true` after publishing entries and `publish_index` wraps to 0 (lines 379–381 in implementation)
- `first_round_done` is reset to `false` by `pause()` and by `on_connected()` (MQTT reconnect), ensuring the flag reflects completion relative to the most recent pause or reconnect event

### 11.4 Interaction with the Publish Loop

**ESP-IDF background task (`mqtt_publisher_task`):**
- On each wake (from `work_semaphore` signal or 100 ms timeout), the task acquires `state_mutex` and reads `mqtt_connected`, `paused`, and dependency pointers
- If `paused` is `true`, the task skips the drain loop entirely and returns to waiting on `work_semaphore` — no cache entries are published
- When `resume()` is called, it clears `paused` and signals `work_semaphore`, waking the task to resume immediately
- After resuming, the task drains all available cache entries; when `publish_index` wraps to 0, `first_round_done` is set to `true`

**Non-ESP-IDF main loop (`erd_cache_mqtt_publisher_loop`):**
- The loop checks `mqtt_connected` but does **not** check `paused`
- `pause()` and `resume()` are no-ops in terms of publish gating on non-ESP-IDF platforms
- `first_round_done` is still set by the loop when `publish_index` wraps to 0, so `first_round_done()` remains useful as a completion indicator even in main-loop mode

### 11.5 Use Case: Pausing During HA Discovery

The primary use case for pause/resume is to avoid MQTT config storms during Home Assistant discovery cleanup:

1. Before starting HA discovery cleanup, call `erd_cache_mqtt_publisher_pause()` to stop the background task from publishing cache updates
2. Perform discovery cleanup (which may generate a burst of MQTT config messages)
3. Call `erd_cache_mqtt_publisher_resume()` to restart the background task
4. Optionally poll `erd_cache_mqtt_publisher_first_round_done()` to confirm the publisher has drained all accumulated cache updates before proceeding

This prevents the background task from competing for MQTT queue space with discovery messages, reducing the risk of queue overflow or dropped messages during the transition.

