/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager implementation.
 *
 * Serialized producer-consumer design:
 *   Producer (background task): decompresses embedded JSONL entity definitions,
 *   builds one discovery topic/payload at a time in a shared buffer, then
 *   signals the consumer via a binary semaphore.
 *
 *   Consumer (main loop run()): receives the semaphore, publishes the payload
 *   to MQTT, releases the semaphore, and the producer continues.
 *
 *   Only one payload is in flight at any time — no item pool needed.
 */

#include "ha_discovery_manager.h"
#include "ha_discovery_data.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#ifdef USE_ESP_IDF
#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#ifndef USE_ESP_IDF_STUBS
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_STDIO
#include "miniz.h"
#endif
#endif /* USE_ESP_IDF */

static const char* const TAG = "ha_discovery";

/* ------------------------------------------------------------------ */
/* Cleanup: discover and remove old HA discovery topics               */
/* ------------------------------------------------------------------ */

/* HA component types to iterate through during cleanup.
 * Subscribing to all at once (homeassistant/+/{device_id}/#) floods the
 * ESP-IDF MQTT inbound queue, causing dropped events. Instead, we
 * subscribe to one component type at a time, wait for idle, clear,
 * then move to the next. */
static const char* const HA_DISCOVERY_COMPONENT_TYPES[] = {
    "alarm_control_panel",
    "binary_sensor",
    "button",
    "camera",
    "climate",
    "cover",
    "date",
    "datetime",
    "event",
    "fan",
    "light",
    "lock",
    "number",
    "select",
    "sensor",
    "switch",
    "text",
    "time",
    "update",
    "vacuum",
    "valve",
    NULL  /* sentinel */
};

/* ------------------------------------------------------------------ */
/* Cleanup: discover and remove old HA discovery topics               */
/* ------------------------------------------------------------------ */

/* Idle timeout after last topic for current component type. */
#define HA_DISCOVERY_CLEANUP_IDLE_TIMEOUT_MS 2000
/* Short timeout for component types with no topics — skip quickly. */
#define HA_DISCOVERY_CLEANUP_IDLE_TIMEOUT_EMPTY_MS 500
/* Wait after a clean pass before starting discovery publishing. */
#define HA_DISCOVERY_CLEANUP_FINAL_WAIT_MS 5000

/* Flush queued cleanup topics: publish empty retained payloads to remove them.
 * Called from cleanup_run() during idle periods, not from the MQTT callback,
 * to avoid blocking the ESP-IDF MQTT task. Returns the number of topics
 * remaining in the queue (0 means all flushed). */
