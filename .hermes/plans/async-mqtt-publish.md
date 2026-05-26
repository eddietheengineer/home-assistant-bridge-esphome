# Plan: Async MQTT Publish via FreeRTOS Task

## Problem

ESPHome loop times spike to 700-1400ms on polling devices (combi, haieroduc6, refer) due to synchronous `mqtt_client->publish()` calls blocking on the IDF MQTT API mutex. Three call sites block the main loop:

1. **`esphome_mqtt_client_adapter_notify_connected()`** (line 318-323 of `esphome_mqtt_client_adapter.cpp`): Flushes up to `MAX_FLUSH_PER_CALL=5` pending ERD updates per loop iteration. Each `publish()` acquires the IDF MQTT mutex.

2. **`ha_discovery_manager::publish_next_entity_()`** (line 225 of `ha_discovery_manager.cpp`): Publishes one HA discovery entity per call during the initial discovery phase. Rate-limited to 50ms intervals.

3. **`update_erd_write_result()`** (line 146 of `esphome_mqtt_client_adapter.cpp`): Publishes write results directly (bypasses pending queue). Rare path but still blocks.

The IDF MQTT `publish()` call acquires an internal mutex and blocks until the MQTT task accepts the packet into its outbox. When the outbox is full (slow broker, high message volume, or many retain messages), this blocks for hundreds of milliseconds to seconds.

## Solution

Move all MQTT publishing to a dedicated FreeRTOS task that runs independently of the ESPHome main loop. The main loop only enqueues messages; the background task drains the queue and calls `mqtt_client->publish()` at its own pace.

### Architecture

```
Main Loop (ESPHome)                    Background Task (FreeRTOS)
─────────────────────────               ──────────────────────────
update_erd()                           mqtt_publish_task_()
  └─> enqueue to publish_queue_       ┌─> xQueueReceive() (blocks)
                                       │
                                       ├─> mqtt_client->publish() (blocks OK here)
                                       │
                                       └─> loop
```

### Changes

#### 1. `esphome_mqtt_client_adapter.h` — Add FreeRTOS queue and task fields

Add to `esphome_mqtt_client_adapter_t` struct (under `#ifdef USE_ESP_IDF`):

```c
#ifdef USE_ESP_IDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#endif

// In the struct:
#ifdef USE_ESP_IDF
  QueueHandle_t publish_queue_;       // Queue of publish requests
  TaskHandle_t  publish_task_;        // Background publish task
  StackType_t*  publish_task_stack_;  // Statically allocated stack
  StaticTask_t* publish_task_tcb_;    // Statically allocated TCB
#endif
```

#### 2. `esphome_mqtt_client_adapter.cpp` — Create background task and enqueue publishes

**New types and constants:**

```c
static constexpr size_t MQTT_PUBLISH_QUEUE_SIZE = 200;  // Max pending publishes
static constexpr uint32_t MQTT_PUBLISH_STACK_SIZE = 1024;  // words (~4 KB)

struct MqttPublishRequest {
  std::string topic;
  std::string payload;
  bool retain;
};
```

**Task function** (static, C-compatible) — holds requests when disconnected, self-deletes on shutdown:

```c
static void mqtt_publish_task_(void* param) {
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(param);
  while (true) {
    MqttPublishRequest* req = nullptr;
    xQueueReceive(self->publish_queue_, &req, portMAX_DELAY);
    if (req == nullptr) break;  // Sentinel — exit task

    auto mqtt_client = esphome::mqtt::global_mqtt_client;
    if (mqtt_client != nullptr && mqtt_client->is_connected()) {
      mqtt_client->publish(req->topic, req->payload, 0, req->retain);
      delete req;
    } else {
      // Hold and retry after delay — prevents losing retained messages during flaps
      vTaskDelay(pdMS_TO_TICKS(500));
      if (xQueueSend(self->publish_queue_, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "MQTT publish retry queue full, dropping");
        delete req;
      }
    }
  }
  vTaskDelete(nullptr);  // Static task must self-delete
}
```

**Init** — create queue and task in `esphome_mqtt_client_adapter_init()`:

