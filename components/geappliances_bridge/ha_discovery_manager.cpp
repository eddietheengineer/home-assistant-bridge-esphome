/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager implementation.
 */

#include "ha_discovery_manager.h"
#include "ha_discovery_data.h"
#include "i_tiny_event.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>


#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#ifdef USE_ESP_IDF
#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#ifdef USE_ESP_IDF_STUBS
#include "esp-idf/zlib_stub.h"
#else
/* Use miniz for decompression on ESP-IDF (no zlib in ESP-IDF 5.x).
 * MINIZ_NO_ARCHIVE_APIS: we only need inflate.
 * MINIZ_NO_ZLIB_COMPATIBLE_NAMES: avoid conflicts if zlib is present.
 * MINIZ_NO_STDIO: no file I/O needed. */
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_STDIO
#include "miniz.h"
#endif
#endif /* USE_ESP_IDF */


static const char* const TAG = "ha_discovery_manager";

/* ------------------------------------------------------------------ */
/* Zero-allocation JSON parser helpers                                */
/* ------------------------------------------------------------------ */

/*
 * json_get_str: extract a string value for a given key from a JSON line.
 * Returns a pointer into the source string, or NULL if not found.
 * The returned pointer is valid as long as the source string is valid.
 * The value is NOT null-terminated by this function; the caller must
 * find the end (next comma, closing brace, or end of string).
 */
