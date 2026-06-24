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
                while (*end && *end != '"' && *end != ',' && *end != '}') {
                    if (*end == '\\') end++;
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

/* Re-escape a raw JSON string value for embedding in another JSON string.
 * The input is already JSON-escaped (contains \\, \", etc.).
 * To embed it as a JSON string value, we need to double the backslashes:
 *   \\ -> \\\\   \" -> \\\"
 * Returns the number of bytes written (excluding null terminator).
 * This avoids needing an intermediate unescaped buffer. */
static int json_reescape(const char* src, size_t src_len, char* out, int out_size)
{
    int i = 0;
    const char* p = src;
    const char* end = src + src_len;
    while (p < end) {
        if (*p == '\\' && p + 1 < end) {
            // Always emit a literal backslash for the escape
            if (i >= out_size - 1) break;
            out[i++] = '\\';
            p++;
            // Then handle the escaped character
            if (i >= out_size - 1) break;
            switch (*p) {
                case '\\':
                case '"':
                    // Double-escape: \\ -> \\\\ or \" -> \\\"
                    if (i >= out_size - 1) break;
                    out[i++] = '\\';
                    if (i >= out_size - 1) break;
                    out[i++] = *p;
                    break;
                case '/':
                    out[i++] = '/';
                    break;
                case 'n':
                    out[i++] = 'n';
                    break;
                case 'r':
                    out[i++] = 'r';
                    break;
                case 't':
                    out[i++] = 't';
                    break;
                case 'u':
                    // \uXXXX — pass through
                    if (i + 5 > out_size - 1) { p += 4; break; }
                    out[i++] = 'u';
                    out[i++] = p[1];
                    out[i++] = p[2];
                    out[i++] = p[3];
                    out[i++] = p[4];
                    p += 4;
                    break;
                default:
                    out[i++] = *p;
                    break;
            }
        } else {
            if (i >= out_size - 1) break;
            out[i++] = *p;
        }
        p++;
    }
    if (i < out_size) out[i] = '\0';
    return i;
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
#endif

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
    char erd_id_hex[8];
    if (len >= sizeof(erd_id_hex)) len = sizeof(erd_id_hex) - 1;
    memcpy(erd_id_hex, val, len);
    erd_id_hex[len] = '\0';

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
    self->scale_factor_buf[0] = '\0';

    if (json_get_str(line, "fi", &val, &len)) json_unescape(val, len, self->field_id_buf, sizeof(self->field_id_buf));
    if (json_get_str(line, "p", &val, &len)) json_unescape(val, len, self->paired_erd_buf, sizeof(self->paired_erd_buf));
    if (json_get_str(line, "r", &val, &len)) json_unescape(val, len, self->role_buf, sizeof(self->role_buf));
    if (json_get_str(line, "u", &val, &len)) json_unescape(val, len, self->unit_buf, sizeof(self->unit_buf));
    if (json_get_str(line, "dc", &val, &len)) json_unescape(val, len, self->device_class_buf, sizeof(self->device_class_buf));
    if (json_get_str(line, "sc", &val, &len)) json_unescape(val, len, self->state_class_buf, sizeof(self->state_class_buf));
    if (json_get_str(line, "o", &val, &len)) json_unescape(val, len, self->options_buf, sizeof(self->options_buf));
    if (json_get_str(line, "dt", &val, &len)) json_unescape(val, len, self->data_type_buf, sizeof(self->data_type_buf));
    if (json_get_str(line, "sf", &val, &len)) json_unescape(val, len, self->scale_factor_buf, sizeof(self->scale_factor_buf));

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
    int offset = 0;
    int remaining = (int)sizeof(self->payload_buf);

    offset += snprintf(payload + offset, remaining,
        "{\"name\":\"%s\",\"unique_id\":\"%s\",\"device\":%s,",
        self->entity_name_buf, self->unique_id_buf, self->device_json_buf);
    remaining -= offset;

    offset += snprintf(payload + offset, remaining,
        "\"state_topic\":\"%s\",", self->actual_state_topic_buf);
    remaining -= offset;

    /* Embed value_template directly from raw JSONL with re-escaping. */
    if (json_get_str(line, "vt", &val, &len)) {
        offset += snprintf(payload + offset, remaining, "\"value_template\":\"");
        remaining -= offset;
        int reescaped = json_reescape(val, len, payload + offset, remaining);
        offset += reescaped;
        remaining -= reescaped;
        offset += snprintf(payload + offset, remaining, "\",");
        remaining -= offset;
    }

    /* Embed command_template directly from raw JSONL with re-escaping. */
    if (json_get_str(line, "ct", &val, &len)) {
        offset += snprintf(payload + offset, remaining, "\"command_topic\":\"%s\",\"command_template\":\"", self->actual_command_topic_buf);
        remaining -= offset;
        int reescaped = json_reescape(val, len, payload + offset, remaining);
        offset += reescaped;
        remaining -= reescaped;
        offset += snprintf(payload + offset, remaining, "\",");
        remaining -= offset;
    } else if (self->paired_erd_buf[0]) {
        offset += snprintf(payload + offset, remaining, "\"command_topic\":\"%s\",", self->actual_command_topic_buf);
        remaining -= offset;
    }

    if (self->unit_buf[0]) {
        offset += snprintf(payload + offset, remaining, "\"unit_of_measurement\":\"%s\",", self->unit_buf);
        remaining -= offset;
    }
    if (self->device_class_buf[0]) {
        offset += snprintf(payload + offset, remaining, "\"device_class\":\"%s\",", self->device_class_buf);
        remaining -= offset;
    }
    if (self->state_class_buf[0]) {
        offset += snprintf(payload + offset, remaining, "\"state_class\":\"%s\",", self->state_class_buf);
        remaining -= offset;
    }
    if (self->options_buf[0]) {
        offset += snprintf(payload + offset, remaining, "\"options\":%s,", self->options_buf);
        remaining -= offset;
    }
    if (self->data_type_buf[0]) {
        if (strcmp(self->data_type_buf, "u8") == 0) {
            offset += snprintf(payload + offset, remaining, "\"min\":0,\"max\":255,");
        } else if (strcmp(self->data_type_buf, "i8") == 0) {
            offset += snprintf(payload + offset, remaining, "\"min\":-128,\"max\":127,");
        } else if (strcmp(self->data_type_buf, "u16") == 0) {
            offset += snprintf(payload + offset, remaining, "\"min\":0,\"max\":65535,");
        } else if (strcmp(self->data_type_buf, "i16") == 0) {
            offset += snprintf(payload + offset, remaining, "\"min\":-32768,\"max\":32767,");
        } else if (strcmp(self->data_type_buf, "u32") == 0) {
            offset += snprintf(payload + offset, remaining, "\"min\":0,\"max\":4294967295,");
        } else if (strcmp(self->data_type_buf, "i32") == 0) {
            offset += snprintf(payload + offset, remaining, "\"min\":-2147483648,\"max\":2147483647,");
        }
        remaining -= offset;
    }
    if (self->scale_factor_buf[0]) {
        offset += snprintf(payload + offset, remaining, "\"step\":%s,", self->scale_factor_buf);
        remaining -= offset;
    }

    /* Remove trailing comma and close */
    if (offset > 0 && payload[offset - 1] == ',') {
        payload[offset - 1] = '\0';
        offset--;
    }
    offset += snprintf(payload + offset, sizeof(self->payload_buf) - (size_t)offset, "}");

    /* Check if payload fits */
    if (offset >= (int)sizeof(self->payload_buf)) {
        ESP_LOGW(TAG, "Payload too large for ERD 0x%s, skipping", erd_id_hex);
        self->total_filtered++;
        return false;
    }

    self->total_discovered++;
    return true;
}
#endif

/* ------------------------------------------------------------------ */
/* Process a category: decompress chunks, parse lines, sync with consumer */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void process_category(ha_discovery_manager_t* self,
    const ha_discovery_category_t* cat)
{
    uint32_t before = self->total_discovered;

    for (uint16_t ci = 0; ci < cat->num_chunks; ci++) {
        const ha_discovery_chunk_t* chunk = &cat->chunks[ci];
        const uint8_t* src = cat->data + chunk->offset;

        size_t dst_size = sizeof(self->decomp_buf);
        if (chunk_decompress(self, src, chunk->size, self->decomp_buf, &dst_size) != 0) {
            continue;
        }

        const char* p = (const char*)self->decomp_buf;
        const char* end = p + dst_size;

        while (p < end) {
            const char* line_end = p;
            while (line_end < end && *line_end != '\n' && *line_end != '\r') line_end++;

            size_t line_len = (size_t)(line_end - p);
            if (line_len == 0) {
                p = line_end + 1;
                continue;
            }
            if (line_len >= sizeof(self->line_buf) - 1) {
                line_len = sizeof(self->line_buf) - 1;
            }
            memcpy(self->line_buf, p, line_len);
            self->line_buf[line_len] = '\0';

            /* Wait for consumer to finish publishing previous payload. */
            if (xSemaphoreTake(self->publish_sem, portMAX_DELAY) != pdTRUE) {
                break;
            }

            if (process_jsonl_line(self, self->line_buf)) {
                /* Signal consumer: payload ready in shared buffer. */
                xSemaphoreGive(self->publish_sem);
            } else {
                /* Line filtered — give sem back so producer can continue. */
                xSemaphoreGive(self->publish_sem);
            }

            p = line_end + 1;
        }
    }

    ESP_LOGI(TAG, "Category %s: %u discovered",
        cat->name, self->total_discovered - before);
}
#endif

/* ------------------------------------------------------------------ */
/* Fetch task (producer)                                              */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void fetch_task(void* arg)
{
    ha_discovery_manager_t* self = (ha_discovery_manager_t*)arg;

    self->state = ha_discovery_state_fetching;

    ESP_LOGI(TAG, "Starting HA discovery fetch...");

    /* Build sorted ERD list and device JSON. */
    build_sorted_erd_list(self);
    build_device_json(self);

    /* publish_sem starts empty. Give it so the first entity can proceed
     * without blocking (producer takes before building, gives after). */
    xSemaphoreGive(self->publish_sem);
    for (size_t i = 0; i < ha_discovery_category_count; i++) {
        process_category(self, &ha_discovery_categories[i]);

        /* Yield and feed WDT after each category. */
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_task_wdt_reset();
    }

    /* Signal done via semaphore. */
    if (self->done_sem) {
        xSemaphoreGive(self->done_sem);
    }

    ESP_LOGI(TAG, "HA discovery fetch complete: %u discovered, %u filtered",
        self->total_discovered, self->total_filtered);

    vTaskDelete(NULL);
}
#endif

/* ------------------------------------------------------------------ */
/* Consumer: run() called from main loop                              */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void cleanup_fetch_resources(ha_discovery_manager_t* self)
{
    if (self->publish_sem) {
        vSemaphoreDelete(self->publish_sem);
        self->publish_sem = NULL;
    }
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

void ha_discovery_manager_run(ha_discovery_manager_t* self)
{
#ifdef USE_ESP_IDF
    if (self->state != ha_discovery_state_fetching && self->state != ha_discovery_state_publishing) {
        return;
    }

    if (self->state == ha_discovery_state_fetching) {
        self->state = ha_discovery_state_publishing;
        self->last_publish_ms = self->get_time_ms();
    }

    /* Check if fetch task has finished. */
    if (!self->fetch_done && self->done_sem) {
        if (xSemaphoreTake(self->done_sem, 0) == pdTRUE) {
            self->fetch_done = true;
            self->task_handle = NULL;
        }
    }

    /* Try to take the publish semaphore (non-blocking).
     * If taken, the producer has built a payload for us to publish. */
    if (xSemaphoreTake(self->publish_sem, 0) != pdTRUE) {
        /* No payload ready yet. */
        if (self->fetch_done) {
            /* Fetch is done and no pending payload. But we need to ensure
             * the last entity was published. The producer gives publish_sem
             * for each entity then takes it back. If fetch_done and sem is
             * not available, either: (a) the last entity is still being
             * published by us (sem taken, not yet given back), or
             * (b) all entities are published. Check if we've published
             * as many as were discovered. */
            if (self->total_published >= self->total_discovered) {
                cleanup_fetch_resources(self);
                self->state = ha_discovery_state_complete;
                ESP_LOGI(TAG, "HA discovery complete: %u published, %u filtered",
                    self->total_published, self->total_filtered);
            }
        }
        return;
    }

    /* Rate-limit publishing. */
    uint32_t now = self->get_time_ms();
    if (now - self->last_publish_ms < HA_DISCOVERY_PUBLISH_INTERVAL_MS) {
        /* Give semaphore back immediately so producer can continue building.
         * We'll take it again next loop iteration. */
        xSemaphoreGive(self->publish_sem);
        return;
    }
    self->last_publish_ms = now;

    /* Publish from shared buffer. */
    if (self->mqtt_client) {
        mqtt_client_publish_raw(self->mqtt_client, self->topic_buf, self->payload_buf, strlen(self->payload_buf), true);
    }
    self->total_published++;

    ESP_LOGD(TAG, "Published: %s", self->entity_name_buf);

    /* Give semaphore back so producer can build the next payload. */
    xSemaphoreGive(self->publish_sem);

    esp_task_wdt_reset();
#else
    (void)self;
#endif
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void ha_discovery_manager_init(ha_discovery_manager_t* self)
{
    memset(self, 0, sizeof(*self));
    self->state = ha_discovery_state_idle;
    self->get_time_ms = esphome::millis;

#ifdef USE_ESP_IDF
    self->publish_sem = xSemaphoreCreateBinary();
    if (!self->publish_sem) {
        ESP_LOGE(TAG, "Failed to create publish semaphore");
    }
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
    erd_cache_t* cache,
    i_mqtt_client_t* mqtt_client)
{
    self->device_id = device_id;
    self->model_number = model_number;
    self->serial_number = serial_number;
    self->cache = cache;
    self->mqtt_client = mqtt_client;
}

void ha_discovery_manager_start(ha_discovery_manager_t* self)
{
    if (self->state != ha_discovery_state_idle) return;

#ifdef USE_ESP_IDF
    /* Allocate task stack — try 8KB, fall back to 4KB. */
    static constexpr int STACK_SIZE_BIG = 8 * 1024;
    static constexpr int STACK_SIZE_SMALL = 4 * 1024;

    self->task_stack = (StackType_t*)heap_caps_malloc(STACK_SIZE_BIG, MALLOC_CAP_8BIT);
    if (!self->task_stack) {
        self->task_stack = (StackType_t*)heap_caps_malloc(STACK_SIZE_SMALL, MALLOC_CAP_8BIT);
    }

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

    int stack_size = self->task_stack ? STACK_SIZE_BIG : STACK_SIZE_SMALL;

    self->task_running = true;
    self->task_handle = xTaskCreateStatic(
        fetch_task,
        "ha_discovery",
        stack_size,
        self,
        1,
        self->task_stack,
        self->task_tcb);

    if (!self->task_handle) {
        ESP_LOGE(TAG, "Failed to create discovery task");
        free(self->task_stack);
        free(self->task_tcb);
        self->task_stack = NULL;
        self->task_tcb = NULL;
        self->state = ha_discovery_state_failed;
        self->task_running = false;
        return;
    }

    self->state = ha_discovery_state_fetching;
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

    cleanup_fetch_resources(self);
#endif

    memset(self, 0, sizeof(*self));
}

bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self)
{
    return self->state == ha_discovery_state_fetching ||
           self->state == ha_discovery_state_publishing;
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