```c
#ifdef USE_ESP_IDF
  self->publish_queue_ = xQueueCreate(MQTT_PUBLISH_QUEUE_SIZE, sizeof(MqttPublishRequest*));
  self->publish_task_stack_ = (StackType_t*)heap_caps_malloc(
    MQTT_PUBLISH_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_INTERNAL);
  self->publish_task_tcb_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
  if (self->publish_queue_ && self->publish_task_stack_ && self->publish_task_tcb_) {
    self->publish_task_ = xTaskCreateStatic(mqtt_publish_task_, "mqtt_pub",
      MQTT_PUBLISH_STACK_SIZE, self, 1, self->publish_task_stack_, self->publish_task_tcb_);
  }
#endif
```

**Destroy** — drain remaining requests, send sentinel, task self-deletes in `esphome_mqtt_client_adapter_destroy()`:

```c
#ifdef USE_ESP_IDF
  if (self->publish_task_ != nullptr) {
    // Drain remaining queued requests (delete heap memory)
    MqttPublishRequest* req = nullptr;
    while (xQueueReceive(self->publish_queue_, &req, 0) == pdTRUE) {
      delete req;
    }
    // Send sentinel so the task exits and self-deletes
    MqttPublishRequest* sentinel = nullptr;
    xQueueSend(self->publish_queue_, &sentinel, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(200));  // Wait for task to self-delete
    self->publish_task_ = nullptr;
  }
  if (self->publish_queue_) vQueueDelete(self->publish_queue_);
  if (self->publish_task_stack_) heap_caps_free(self->publish_task_stack_);
  if (self->publish_task_tcb_)   heap_caps_free(self->publish_task_tcb_);
#endif
```

**Helper** — enqueue a publish request:

```c
static void enqueue_publish(esphome_mqtt_client_adapter_t* self, 
                            const std::string& topic, const std::string& payload, bool retain) {
#ifdef USE_ESP_IDF
  if (self->publish_task_ != nullptr) {
    MqttPublishRequest* req = new MqttPublishRequest{topic, payload, retain};
    if (xQueueSend(self->publish_queue_, &req, 0) != pdTRUE) {
      ESP_LOGW(TAG, "MQTT publish queue full, dropping");
      delete req;
    }
    return;
  }
#endif
  // Fallback: synchronous publish (ESP-ARDUINO or task not ready)
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, payload, 0, retain);
  }
}
```

**Update `update_erd()`** — no change needed. It already queues to `pending_updates` map.

**Update `esphome_mqtt_client_adapter_notify_connected()`** — change flush loop to use `enqueue_publish()`:

```c
// Before:
mqtt_client->publish(it->second.topic, it->second.payload, 0, true);

// After:
enqueue_publish(self, it->second.topic, it->second.payload, true);
```

**Update `update_erd_write_result()`** — change direct publish to `enqueue_publish()`:

```c
// Before:
mqtt_client->publish(topic, payload, 0, false);

// After:
enqueue_publish(self, topic, payload, false);
```

#### 3. `ha_discovery_manager.cpp` — Use async publish for HA discovery entities

**Option A (simpler)**: Keep the existing `publish_next_entity_()` pattern but have it call `enqueue_publish()` through the adapter instead of `mqtt_client->publish()` directly. This requires passing the adapter to the discovery manager.

**Option B (cleaner)**: Have the discovery manager continue using its existing queue/task pattern (which already offloads JSON fetching to a background task), but change `publish_next_entity_()` to enqueue to the MQTT adapter's publish queue instead of calling `mqtt_client->publish()` directly.

I recommend **Option B** — add a method to the adapter:

```c
void esphome_mqtt_client_adapter_publish(
  esphome_mqtt_client_adapter_t* self,
  const std::string& topic, const std::string& payload, bool retain);
```

Then in `ha_discovery_manager.cpp`:

```c
// Before:
mqtt_client->publish(item->topic, item->payload, 0, true);

// After:
esphome_mqtt_client_adapter_publish(&bridge->mqtt_client_adapter_, 
                                     item->topic, item->payload, true);
```

This requires the discovery manager to have access to the adapter. Currently it receives `mqtt::MQTTClientComponent*` — we can add an optional adapter pointer parameter.

#### 4. Notify connected/disconnected lifecycle