static const char* json_get_str(const char* json, const char* key,
                                 const char** out_value, size_t* out_len)
{
    size_t key_len = strlen(key);
    const char* p = json;

    while ((p = strstr(p, "\"")) != NULL) {
        if (strncmp(p, "\"", 1) == 0 &&
            strncmp(p + 1, key, key_len) == 0 &&
            p[key_len + 1] == '\"') {
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


/* ------------------------------------------------------------------ */
/* Embedded category data declarations                                  */
/* ------------------------------------------------------------------ */

/*
 * Category data is now defined in ha_discovery_data.h with chunked
 * compression. The header provides:
 *   ha_discovery_category_t - category descriptor with chunk table
 *   ha_discovery_categories[] - array of all categories
 *   ha_discovery_category_count - number of categories
 */

static const size_t num_categories = ha_discovery_category_count;

/* ------------------------------------------------------------------ */
/* Decompression helpers                                                */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
/* Decompress a single chunk using tinfl (raw deflate, no zlib header).
 * Returns 0 on success, -1 on failure. */
static int IRAM_ATTR chunk_decompress(const uint8_t* compressed, size_t compressed_len,
                           uint8_t* output, size_t* output_len)
{
#ifdef USE_ESP_IDF_STUBS
    (void)compressed; (void)compressed_len; (void)output; (void)output_len;
    return -1;
#else
    tinfl_decompressor decomp;
    tinfl_init(&decomp);

    size_t src_size = compressed_len;
    size_t dst_size = *output_len;

    tinfl_status status = tinfl_decompress(
        &decomp,
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
/* ERD cache lookup                                                     */
/* ------------------------------------------------------------------ */

/*
 * erd_cache_find is a static function in erd_cache.cpp.
 * We replicate the linear scan here to avoid depending on an internal function.
 */
static bool erd_is_registered(const ha_discovery_manager_t* self, uint16_t erd_id)
{
    if (!self->cache) return false;

    for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
        erd_cache_entry_t* e = &self->cache->entries[i];
        if (e->valid && e->erd == erd_id) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Discovery payload builder                                            */
/* ------------------------------------------------------------------ */

/*
 * Build the HA discovery topic:
 *   homeassistant/<domain>/<device_id>/<topic_key>/config
 */
static int build_discovery_topic(char* buf, size_t buf_size,
                                  const char* domain,
                                  const char* device_id,
                                  const char* erd_id_hex,
                                  const char* field_suffix)
{
    if (field_suffix && field_suffix[0]) {
        return snprintf(buf, buf_size,
            "homeassistant/%s/%s/%s_%s/config",
            domain, device_id, erd_id_hex, field_suffix);
    } else {
        return snprintf(buf, buf_size,
            "homeassistant/%s/%s/%s/config",
            domain, device_id, erd_id_hex);
    }
}

/*
 * Build the HA discovery JSON payload.
 * The payload is a compact JSON object conforming to the HA MQTT Discovery schema.
 */
static int build_discovery_payload(char* buf, size_t buf_size,
                                    const ha_discovery_manager_t* self,
                                    const char* erd_id_hex,
                                    const char* entity_name,
                                    const char* field_id,
                                    const char* paired_erd,
                                    const char* role,
                                    const char* value_template,
                                    const char* command_template,
                                    const char* unit,
                                    const char* device_class,
                                    const char* state_class,
                                    const char* options,
                                    const char* data_type,
                                    const char* scale_factor)
{
    /* Build unique_id */
    char unique_id[128];
    if (field_id && field_id[0]) {
        snprintf(unique_id, sizeof(unique_id),
            "%s_erd_%s_%s", self->device_id, erd_id_hex, field_id);
    } else {
        snprintf(unique_id, sizeof(unique_id),
            "%s_erd_%s", self->device_id, erd_id_hex);
    }

    /* Build state_topic and command_topic */
    char state_topic[128];
    char command_topic[128];

    snprintf(state_topic, sizeof(state_topic),
        "geappliances/%s/erd/0x%s/value", self->device_id, erd_id_hex);

    snprintf(command_topic, sizeof(command_topic),
        "geappliances/%s/erd/0x%s/write", self->device_id, erd_id_hex);

    /* For paired entities, the request ERD is the command topic, status is state topic */
    char actual_state_topic[128];
    char actual_command_topic[128];

    if (paired_erd && paired_erd[0]) {
        if (role && strcmp(role, "request") == 0) {
            /* This is the request ERD — command goes to this ERD, state comes from paired */
            snprintf(actual_command_topic, sizeof(actual_command_topic),
                "geappliances/%s/erd/0x%s/write", self->device_id, erd_id_hex);
            snprintf(actual_state_topic, sizeof(actual_state_topic),
                "geappliances/%s/erd/0x%s/value", self->device_id, paired_erd);
        } else {
            /* This is the status ERD — state comes from this ERD, command goes to paired */
            snprintf(actual_state_topic, sizeof(actual_state_topic),
                "geappliances/%s/erd/0x%s/value", self->device_id, erd_id_hex);
            snprintf(actual_command_topic, sizeof(actual_command_topic),
                "geappliances/%s/erd/0x%s/write", self->device_id, paired_erd);
        }
    } else {
        strncpy(actual_state_topic, state_topic, sizeof(actual_state_topic));
        strncpy(actual_command_topic, command_topic, sizeof(actual_command_topic));
    }

    /* Build the JSON payload */
    int offset = 0;
    offset += snprintf(buf + offset, buf_size - (size_t)offset,
        "{\"name\":\"%s\",\"unique_id\":\"%s\",\"device\":{\"identifiers\":[\"%s\"],",
        entity_name, unique_id, self->device_id);

    if (self->model_number && self->model_number[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"model\":\"%s\",", self->model_number);
    }

    if (self->serial_number && self->serial_number[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"serial_number\":\"%s\",", self->serial_number);
    }

    offset += snprintf(buf + offset, buf_size - (size_t)offset,
        "\"manufacturer\":\"GE Appliances\"},");

    offset += snprintf(buf + offset, buf_size - (size_t)offset,
        "\"state_topic\":\"%s\",", actual_state_topic);

    if (command_template && command_template[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"command_topic\":\"%s\",", actual_command_topic);
    }

    if (value_template && value_template[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"value_template\":\"%s\",", value_template);
    }

    if (command_template && command_template[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"command_template\":\"%s\",", command_template);
    }

    if (unit && unit[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"unit_of_measurement\":\"%s\",", unit);
    }

    if (device_class && device_class[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"device_class\":\"%s\",", device_class);
    }

    if (state_class && state_class[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"state_class\":\"%s\",", state_class);
    }

    if (options && options[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"options\":%s,", options);
    }

    if (data_type && data_type[0]) {
        /* For number domain, add min/max based on data type */
        if (strcmp(data_type, "u8") == 0) {
            offset += snprintf(buf + offset, buf_size - (size_t)offset,
                "\"min\":0,\"max\":255,");
        } else if (strcmp(data_type, "i8") == 0) {
            offset += snprintf(buf + offset, buf_size - (size_t)offset,
                "\"min\":-128,\"max\":127,");
        } else if (strcmp(data_type, "u16") == 0) {
            offset += snprintf(buf + offset, buf_size - (size_t)offset,
                "\"min\":0,\"max\":65535,");
        } else if (strcmp(data_type, "i16") == 0) {
            offset += snprintf(buf + offset, buf_size - (size_t)offset,
                "\"min\":-32768,\"max\":32767,");
        } else if (strcmp(data_type, "u32") == 0) {
            offset += snprintf(buf + offset, buf_size - (size_t)offset,
                "\"min\":0,\"max\":4294967295,");
        } else if (strcmp(data_type, "i32") == 0) {
            offset += snprintf(buf + offset, buf_size - (size_t)offset,
                "\"min\":-2147483648,\"max\":2147483647,");
        }
    }

    if (scale_factor && scale_factor[0]) {
        offset += snprintf(buf + offset, buf_size - (size_t)offset,
            "\"step\":%s,", scale_factor);
    }

    /* Remove trailing comma and close */
    if (offset > 0 && buf[offset - 1] == ',') {
        buf[offset - 1] = '\0';
        offset--;
    }

    offset += snprintf(buf + offset, buf_size - (size_t)offset, "}");

    return offset;
}
/* ------------------------------------------------------------------ */
/* Direct publish helper (replaces queue)                              */
/* ------------------------------------------------------------------ */

static void publish_entity(ha_discovery_manager_t* self, const char* line)
{
    const char* val = NULL;
    size_t len = 0;

    if (!json_get_str(line, "i", &val, &len)) return;
    char erd_id_hex[8];
    memcpy(erd_id_hex, val, len);
    erd_id_hex[len] = '\0';

    if (!json_get_str(line, "n", &val, &len)) return;
    char entity_name[128];
    if (len >= sizeof(entity_name)) len = sizeof(entity_name) - 1;
    memcpy(entity_name, val, len);
    entity_name[len] = '\0';

    if (!json_get_str(line, "d", &val, &len)) return;
    char domain[32];
    if (len >= sizeof(domain)) len = sizeof(domain) - 1;
    memcpy(domain, val, len);
    domain[len] = '\0';

    char field_id[16] = "";
    char paired_erd[8] = "";
    char role[16] = "";
    char value_template[512] = "";
    char command_template[512] = "";
    char unit[32] = "";
    char device_class[32] = "";
    char state_class[32] = "";
    char options[256] = "";
    char data_type[16] = "";
    char scale_factor[16] = "";

    if (json_get_str(line, "fi", &val, &len)) {
        if (len >= sizeof(field_id)) len = sizeof(field_id) - 1;
        memcpy(field_id, val, len);
        field_id[len] = '\0';
    }
    if (json_get_str(line, "p", &val, &len)) {
        if (len >= sizeof(paired_erd)) len = sizeof(paired_erd) - 1;
        memcpy(paired_erd, val, len);
        paired_erd[len] = '\0';
    }
    if (json_get_str(line, "r", &val, &len)) {
        if (len >= sizeof(role)) len = sizeof(role) - 1;
        memcpy(role, val, len);
        role[len] = '\0';
    }
    if (json_get_str(line, "vt", &val, &len)) {
        if (len >= sizeof(value_template)) len = sizeof(value_template) - 1;
        memcpy(value_template, val, len);
        value_template[len] = '\0';
    }
    if (json_get_str(line, "ct", &val, &len)) {
        if (len >= sizeof(command_template)) len = sizeof(command_template) - 1;
        memcpy(command_template, val, len);
        command_template[len] = '\0';
    }
    if (json_get_str(line, "u", &val, &len)) {
        if (len >= sizeof(unit)) len = sizeof(unit) - 1;
        memcpy(unit, val, len);
        unit[len] = '\0';
    }
    if (json_get_str(line, "dc", &val, &len)) {
        if (len >= sizeof(device_class)) len = sizeof(device_class) - 1;
        memcpy(device_class, val, len);
        device_class[len] = '\0';
    }
    if (json_get_str(line, "sc", &val, &len)) {
        if (len >= sizeof(state_class)) len = sizeof(state_class) - 1;
        memcpy(state_class, val, len);
        state_class[len] = '\0';
    }
    if (json_get_str(line, "o", &val, &len)) {
        if (len >= sizeof(options)) len = sizeof(options) - 1;
        memcpy(options, val, len);
        options[len] = '\0';
    }
    if (json_get_str(line, "dt", &val, &len)) {
        if (len >= sizeof(data_type)) len = sizeof(data_type) - 1;
        memcpy(data_type, val, len);
        data_type[len] = '\0';
    }
    if (json_get_str(line, "sf", &val, &len)) {
        if (len >= sizeof(scale_factor)) len = sizeof(scale_factor) - 1;
        memcpy(scale_factor, val, len);
        scale_factor[len] = '\0';
    }

    uint16_t erd_id = (uint16_t)strtoul(erd_id_hex, NULL, 16);

    if (!erd_is_registered(self, erd_id)) {
        self->total_filtered++;
        return;
    }

    if (paired_erd[0]) {
        uint16_t paired_id = (uint16_t)strtoul(paired_erd, NULL, 16);
        if (!erd_is_registered(self, paired_id)) {
            self->total_filtered++;
            return;
        }
    }

    build_discovery_topic(self->topic_buf, sizeof(self->topic_buf),
        domain, self->device_id, erd_id_hex, field_id);

    build_discovery_payload(self->payload_buf, sizeof(self->payload_buf),
        self, erd_id_hex, entity_name,
        field_id, paired_erd, role,
        value_template, command_template,
        unit, device_class, state_class,
        options, data_type, scale_factor);

    mqtt_client_publish_raw(self->mqtt_client,
        self->topic_buf, self->payload_buf, strlen(self->payload_buf), true);

    self->total_discovered++;
    self->total_published++;
}

/* ------------------------------------------------------------------ */
/* Streaming chunk processing: decompress → publish → discard          */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
/* Process a single chunk: decompress, publish each entity, discard. */
static void IRAM_ATTR process_chunk_streaming(ha_discovery_manager_t* self,
    const uint8_t* compressed, size_t compressed_len)
{
    size_t dst_size = sizeof(self->decompress_buf);

    if (chunk_decompress(compressed, compressed_len,
                         self->decompress_buf, &dst_size) != 0) {
        return;
    }

    /* Parse line by line from decompressed data, publishing each entity. */
    const char* p = (const char*)self->decompress_buf;
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

        publish_entity(self, self->line_buf);

        p = line_end + 1;
    }
}

static void IRAM_ATTR process_category_streaming(ha_discovery_manager_t* self,
    const ha_discovery_category_t* cat)
{
    uint32_t before = self->total_published;

    for (uint16_t ci = 0; ci < cat->num_chunks; ci++) {
        const ha_discovery_chunk_t* chunk = &cat->chunks[ci];
        const uint8_t* src = cat->data + chunk->offset;

        process_chunk_streaming(self, src, chunk->size);
    }

    ESP_LOGI(TAG, "Category %s: %u published",
        cat->name, self->total_published - before);
}
#endif

/* ------------------------------------------------------------------ */
/* Single streaming task (ESP-IDF)                                     */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void discovery_task(void* arg)
{
    ha_discovery_manager_t* self = (ha_discovery_manager_t*)arg;

    self->state = ha_discovery_state_processing;

    ESP_LOGI(TAG, "Starting HA discovery streaming...");

    /* Walk every category, processing each chunk: decompress → publish → discard. */
    for (size_t i = 0; i < num_categories; i++) {
        process_category_streaming(self, &ha_discovery_categories[i]);

        /* Yield after each category so the main loop and other tasks
         * can run.  Prevents starving the ESPHome framework watchdog. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    self->state = ha_discovery_state_complete;
    ESP_LOGI(TAG, "HA discovery complete: %u published, %u filtered",
        self->total_published, self->total_filtered);

    /* Signal completion */
    if (self->done_semaphore) {
        xSemaphoreGive(self->done_semaphore);
    }

    vTaskDelete(NULL);
}
#endif

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void ha_discovery_manager_init(ha_discovery_manager_t* self)
{
    memset(self, 0, sizeof(*self));
    self->state = ha_discovery_state_idle;
    self->get_time_ms = esphome::millis;

#ifdef USE_ESP_IDF
    self->done_semaphore = xSemaphoreCreateBinary();
    if (!self->done_semaphore) {
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
    self->task_running = true;
    self->task_handle = xTaskCreateStatic(
        discovery_task,
        "ha_discovery",
        2048,
        self,
        2,
        self->task_stack,
        &self->task_tcb);
    if (!self->task_handle) {
        ESP_LOGE(TAG, "Failed to create discovery task");
        self->state = ha_discovery_state_failed;
        self->task_running = false;
        return;
    }
#else
    /* Non-ESP-IDF: cannot decompress, mark complete. */
    self->state = ha_discovery_state_complete;
#endif
}

void ha_discovery_manager_cleanup(ha_discovery_manager_t* self)
{
#ifdef USE_ESP_IDF
    if (self->task_handle) {
        self->task_running = false;
        if (self->done_semaphore) {
            xSemaphoreTake(self->done_semaphore, pdMS_TO_TICKS(1000));
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
        self->task_handle = NULL;
    }

    if (self->done_semaphore) {
        vSemaphoreDelete(self->done_semaphore);
        self->done_semaphore = NULL;
    }
#endif

    memset(self, 0, sizeof(*self));
}

bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self)
{
    return self->state == ha_discovery_state_processing;
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