static uint16_t cleanup_flush_queue(ha_discovery_manager_t* self)
{
    uint16_t batch = 0;
    const uint16_t max_batch = 8;

    while (self->cleanup_queue_count > 0 && batch < max_batch) {
        /* Pop from the front of the queue by shifting. */
        mqtt_client_publish_raw(self->mqtt_client,
            self->cleanup_topic_queue[0], "", 0, true);

        ESP_LOGD(TAG, "Removed old topic: %s", self->cleanup_topic_queue[0]);
        self->cleanup_pass_removed_count++;
        self->cleanup_component_removed_count++;

        /* Shift remaining entries down. */
        for (uint16_t i = 1; i < self->cleanup_queue_count; i++) {
            memcpy(self->cleanup_topic_queue[i - 1], self->cleanup_topic_queue[i],
                   sizeof(self->cleanup_topic_queue[0]));
        }
        self->cleanup_queue_count--;
        batch++;

        /* Yield between batches to let the MQTT task process inbound messages. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return self->cleanup_queue_count;
}

/* Callback for homeassistant/{component}/{device_id}/# subscription during cleanup.
 * Queues the topic name for batched publishing from the main loop. This keeps
 * the callback short — no outbound publish call — so the MQTT task's inbound
 * queue drains fast and retained message bursts don't overflow. */
static void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg)
{
    ha_discovery_manager_t* self = (ha_discovery_manager_t*)arg;

    /* Only remove config topics. */
    size_t topic_len = strlen(topic);
    if (topic_len < 7) return;
    if (strcmp(topic + topic_len - 7, "/config") != 0) return;

    /* If the payload is empty, it's our own echo from a previous clear — skip. */
    if (payload_len == 0) return;

    /* Queue the topic name for publishing from the main loop. */
    if (self->cleanup_queue_count < HA_DISCOVERY_CLEANUP_QUEUE_SIZE) {
        strncpy(self->cleanup_topic_queue[self->cleanup_queue_count], topic, 127);
        self->cleanup_topic_queue[self->cleanup_queue_count][127] = '\0';
        self->cleanup_queue_count++;
    }

    self->cleanup_received_topics = true;
    self->cleanup_pass_found_topics = true;

    /* Record activity time so the idle timer resets. */
    self->cleanup_last_activity_ms = self->get_time_ms();
}

static void cleanup_start(ha_discovery_manager_t* self)
{
    self->cleanup_subscribed = false;
    self->cleanup_current_component = 0;
    self->cleanup_last_activity_ms = self->get_time_ms();
    self->cleanup_queue_count = 0;
    self->cleanup_received_topics = false;
    self->cleanup_clean_passes = 0;
    self->cleanup_pass_found_topics = false;
    self->cleanup_pass_removed_count = 0;
    self->cleanup_component_removed_count = 0;
    self->cleanup_pass_number = 1;
    self->cleanup_wait_start_ms = 0;
    self->cleanup_component_skip = 0;

    ESP_LOGI(TAG, "Starting HA discovery cleanup...");
}

static void cleanup_run(ha_discovery_manager_t* self)
{
    /* Find the next component type to clean. */
    while (self->cleanup_current_component < sizeof(HA_DISCOVERY_COMPONENT_TYPES) / sizeof(HA_DISCOVERY_COMPONENT_TYPES[0])) {
        const char* component = HA_DISCOVERY_COMPONENT_TYPES[self->cleanup_current_component];

        /* NULL sentinel means we've processed all types. */
        if (component == NULL) break;
        /* Skip components that had 0 removals on a previous pass.
         * Once a component has no retained topics, it won't magically
         * have some later — the bitmap persists across passes. */
        if (self->cleanup_component_skip & (1u << self->cleanup_current_component)) {
            self->cleanup_current_component++;
            continue;
        }

        /* Flush any queued topics before subscribing to the next component. */
        cleanup_flush_queue(self);

        /* Subscribe to this component type for our device.
         * homeassistant/{component}/{device_id}/# scopes to one component
         * type at a time, avoiding inbound queue overflow. */
        if (!self->cleanup_subscribed) {
            if (self->mqtt_client) {
                char sub_topic[128];
                snprintf(sub_topic, sizeof(sub_topic), "homeassistant/%s/%s/#", component, self->device_id);
                mqtt_client_subscribe(self->mqtt_client, sub_topic,
                    cleanup_topic_callback, self);
                self->cleanup_subscribed = true;
                self->cleanup_last_activity_ms = self->get_time_ms();
                ESP_LOGI(TAG, "  [%u/%u] Subscribing to %s (pass %u)",
                    self->cleanup_current_component + 1,
                    sizeof(HA_DISCOVERY_COMPONENT_TYPES) / sizeof(HA_DISCOVERY_COMPONENT_TYPES[0]) - 1,
                    component, self->cleanup_pass_number);
            }
            return;
        }

        /* Check if we've been idle long enough — no new matching topics
         * have arrived, so the broker has delivered all retained messages
         * for this component type. Use short timeout for empty components. */
        uint32_t now = self->get_time_ms();
        uint32_t timeout = self->cleanup_received_topics
            ? HA_DISCOVERY_CLEANUP_IDLE_TIMEOUT_MS
            : HA_DISCOVERY_CLEANUP_IDLE_TIMEOUT_EMPTY_MS;
        if (now - self->cleanup_last_activity_ms >= timeout) {
            /* Flush any remaining queued topics before unsubscribing. */
            cleanup_flush_queue(self);

            /* Unsubscribe from current component type. */
            if (self->mqtt_client) {
                char sub_topic[128];
                snprintf(sub_topic, sizeof(sub_topic), "homeassistant/%s/%s/#", component, self->device_id);
                mqtt_client_unsubscribe(self->mqtt_client, sub_topic);
            }
            self->cleanup_subscribed = false;
            ESP_LOGI(TAG, "  [%u/%u] %s: %u topics removed (pass %u)",
                self->cleanup_current_component + 1,
                sizeof(HA_DISCOVERY_COMPONENT_TYPES) / sizeof(HA_DISCOVERY_COMPONENT_TYPES[0]) - 1,
                component, self->cleanup_component_removed_count, self->cleanup_pass_number);
            if (self->cleanup_component_removed_count == 0) {
                self->cleanup_component_skip |= (1u << self->cleanup_current_component);
            }
            self->cleanup_component_removed_count = 0;
            self->cleanup_current_component++;
            self->cleanup_received_topics = false;

            /* Move to the next component type. */
            continue;
        }

        /* Flush queued topics while waiting for the idle timeout. */
        cleanup_flush_queue(self);

        /* Still receiving messages for this component type. */
        return;
    }

    /* All component types processed for this pass. */
    if (self->cleanup_pass_found_topics) {
        /* Topics were found and cleared — loop again to verify. */
        ESP_LOGI(TAG, "Pass %u: removed %u topics, running another pass...",
            self->cleanup_pass_number, self->cleanup_pass_removed_count);
        self->cleanup_pass_number++;
        self->cleanup_current_component = 0;
        self->cleanup_received_topics = false;
        self->cleanup_pass_found_topics = false;
        self->cleanup_pass_removed_count = 0;
        self->cleanup_component_removed_count = 0;
        return;
    }

    /* Clean pass — no topics found. */
    if (self->cleanup_wait_start_ms != 0) {
        /* Already in the final wait period; skip pass accounting. */
        goto wait_check;
    }

    self->cleanup_clean_passes++;
    ESP_LOGI(TAG, "Cleanup pass %u completed with no topics found", self->cleanup_pass_number);

    if (self->cleanup_clean_passes < 2) {
        /* Run another validation pass to confirm nothing was missed. */
        self->cleanup_pass_number++;
        self->cleanup_current_component = 0;
        self->cleanup_received_topics = false;
        self->cleanup_pass_found_topics = false;
        self->cleanup_component_removed_count = 0;
        return;
    }

    /* Two consecutive clean passes. Start the final wait period. */
    self->cleanup_wait_start_ms = self->get_time_ms();
    ESP_LOGI(TAG, "Waiting %lu seconds before discovery...", (unsigned long)(HA_DISCOVERY_CLEANUP_FINAL_WAIT_MS / 1000));

wait_check:

    /* Check if the final wait period has elapsed. */
    uint32_t now = self->get_time_ms();
    if (now - self->cleanup_wait_start_ms < HA_DISCOVERY_CLEANUP_FINAL_WAIT_MS) {
        /* Still waiting. */
        return;
    }

    /* Final wait complete. Proceed to discovery. */
    ESP_LOGI(TAG, "Cleanup complete, proceeding to discovery");
    {
        size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Heap before discovery: free=%u, largest_block=%u, fragmentation=%.1f%%",
            (unsigned)free_heap, (unsigned)largest_free,
            (free_heap > 0) ? (1.0 - (double)largest_free / free_heap) * 100.0 : 0.0);
    }
    self->cleanup_subscribed = false;
    self->state = ha_discovery_state_discovering;
    self->current_category = 0;
    self->current_chunk = 0;
    self->current_offset = 0;
    self->current_decomp_size = 0;
    self->last_publish_ms = self->get_time_ms();
    ESP_LOGI(TAG, "Starting HA discovery fetch...");
}

/* ------------------------------------------------------------------ */
/* Zero-allocation JSON parser helpers                                */
/* ------------------------------------------------------------------ */

/* Extract a JSON string value for the given key.
 * Returns a pointer to the first character of the value (after opening quote)
 * and sets *out_len to the length (not including closing quote).
 * Returns NULL if key not found or value is not a string. */