- On `notify_disconnected()`: The background task finds `mqtt_client->is_connected()` returns false, holds the request, and retries after 500ms. This prevents silently losing retained HA discovery messages or write-result publishes during connectivity flaps.
- On `notify_connected()`: The background task resumes publishing normally. The `MAX_FLUSH_PER_CALL` drain in `notify_connected()` still controls the rate at which pending ERD updates enter the publish queue.

### Files Modified

1. **`esphome_mqtt_client_adapter.h`**:
   - Add `#include "freertos/..."` under `#ifdef USE_ESP_IDF`
   - Add `MqttPublishRequest` struct
   - Add `publish_queue_`, `publish_task_`, `publish_task_stack_`, `publish_task_tcb_` fields
   - Add `esphome_mqtt_client_adapter_publish()` C API declaration

2. **`esphome_mqtt_client_adapter.cpp`**:
   - Add `mqtt_publish_task_()` static function
   - Add `enqueue_publish()` static helper
   - Modify `esphome_mqtt_client_adapter_init()` to create queue/task
   - Modify `esphome_mqtt_client_adapter_destroy()` to send sentinel and clean up
   - Modify `esphome_mqtt_client_adapter_notify_connected()` to use `enqueue_publish()`
   - Modify `update_erd_write_result()` to use `enqueue_publish()`
   - Add `esphome_mqtt_client_adapter_publish()` extern C function

3. **`ha_discovery_manager.h`**:
   - Add optional `esphome_mqtt_client_adapter_t*` parameter to `publish_next_entity_()` or add a new publish method

4. **`ha_discovery_manager.cpp`**:
   - Modify `publish_next_entity_()` to use the adapter's async publish instead of `mqtt_client->publish()` directly

5. **`geappliances_bridge.h` / `geappliances_bridge_bridge_init.cpp`**:
   - Pass the adapter pointer to the discovery manager so it can use async publish

### Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Publish queue fills up (200 items) | Drop with warning. ERD updates are idempotent — next poll cycle will re-send. |
| New heap allocations (`new MqttPublishRequest`) | Each request is small (two strings). Queue is bounded at 200. |
| Task stack overflow (~4 KB) | 1024 words (~4 KB) is sufficient for a task that only does `xQueueReceive` + `publish` + `delete`. Monitor with `uxTaskGetStackHighWaterMark`. |
| Non-ESP-IDF builds (ESP-ARDUINO) | `enqueue_publish()` falls back to synchronous `publish()`. No regression. |
| MQTT disconnect while queue has items | Task holds and retries after 500ms delay. Prevents losing retained messages during connectivity flaps. |
| Shutdown: queued items leaked | Destroy drains the queue (deletes all remaining `MqttPublishRequest*`) before sending the sentinel. |
| Shutdown: `vTaskDelete` double-delete | Task calls `vTaskDelete(nullptr)` to self-delete on exit. Destroy path does NOT call `vTaskDelete`. |
| Ordering: HA discovery items mixed with ERD updates | Not a concern — they go to different MQTT topics. HA discovery is one-time at boot. |

### Expected Results

- **Main loop time**: Should drop from 700-1400ms spikes to <50ms consistently (the enqueue is a non-blocking `xQueueSend` with 0 timeout)
- **MQTT publish throughput**: Unchanged — the background task publishes at the same rate, just not on the main loop
- **Heap usage**: ~4KB stack (1024 words × 4 bytes) + queue overhead (~1 KB) = ~5 KB additional. Acceptable on ESP32-C6 (which has 320KB+ PSRAM)
- **ESP32-C3 devices**: Also have sufficient RAM (320KB internal + optional PSRAM)

### Verification

1. `make test` — unit tests pass
2. Compile all 7 configs
3. OTA flash all 7 devices
4. Monitor logs for:
   - No more `mqtt took a long time` warnings
   - No more `geappliances_bridge took a long time` warnings
   - Loop time sensor stays <100ms
   - MQTT verify shows same ERD coverage (no regression)

### Rollback Plan

If the async task causes issues (e.g., publishes not reaching broker), revert to synchronous publishes by:
1. Removing the FreeRTOS task creation
2. Changing `enqueue_publish()` to call `mqtt_client->publish()` directly
3. This is a simple code revert with no data loss risk