static const char* json_get_str(const char* json, const char* key,
                                 const char** out_value, size_t* out_len)
{
    size_t key_len = strlen(key);
    const char* p = json;

    while ((p = strstr(p, "\"")) != NULL) {
        if (strncmp(p + 1, key, key_len) == 0 && p[key_len + 1] == '\"') {
            p = p + key_len + 3;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '"') {
                *out_value = p + 1;
                const char* end = p + 1;
                while (*end && *end != '"') {
                    if (*end == '\\') end++;  /* skip escaped char */
                    end++;
                }
                *out_len = (size_t)(end - *out_value);
                return *out_value;
            } else if (*p == '{' || *p == '[') {
                char open = *p;
                char close = (open == '{') ? '}' : ']';
                *out_value = p;
                int depth = 0;
                const char* end = p;
                while (*end) {
                    if (*end == open) depth++;
                    if (*end == close) {
                        depth--;
                        if (depth == 0) break;
                    }
                    end++;
                }
                *out_len = (size_t)(end - p + 1);
                return *out_value;
            } else {
                *out_value = p;
                const char* end = p;
                while (*end && *end != ',' && *end != '}' && *end != ']' && *end != '\n') {
                    end++;
                }
                *out_len = (size_t)(end - p);
                return *out_value;
            }
        }
        p++;
    }
    return NULL;
}

/* Unescape a JSON string value into out (max out_size bytes including null). */
static void json_unescape(const char* src, size_t src_len, char* out, int out_size)
{
    int i = 0;
    const char* p = src;
    const char* end = src + src_len;
    while (p < end && i < out_size - 1) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case '"':  out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                case '/':  out[i++] = '/'; break;
                case 'n':  out[i++] = '\n'; break;
                case 'r':  out[i++] = '\r'; break;
                case 't':  out[i++] = '\t'; break;
                case 'u':  /* skip \uXXXX */ p += 4; break;
                default:   out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
}

/* Copy a raw JSON string value for embedding in another JSON string.
 * The input is already JSON-escaped (contains \\, \", etc.).
 * Embedding it directly in another JSON string requires no transformation —
 * the escape sequences remain valid.
 * Returns the number of bytes written (excluding null terminator). */
static int json_reescape(const char* src, size_t src_len, char* out, int out_size)
{
    if (src_len >= (size_t)out_size) src_len = (size_t)(out_size - 1);
    memcpy(out, src, src_len);
    out[src_len] = '\0';
    return (int)src_len;
}

/* ------------------------------------------------------------------ */
/* ERD cache lookup (binary search on sorted array)                    */
/* ------------------------------------------------------------------ */

static bool erd_is_registered_sorted(const ha_discovery_manager_t* self, uint16_t erd_id)
{
    uint16_t lo = 0, hi = self->sorted_erds_count;
    while (lo < hi) {
        uint16_t mid = lo + (hi - lo) / 2;
        if (self->sorted_erds[mid] < erd_id) lo = mid + 1;
        else if (self->sorted_erds[mid] > erd_id) hi = mid;
        else return true;
    }
    return false;
}

/* Build sorted ERD array from cache for binary search. */
static void build_sorted_erd_list(ha_discovery_manager_t* self)
{
    self->sorted_erds_count = 0;
    uint16_t iterator = 0;
    while (true) {
        erd_cache_entry_t* entry = erd_cache_get_next_entry(self->cache, &iterator);
        if (!entry) break;
        if (self->sorted_erds_count >= HA_DISCOVERY_MAX_ERDS) break;
        /* Dedup */
        bool already = false;
        for (uint16_t k = 0; k < self->sorted_erds_count; k++) {
            if (self->sorted_erds[k] == entry->erd) { already = true; break; }
        }
        if (!already) {
            self->sorted_erds[self->sorted_erds_count++] = entry->erd;
        }
    }
    /* Insertion sort (small N). */
    for (uint16_t i = 1; i < self->sorted_erds_count; i++) {
        uint16_t key = self->sorted_erds[i];
        uint16_t j = i;
        while (j > 0 && self->sorted_erds[j - 1] > key) {
            self->sorted_erds[j] = self->sorted_erds[j - 1];
            j--;
        }
        self->sorted_erds[j] = key;
    }
}

/* ------------------------------------------------------------------ */
/* Device JSON builder                                                */
/* ------------------------------------------------------------------ */

static void build_device_json(ha_discovery_manager_t* self)
{
    int pos = snprintf(self->device_json_buf, sizeof(self->device_json_buf),
        "{\"identifiers\":[\"%s\"],\"name\":\"", self->device_id);

    /* Escape device_id */
    for (const char* p = self->device_id; *p && pos < (int)sizeof(self->device_json_buf) - 8; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\"");
        else if (c == '\\') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\\");
        else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\u%04x", c);
        else { self->device_json_buf[pos++] = (char)c; }
    }

    if (pos < (int)sizeof(self->device_json_buf) - 64) {
        pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos,
            "\",\"manufacturer\":\"GE Appliances\"");
    }

    if (self->model_number && self->model_number[0] && pos < (int)sizeof(self->device_json_buf) - 128) {
        pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, ",\"model\":\"");
        for (const char* p = self->model_number; *p && pos < (int)sizeof(self->device_json_buf) - 8; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '"') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\"");
            else if (c == '\\') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\\");
            else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\u%04x", c);
            else { self->device_json_buf[pos++] = (char)c; }
        }
        if (pos < (int)sizeof(self->device_json_buf) - 2) self->device_json_buf[pos++] = '"';
    }

    if (self->serial_number && self->serial_number[0] && pos < (int)sizeof(self->device_json_buf) - 128) {
        pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, ",\"serial_number\":\"");
        for (const char* p = self->serial_number; *p && pos < (int)sizeof(self->device_json_buf) - 8; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '"') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\"");
            else if (c == '\\') pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\\\");
            else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) pos += snprintf(self->device_json_buf + pos, sizeof(self->device_json_buf) - (size_t)pos, "\\u%04x", c);
            else { self->device_json_buf[pos++] = (char)c; }
        }
        if (pos < (int)sizeof(self->device_json_buf) - 2) self->device_json_buf[pos++] = '"';
    }

    if (pos < (int)sizeof(self->device_json_buf) - 2) self->device_json_buf[pos++] = '}';
    self->device_json_buf[pos] = '\0';
}

/* ------------------------------------------------------------------ */
/* Decompression helper                                               */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static int chunk_decompress(ha_discovery_manager_t* self, const uint8_t* compressed, size_t compressed_len,
                           uint8_t* output, size_t* output_len)
{
#ifdef USE_ESP_IDF_STUBS
    (void)compressed; (void)compressed_len; (void)output; (void)output_len;
    return -1;
#else
    tinfl_init(&self->decomp_state);

    size_t src_size = compressed_len;
    size_t dst_size = *output_len;

    tinfl_status status = tinfl_decompress(
        &self->decomp_state,
        compressed, &src_size,
        output, output, &dst_size,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    if (status != TINFL_STATUS_DONE) {
        return -1;
    }

    *output_len = dst_size;
    return 0;
#endif
}
#endif /* USE_ESP_IDF */

/* ------------------------------------------------------------------ */
/* Process a single JSONL line: build topic/payload in shared buffers */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static bool process_jsonl_line(ha_discovery_manager_t* self, const char* line)
{
    const char* val = NULL;
    size_t len = 0;

    /* Required fields */
    if (!json_get_str(line, "i", &val, &len)) return false;
    if (len >= sizeof(self->erd_id_hex_buf)) len = sizeof(self->erd_id_hex_buf) - 1;
    memcpy(self->erd_id_hex_buf, val, len);
    self->erd_id_hex_buf[len] = '\0';
    const char* erd_id_hex = self->erd_id_hex_buf;

    if (!json_get_str(line, "n", &val, &len)) return false;
    json_unescape(val, len, self->entity_name_buf, sizeof(self->entity_name_buf));

    if (!json_get_str(line, "d", &val, &len)) return false;
    json_unescape(val, len, self->domain_buf, sizeof(self->domain_buf));

    /* Optional fields — use struct buffers to avoid stack overflow. */
    self->field_id_buf[0] = '\0';
    self->paired_erd_buf[0] = '\0';
    self->role_buf[0] = '\0';
    self->unit_buf[0] = '\0';
    self->device_class_buf[0] = '\0';
    self->state_class_buf[0] = '\0';
    self->options_buf[0] = '\0';
    self->data_type_buf[0] = '\0';
    self->mode_buf[0] = '\0';
    self->payload_on_buf[0] = '\0';
    self->payload_off_buf[0] = '\0';
    self->state_on_buf[0] = '\0';
    self->state_off_buf[0] = '\0';
    self->min_buf[0] = '\0';
    self->max_buf[0] = '\0';
    self->step_buf[0] = '\0';

    if (json_get_str(line, "fi", &val, &len)) json_unescape(val, len, self->field_id_buf, sizeof(self->field_id_buf));
    if (json_get_str(line, "p", &val, &len)) json_unescape(val, len, self->paired_erd_buf, sizeof(self->paired_erd_buf));
    if (json_get_str(line, "r", &val, &len)) json_unescape(val, len, self->role_buf, sizeof(self->role_buf));
    if (json_get_str(line, "u", &val, &len)) json_unescape(val, len, self->unit_buf, sizeof(self->unit_buf));
    if (json_get_str(line, "dc", &val, &len)) json_unescape(val, len, self->device_class_buf, sizeof(self->device_class_buf));
    if (json_get_str(line, "sc", &val, &len)) json_unescape(val, len, self->state_class_buf, sizeof(self->state_class_buf));
    if (json_get_str(line, "o", &val, &len)) json_unescape(val, len, self->options_buf, sizeof(self->options_buf));
    if (json_get_str(line, "dt", &val, &len)) json_unescape(val, len, self->data_type_buf, sizeof(self->data_type_buf));
    if (json_get_str(line, "sf", &val, &len)) json_unescape(val, len, self->scale_factor_buf, sizeof(self->scale_factor_buf));
    if (json_get_str(line, "m", &val, &len)) json_unescape(val, len, self->mode_buf, sizeof(self->mode_buf));
    if (json_get_str(line, "pon", &val, &len)) json_unescape(val, len, self->payload_on_buf, sizeof(self->payload_on_buf));
    if (json_get_str(line, "poff", &val, &len)) json_unescape(val, len, self->payload_off_buf, sizeof(self->payload_off_buf));
    if (json_get_str(line, "son", &val, &len)) json_unescape(val, len, self->state_on_buf, sizeof(self->state_on_buf));
    if (json_get_str(line, "soff", &val, &len)) json_unescape(val, len, self->state_off_buf, sizeof(self->state_off_buf));
    if (json_get_str(line, "mn", &val, &len)) json_unescape(val, len, self->min_buf, sizeof(self->min_buf));
    if (json_get_str(line, "mx", &val, &len)) json_unescape(val, len, self->max_buf, sizeof(self->max_buf));
    if (json_get_str(line, "st", &val, &len)) json_unescape(val, len, self->step_buf, sizeof(self->step_buf));

    uint16_t erd_id = (uint16_t)strtoul(erd_id_hex, NULL, 16);

    /* Check if ERD is registered (binary search). */
    if (!erd_is_registered_sorted(self, erd_id)) {
        self->total_filtered++;
        return false;
    }

    /* Check paired ERD if present. */
    if (self->paired_erd_buf[0]) {
        uint16_t paired_id = (uint16_t)strtoul(self->paired_erd_buf, NULL, 16);
        if (!erd_is_registered_sorted(self, paired_id)) {
            self->total_filtered++;
            return false;
        }
    }

    /* Build unique_id */
    if (self->field_id_buf[0]) {
        snprintf(self->unique_id_buf, sizeof(self->unique_id_buf), "%s_erd_%s_%s", self->device_id, erd_id_hex, self->field_id_buf);
    } else {
        snprintf(self->unique_id_buf, sizeof(self->unique_id_buf), "%s_erd_%s", self->device_id, erd_id_hex);
    }

    /* Build state_topic and command_topic */
    snprintf(self->state_topic_buf, sizeof(self->state_topic_buf), "geappliances/%s/erd/0x%s/value", self->device_id, erd_id_hex);
    snprintf(self->command_topic_buf, sizeof(self->command_topic_buf), "geappliances/%s/erd/0x%s/write", self->device_id, erd_id_hex);

    /* For paired entities, swap state/command topics */
    if (self->paired_erd_buf[0]) {
        if (self->role_buf[0] && strcmp(self->role_buf, "request") == 0) {
            snprintf(self->actual_command_topic_buf, sizeof(self->actual_command_topic_buf), "geappliances/%s/erd/0x%s/write", self->device_id, erd_id_hex);
            snprintf(self->actual_state_topic_buf, sizeof(self->actual_state_topic_buf), "geappliances/%s/erd/0x%s/value", self->device_id, self->paired_erd_buf);
        } else {
            snprintf(self->actual_state_topic_buf, sizeof(self->actual_state_topic_buf), "geappliances/%s/erd/0x%s/value", self->device_id, erd_id_hex);
            snprintf(self->actual_command_topic_buf, sizeof(self->actual_command_topic_buf), "geappliances/%s/erd/0x%s/write", self->device_id, self->paired_erd_buf);
        }
    } else {
        strncpy(self->actual_state_topic_buf, self->state_topic_buf, sizeof(self->actual_state_topic_buf));
        strncpy(self->actual_command_topic_buf, self->command_topic_buf, sizeof(self->actual_command_topic_buf));
    }

    /* Build topic */
    if (self->field_id_buf[0]) {
        snprintf(self->topic_buf, sizeof(self->topic_buf), "homeassistant/%s/%s/%s_%s/config", self->domain_buf, self->device_id, erd_id_hex, self->field_id_buf);
    } else {
        snprintf(self->topic_buf, sizeof(self->topic_buf), "homeassistant/%s/%s/%s/config", self->domain_buf, self->device_id, erd_id_hex);
    }

    /* Build payload directly in shared buffer.
     * Templates are embedded directly from the raw JSONL line with re-escaping,
     * avoiding intermediate buffer limits. */
    char* payload = self->payload_buf;
    int pos = 0;
    int space = (int)sizeof(self->payload_buf) - 1;  /* leave room for null */

    int n;

    /* Button domain: simpler payload, no state_topic/value_template. */
    if (strcmp(self->domain_buf, "button") == 0) {
        n = snprintf(payload + pos, space,
            "{\"name\":\"%s\",\"unique_id\":\"%s\",\"device\":%s,",
            self->entity_name_buf, self->unique_id_buf, self->device_json_buf);
        if (n < 0 || n >= space) goto too_large;
        pos += n; space -= n;

        n = snprintf(payload + pos, space,
            "\"command_topic\":\"%s\",\"payload_press\":\"1\",",
            self->actual_command_topic_buf);
        if (n < 0 || n >= space) goto too_large;
        pos += n; space -= n;

        if (self->device_class_buf[0]) {
            n = snprintf(payload + pos, space, "\"device_class\":\"%s\",", self->device_class_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
    } else {
        /* Non-button domains: sensor, binary_sensor, switch, select, number, etc. */
        n = snprintf(payload + pos, space,
            "{\"name\":\"%s\",\"unique_id\":\"%s\",\"device\":%s,",
            self->entity_name_buf, self->unique_id_buf, self->device_json_buf);
        if (n < 0 || n >= space) goto too_large;
        pos += n; space -= n;

        n = snprintf(payload + pos, space,
            "\"state_topic\":\"%s\",", self->actual_state_topic_buf);
        if (n < 0 || n >= space) goto too_large;
        pos += n; space -= n;

        /* Embed value_template directly from raw JSONL with re-escaping. */
        if (json_get_str(line, "vt", &val, &len)) {
            n = snprintf(payload + pos, space, "\"value_template\":\"");
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
            int reescaped = json_reescape(val, len, payload + pos, space);
            if (reescaped >= space) goto too_large;
            pos += reescaped; space -= reescaped;
            n = snprintf(payload + pos, space, "\",");
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }

        /* Embed command_template directly from raw JSONL with re-escaping. */
        if (json_get_str(line, "ct", &val, &len)) {
            n = snprintf(payload + pos, space, "\"command_topic\":\"%s\",\"command_template\":\"", self->actual_command_topic_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
            int reescaped = json_reescape(val, len, payload + pos, space);
            if (reescaped >= space) goto too_large;
            pos += reescaped; space -= reescaped;
            n = snprintf(payload + pos, space, "\",");
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        } else if (self->paired_erd_buf[0]) {
            n = snprintf(payload + pos, space, "\"command_topic\":\"%s\",", self->actual_command_topic_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }

        if (self->unit_buf[0]) {
            n = snprintf(payload + pos, space, "\"unit_of_measurement\":\"%s\",", self->unit_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->device_class_buf[0]) {
            n = snprintf(payload + pos, space, "\"device_class\":\"%s\",", self->device_class_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->state_class_buf[0]) {
            n = snprintf(payload + pos, space, "\"state_class\":\"%s\",", self->state_class_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->options_buf[0]) {
            n = snprintf(payload + pos, space, "\"options\":%s,", self->options_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        /* Number domain: use mn/mx/st from JSONL (scaled values). */
        if (strcmp(self->domain_buf, "number") == 0) {
            if (self->min_buf[0]) {
                n = snprintf(payload + pos, space, "\"min\":%s,", self->min_buf);
                if (n < 0 || n >= space) goto too_large;
                pos += n; space -= n;
            }
            if (self->max_buf[0]) {
                n = snprintf(payload + pos, space, "\"max\":%s,", self->max_buf);
                if (n < 0 || n >= space) goto too_large;
                pos += n; space -= n;
            }
            if (self->step_buf[0]) {
                n = snprintf(payload + pos, space, "\"step\":%s,", self->step_buf);
                if (n < 0 || n >= space) goto too_large;
                pos += n; space -= n;
            }
            /* Fallback to dt-based ranges if mn/mx not set. */
            if (!self->min_buf[0] && self->data_type_buf[0]) {
                if (strcmp(self->data_type_buf, "u8") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":0,\"max\":255,");
                } else if (strcmp(self->data_type_buf, "i8") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":-128,\"max\":127,");
                } else if (strcmp(self->data_type_buf, "u16") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":0,\"max\":65535,");
                } else if (strcmp(self->data_type_buf, "i16") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":-32768,\"max\":32767,");
                } else if (strcmp(self->data_type_buf, "u32") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":0,\"max\":4294967295,");
                } else if (strcmp(self->data_type_buf, "i32") == 0) {
                    n = snprintf(payload + pos, space, "\"min\":-2147483648,\"max\":2147483647,");
                }
                if (n < 0 || n >= space) goto too_large;
                pos += n; space -= n;
            }
        }
        /* Fallback step from scale_factor if st not set (for non-number domains). */
        if (self->scale_factor_buf[0] && !self->step_buf[0] && strcmp(self->domain_buf, "number") == 0) {
            n = snprintf(payload + pos, space, "\"step\":%s,", self->scale_factor_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->mode_buf[0]) {
            n = snprintf(payload + pos, space, "\"mode\":\"%s\",", self->mode_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->payload_on_buf[0]) {
            n = snprintf(payload + pos, space, "\"payload_on\":\"%s\",", self->payload_on_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->payload_off_buf[0]) {
            n = snprintf(payload + pos, space, "\"payload_off\":\"%s\",", self->payload_off_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->state_on_buf[0]) {
            n = snprintf(payload + pos, space, "\"state_on\":\"%s\",", self->state_on_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
        if (self->state_off_buf[0]) {
            n = snprintf(payload + pos, space, "\"state_off\":\"%s\",", self->state_off_buf);
            if (n < 0 || n >= space) goto too_large;
            pos += n; space -= n;
        }
    }

    /* Remove trailing comma and close */
    if (pos > 0 && payload[pos - 1] == ',') {
        payload[pos - 1] = '\0';
        pos--; space++;
    }
    n = snprintf(payload + pos, space, "}");
    if (n < 0 || n >= space) goto too_large;
    pos += n;
    payload[pos] = '\0';

    return true;

too_large:
    ESP_LOGW(TAG, "Payload too large for ERD 0x%s, skipping", erd_id_hex);
    self->total_filtered++;
    return false;
}
#endif

/* ------------------------------------------------------------------ */
/* Category filtering by appliance type                               */
/* ------------------------------------------------------------------ */

static bool should_process_category(const char* category, uint8_t appliance_type)
{
    if (strcmp(category, "common") == 0) return true;

    /* Appliance type enum (ERD 0x0008):
     * 0=WaterHeater, 1=ClothesDryer, 2=ClothesWasher, 3=Refrigerator,
     * 4=Microwave, 5=Advantium, 6=Dishwasher, 7=Oven, 8=ElectricRange,
     * 9=GasRange, 10=ThermostatRAC, 11=ElectricCooktop, 12=PizzaOven,
     * 13=GasCooktop, 14=SplitDFSDuctFreeSplitAC, 15=Hood,
     * 16=PointOfEntryWaterFilter, 17=InductionCooktop, 18=DeliveryBox,
     * 19=KitchenHubVentHood, 20=ZonelinePTAC, 21=WaterSoftener,
     * 22=PortableAC, 23=CombinationWasherDryer, 24=DualZoneWineChiller,
     * 25=BeverageCenter, 26=CoffeeBrewer, 27=OpalNuggetIceMaker,
     * 28=InHomeGrower, 29=Dehumidifer, 30=UnderCounterIceMaker,
     * 31=ThroughWallAC, 32=FPDishDrawer, 33=EspressoCoffeeMaker,
     * 34=ToasterOven, 35=ZonelineVertical, 36=CentralDFSDuctFreeSplitController,
     * 37=BLEMeshGateway, 38=StandMixer, 39=FPCooktop,
     * 40=FPCooktopTeppanyaki, 41=FPVentilationDowndraft, 42=SmartPlug,
     * 43=Smoker, 44=AirHandlerVRF, 45=FabricCareCabinetCloset,
     * 46=LaundryCenter, 47=Grill, 48=Freezer, 49=WarmingDrawer,
     * 50=VacuumSealDrawer, 51=WineCabinet, 52=CentralAC, 53=SoftStarter,
     * 54=HearthPizzaOven, 55=SourdoughStarter, 56=Thermostat
     */

    /* Dishwasher: 6=Dishwasher, 32=FPDishDrawer */
    if (appliance_type == 6 || appliance_type == 32) {
        if (strcmp(category, "dishwasher") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Refrigeration: 3=Refrigerator, 24=DualZoneWineChiller,
     * 25=BeverageCenter, 48=Freezer, 51=WineCabinet */
    if (appliance_type == 3 || appliance_type == 24 ||
        appliance_type == 25 || appliance_type == 48 ||
        appliance_type == 51) {
        if (strcmp(category, "refrigeration") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Laundry: 1=ClothesDryer, 2=ClothesWasher, 23=CombinationWasherDryer,
     * 45=FabricCareCabinetCloset, 46=LaundryCenter */
    if (appliance_type == 1 || appliance_type == 2 ||
        appliance_type == 23 || appliance_type == 45 ||
        appliance_type == 46) {
        if (strcmp(category, "laundry") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Range/Cooking: 4=Microwave, 5=Advantium, 7=Oven, 8=ElectricRange,
     * 9=GasRange, 11=ElectricCooktop, 12=PizzaOven, 13=GasCooktop,
     * 15=Hood, 17=InductionCooktop, 19=KitchenHubVentHood,
     * 34=ToasterOven, 39=FPCooktop, 40=FPCooktopTeppanyaki,
     * 41=FPVentilationDowndraft, 43=Smoker, 47=Grill,
     * 49=WarmingDrawer, 54=HearthPizzaOven */
    if (appliance_type == 4 || appliance_type == 5 ||
        appliance_type == 7 || appliance_type == 8 ||
        appliance_type == 9 || appliance_type == 11 ||
        appliance_type == 12 || appliance_type == 13 ||
        appliance_type == 15 || appliance_type == 17 ||
        appliance_type == 19 || appliance_type == 34 ||
        appliance_type == 39 || appliance_type == 40 ||
        appliance_type == 41 || appliance_type == 43 ||
        appliance_type == 47 || appliance_type == 49 ||
        appliance_type == 54) {
        if (strcmp(category, "range") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Air conditioning: 10=ThermostatRAC, 14=SplitDFSDuctFreeSplitAC,
     * 20=ZonelinePTAC, 22=PortableAC, 30=UnderCounterIceMaker,
     * 31=ThroughWallAC, 35=ZonelineVertical, 36=CentralDFSDuctFreeSplitController,
     * 44=AirHandlerVRF, 52=CentralAC, 56=Thermostat */
    if (appliance_type == 10 || appliance_type == 14 ||
        appliance_type == 20 || appliance_type == 22 ||
        appliance_type == 30 || appliance_type == 31 ||
        appliance_type == 35 || appliance_type == 36 ||
        appliance_type == 44 || appliance_type == 52 ||
        appliance_type == 56) {
        if (strcmp(category, "airconditioning") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Water heater: 0=WaterHeater */
    if (appliance_type == 0) {
        if (strcmp(category, "waterheater") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Water filter: 16=PointOfEntryWaterFilter, 21=WaterSoftener */
    if (appliance_type == 16 || appliance_type == 21) {
        if (strcmp(category, "waterfilter") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    /* Small appliance: 18=DeliveryBox, 26=CoffeeBrewer, 27=OpalNuggetIceMaker,
     * 28=InHomeGrower, 29=Dehumidifer, 33=EspressoCoffeeMaker,
     * 37=BLEMeshGateway, 38=StandMixer, 50=VacuumSealDrawer,
     * 53=SoftStarter, 55=SourdoughStarter */
    if (appliance_type == 18 || appliance_type == 26 ||
        appliance_type == 27 || appliance_type == 28 ||
        appliance_type == 29 || appliance_type == 33 ||
        appliance_type == 37 || appliance_type == 38 ||
        appliance_type == 42 || appliance_type == 50 ||
        appliance_type == 53 || appliance_type == 55) {
        if (strcmp(category, "smallappliance") == 0) return true;
        if (strcmp(category, "energy") == 0) return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Build task: builds sorted ERD list and device JSON                 */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void build_task(void* arg)
{
    ha_discovery_manager_t* self = (ha_discovery_manager_t*)arg;

    /* Fragmentation baseline: log heap state before build work. */
    {
        size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Heap before build: free=%u, largest_block=%u, fragmentation=%.1f%%",
            (unsigned)free_heap, (unsigned)largest_free,
            (free_heap > 0) ? (1.0 - (double)largest_free / free_heap) * 100.0 : 0.0);
    }

    build_sorted_erd_list(self);
    build_device_json(self);

    /* Stack watermark: verify 2KB stack is sufficient. */
    {
        UBaseType_t hw = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI(TAG, "build_task stack high_watermark: %lu words (%lu bytes)",
            (unsigned long)hw, (unsigned long)(hw * sizeof(StackType_t)));
    }

    if (self->done_sem) {
        xSemaphoreGive(self->done_sem);
    }

    vTaskDelete(NULL);
}
#endif

/* ------------------------------------------------------------------ */
/* Cleanup helper                                                     */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void cleanup_resources(ha_discovery_manager_t* self)
{
    if (self->done_sem) {
        vSemaphoreDelete(self->done_sem);
        self->done_sem = NULL;
    }
    if (self->task_stack) {
        free(self->task_stack);
        self->task_stack = NULL;
    }
    if (self->task_tcb) {
        free(self->task_tcb);
        self->task_tcb = NULL;
    }
    self->task_handle = NULL;
}
#endif

/* ------------------------------------------------------------------ */
/* run(): sequential chunk decompression + publish                    */
/* ------------------------------------------------------------------ */

void ha_discovery_manager_run(ha_discovery_manager_t* self)
{
#ifdef USE_ESP_IDF
    if (self->state != ha_discovery_state_building &&
        self->state != ha_discovery_state_cleaning &&
        self->state != ha_discovery_state_discovering) {
        return;
    }

    /* Wait for build task to finish (first call). */
    if (self->state == ha_discovery_state_building) {
        if (xSemaphoreTake(self->done_sem, 0) == pdTRUE) {
            self->build_done = true;

            /* After vTaskDelete() the TCB is on xTasksWaitingTermination.
             * The idle task runs prvCheckTasksWaitingTermination to unlink
             * the TCB's list items via uxListRemove(). We MUST yield here
             * so the idle task can finish before freeing the TCB — freeing
             * it mid-uxListRemove causes a load access fault. */
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(100));

            /* Free task resources after the idle task has unlinked the TCB.
             * This returns ~3 KB (stack + TCB) to the heap during the
             * cleanup + discovery phases — the period of highest memory
             * pressure. cleanup_resources() at the end will be a no-op
             * since these are set to NULL. */
            free(self->task_stack);
            free(self->task_tcb);
            self->task_stack = NULL;
            self->task_tcb = NULL;
            self->task_handle = NULL;
        } else {
            return;  /* Build not done yet. */
        }
        if (!self->build_done) return;

        /* Heap after build task freed its stack/TCB. */
        {
            size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            ESP_LOGI(TAG, "Heap after build: free=%u, largest_block=%u, fragmentation=%.1f%%",
                (unsigned)free_heap, (unsigned)largest_free,
                (free_heap > 0) ? (1.0 - (double)largest_free / free_heap) * 100.0 : 0.0);
        }

        /* Transition to cleaning. */
        self->state = ha_discovery_state_cleaning;
        cleanup_start(self);
        return;
    }

    /* Cleaning state: collect and remove old discovery topics. */
    if (self->state == ha_discovery_state_cleaning) {
        cleanup_run(self);
        return;
    }

    /* Discovering state: decompress chunks and publish entities. */
    while (self->state == ha_discovery_state_discovering) {
        /* Find the next category to process. */
        while (self->current_category < ha_discovery_category_count) {
            const ha_discovery_category_t* cat = &ha_discovery_categories[self->current_category];

            if (!should_process_category(cat->name, self->appliance_type)) {
                self->current_category++;
                self->current_chunk = 0;
                self->current_offset = 0;
                self->current_decomp_size = 0;
                continue;
            }

            /* Decompress the current chunk if needed. */
            if (self->current_decomp_size == 0) {
                if (self->current_chunk >= cat->num_chunks) {
                    /* Done with this category. */
                    self->current_category++;
                    self->current_chunk = 0;
                    self->current_offset = 0;
                    self->current_decomp_size = 0;
                    continue;
                }

                const ha_discovery_chunk_t* chunk = &cat->chunks[self->current_chunk];
                const uint8_t* src = cat->data + chunk->offset;

                size_t dst_size = sizeof(self->decomp_buf);
                if (chunk_decompress(self, src, chunk->size, self->decomp_buf, &dst_size) != 0) {
                    /* Decompression failed, skip this chunk. */
                    self->current_chunk++;
                    self->current_offset = 0;
                    self->current_decomp_size = 0;
                    continue;
                }
                self->current_decomp_size = (uint32_t)dst_size;
                self->current_offset = 0;
            }

            /* Parse lines from the current decompressed chunk. */
            const char* decomp = (const char*)self->decomp_buf;

            while (self->current_offset < self->current_decomp_size) {
                /* Find the next line. */
                const char* line_start = decomp + self->current_offset;
                const char* line_end = line_start;
                while ((uintptr_t)(line_end - decomp) < self->current_decomp_size &&
                       *line_end != '\n' && *line_end != '\r') {
                    line_end++;
                }

                size_t line_len = (size_t)(line_end - line_start);
                if (line_len == 0) {
                    self->current_offset++;
                    continue;
                }
                if (line_len >= sizeof(self->line_buf) - 1) {
                    line_len = sizeof(self->line_buf) - 1;
                }
                memcpy(self->line_buf, line_start, line_len);
                self->line_buf[line_len] = '\0';

                /* Process the line. */
                if (process_jsonl_line(self, self->line_buf)) {
                    /* Rate-limit before publishing. */
                    uint32_t now = self->get_time_ms();
                    if (now - self->last_publish_ms < HA_DISCOVERY_PUBLISH_INTERVAL_MS) {
                        /* Don't advance offset; next run() will retry this line. */
                        return;
                    }
                    self->last_publish_ms = now;

                    /* Publish. */
                    if (self->mqtt_client) {
                        mqtt_client_publish_raw(self->mqtt_client, self->topic_buf,
                            self->payload_buf, strlen(self->payload_buf), true);
                    }
                    self->total_published++;
                    self->total_discovered++;

                    /* Advance offset past this line after successful publish. */
                    self->current_offset = (uint32_t)(line_end - decomp) + 1;

                    ESP_LOGD(TAG, "Published: %s (0x%s)", self->entity_name_buf, self->erd_id_hex_buf);

                    /* Log category progress periodically. */
                    if (self->total_published % 50 == 0) {
                        ESP_LOGI(TAG, "Category %s: %u discovered, %u published",
                            cat->name, self->total_discovered, self->total_published);
                    }
                } else {
                    /* Line was filtered; advance offset past it. */
                    self->current_offset = (uint32_t)(line_end - decomp) + 1;
                }

                /* Yield to other tasks after each entity. */
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(1));
            }

            /* Done with this chunk, move to the next. */
            self->current_chunk++;
            self->current_offset = 0;
            self->current_decomp_size = 0;

            /* Log category completion. */
            uint32_t cat_discovered = self->total_discovered;
            /* We don't track per-category discovered easily, so skip the log. */

            /* Yield after finishing a chunk. */
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(10));

            break;  /* Break inner while to re-evaluate category/chunk state. */
        }

        /* Check if all categories are done. */
        if (self->current_category >= ha_discovery_category_count) {
            cleanup_resources(self);
            self->state = ha_discovery_state_complete;

            /* Fragmentation after discovery: log heap state post-completion. */
            size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            ESP_LOGI(TAG, "HA discovery complete: %u published, %u filtered",
                self->total_published, self->total_filtered);
            ESP_LOGI(TAG, "Heap after discovery: free=%u, largest_block=%u, fragmentation=%.1f%%",
                (unsigned)free_heap, (unsigned)largest_free,
                (free_heap > 0) ? (1.0 - (double)largest_free / free_heap) * 100.0 : 0.0);
            break;
        }
    }
#else
    (void)self;
#endif
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void ha_discovery_manager_init(ha_discovery_manager_t* self)
{
    memset(self, 0, sizeof(*self));
    self->state = ha_discovery_state_idle;
    self->get_time_ms = esphome::millis;

#ifdef USE_ESP_IDF
    self->done_sem = xSemaphoreCreateBinary();
    if (!self->done_sem) {
        ESP_LOGE(TAG, "Failed to create done semaphore");
    }
    self->task_running = false;
#endif
}

void ha_discovery_manager_configure(
    ha_discovery_manager_t* self,
    const char* device_id,
    const char* model_number,
    const char* serial_number,
    uint8_t appliance_type,
    erd_cache_t* cache,
    i_mqtt_client_t* mqtt_client)
{
    self->device_id = device_id;
    self->model_number = model_number;
    self->serial_number = serial_number;
    self->appliance_type = appliance_type;
    self->cache = cache;
    self->mqtt_client = mqtt_client;
}

void ha_discovery_manager_start(ha_discovery_manager_t* self)
{
    if (self->state != ha_discovery_state_idle) return;

#ifdef USE_ESP_IDF
    static constexpr int STACK_SIZE = 4 * 1024;

    self->task_stack = (StackType_t*)heap_caps_malloc(STACK_SIZE, MALLOC_CAP_8BIT);
    self->task_tcb = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_8BIT);

    if (!self->task_stack || !self->task_tcb) {
        ESP_LOGW(TAG, "Skipping HA discovery: unable to allocate task stack/TCB");
        free(self->task_stack);
        free(self->task_tcb);
        self->task_stack = NULL;
        self->task_tcb = NULL;
        self->state = ha_discovery_state_failed;
        return;
    }

    self->task_running = true;
    self->task_handle = xTaskCreateStatic(
        build_task,
        "ha_discovery_build",
        STACK_SIZE,
        self,
        1,
        self->task_stack,
        self->task_tcb);

    if (!self->task_handle) {
        ESP_LOGE(TAG, "Failed to create build task");
        free(self->task_stack);
        free(self->task_tcb);
        self->task_stack = NULL;
        self->task_tcb = NULL;
        self->state = ha_discovery_state_failed;
        self->task_running = false;
        return;
    }

    self->state = ha_discovery_state_building;
#else
    self->state = ha_discovery_state_complete;
#endif
}

void ha_discovery_manager_cleanup(ha_discovery_manager_t* self)
{
#ifdef USE_ESP_IDF
    if (self->task_handle) {
        self->task_running = false;
        if (self->done_sem) {
            xSemaphoreTake(self->done_sem, pdMS_TO_TICKS(1000));
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
        self->task_handle = NULL;
    }

    cleanup_resources(self);
#endif

    memset(self, 0, sizeof(*self));
}

bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self)
{
    return self->state == ha_discovery_state_building ||
           self->state == ha_discovery_state_cleaning ||
           self->state == ha_discovery_state_discovering;
}

ha_discovery_state_t ha_discovery_manager_get_state(ha_discovery_manager_t* self)
{
    return self->state;
}

void ha_discovery_manager_set_time_fn(
    ha_discovery_manager_t* self,
    uint32_t (*get_time_ms)(void))
{
    self->get_time_ms = get_time_ms;
}
