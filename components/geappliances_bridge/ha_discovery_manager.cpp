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
#include "esp_task_wdt.h"
#include "esp_log.h"
#ifdef USE_ESP_IDF_STUBS
#include "esp-idf/zlib_stub.h"
#else
#include "zlib.h"
#endif
#endif


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
 * Category data: array of (name, data pointer, data length).
 * Name is used for logging; data/len are the compressed byte arrays
 * from ha_discovery_data.h.
 */
typedef struct {
    const char* name;
    const uint8_t* data;
    size_t data_len;
} ha_discovery_category_t;

static const ha_discovery_category_t categories[] = {
    { "common", ha_discovery_common, ha_discovery_common_len },
    { "refrigeration", ha_discovery_refrigeration, ha_discovery_refrigeration_len },
    { "laundry", ha_discovery_laundry, ha_discovery_laundry_len },
    { "dishwasher", ha_discovery_dishwasher, ha_discovery_dishwasher_len },
    { "waterheater", ha_discovery_waterheater, ha_discovery_waterheater_len },
    { "range", ha_discovery_range, ha_discovery_range_len },
    { "airconditioning", ha_discovery_airconditioning, ha_discovery_airconditioning_len },
    { "waterfilter", ha_discovery_waterfilter, ha_discovery_waterfilter_len },
    { "smallappliance", ha_discovery_smallappliance, ha_discovery_smallappliance_len },
    { "energy", ha_discovery_energy, ha_discovery_energy_len },
};

static const size_t num_categories = sizeof(categories) / sizeof(categories[0]);

/* ------------------------------------------------------------------ */
/* Decompression helpers                                                */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static int zlib_decompress(const uint8_t* compressed, size_t compressed_len,
                           uint8_t* output, size_t* output_len)
{
#ifdef USE_ESP_IDF_STUBS
    /* Stub: can't actually decompress in test builds.
     * Return error so the decompress task gracefully skips. */
    (void)compressed;
    (void)compressed_len;
    (void)output;
    (void)output_len;
    return -1;
#else
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.next_in = (Bytef*)compressed;
    strm.avail_in = (uInt)compressed_len;
    strm.next_out = output;
    strm.avail_out = (uInt)*output_len;

    if (inflateInit2(&strm, 31 + 15) != Z_OK) {
        return -1;
    }

    int ret = inflate(&strm, Z_FINISH);
    *output_len = strm.total_out;

    if (ret != Z_STREAM_END && ret != Z_OK) {
        inflateEnd(&strm);
        return -1;
    }

    inflateEnd(&strm);
    return 0;
#endif
}
#endif

/* ------------------------------------------------------------------ */
/* Queue helpers                                                        */
/* ------------------------------------------------------------------ */

static bool queue_is_full(const ha_discovery_manager_t* self)
{
    return self->queue_count >= HA_DISCOVERY_QUEUE_CAPACITY;
}

static bool queue_is_empty(const ha_discovery_manager_t* self)
{
    return self->queue_count == 0;
}

static bool queue_push(ha_discovery_manager_t* self, const char* topic, const char* payload)
{
    if (queue_is_full(self)) return false;

    ha_discovery_item_t* item = &self->queue[self->queue_tail];
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);

    if (topic_len >= HA_DISCOVERY_TOPIC_SIZE) return false;
    if (payload_len >= HA_DISCOVERY_PAYLOAD_SIZE) return false;

    memcpy(item->topic, topic, topic_len + 1);
    memcpy(item->payload, payload, payload_len + 1);

    self->queue_tail = (self->queue_tail + 1) % HA_DISCOVERY_QUEUE_CAPACITY;
    self->queue_count++;
    return true;
}

static const ha_discovery_item_t* queue_peek(const ha_discovery_manager_t* self)
{
    if (queue_is_empty(self)) return NULL;
    return &self->queue[self->queue_head];
}

static void queue_pop(ha_discovery_manager_t* self)
{
    if (queue_is_empty(self)) return;
    self->queue_head = (self->queue_head + 1) % HA_DISCOVERY_QUEUE_CAPACITY;
    self->queue_count--;
}

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
/* Process a single JSONL line                                          */
/* ------------------------------------------------------------------ */

static void process_entity_line(ha_discovery_manager_t* self, const char* line)
{
    /* Extract fields from the JSON line */
    const char* val = NULL;
    size_t len = 0;


    /* Required fields */
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

    /* Optional fields */
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

    /* Filter: check if ERD is registered in the cache */
    uint16_t erd_id = (uint16_t)strtoul(erd_id_hex, NULL, 16);

    if (!erd_is_registered(self, erd_id)) {
        self->total_filtered++;
        return;
    }

    /* For paired entities, both ERDs must be registered */
    if (paired_erd[0]) {
        uint16_t paired_id = (uint16_t)strtoul(paired_erd, NULL, 16);
        if (!erd_is_registered(self, paired_id)) {
            self->total_filtered++;
            return;
        }
    }

    /* Build discovery topic */
    char topic[HA_DISCOVERY_TOPIC_SIZE];
    build_discovery_topic(topic, sizeof(topic),
        domain, self->device_id, erd_id_hex, field_id);

    /* Build discovery payload */
    char payload[HA_DISCOVERY_PAYLOAD_SIZE];
    build_discovery_payload(payload, sizeof(payload),
        self, erd_id_hex, entity_name,
        field_id, paired_erd, role,
        value_template, command_template,
        unit, device_class, state_class,
        options, data_type, scale_factor);

    /* Queue the item */
    if (!queue_push(self, topic, payload)) {
        ESP_LOGW(TAG, "Discovery queue full, dropping entity: %s", entity_name);
        return;
    }

    self->total_discovered++;
}

/* ------------------------------------------------------------------ */
/* Decompress and parse a category                                      */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void decompress_and_parse_category(ha_discovery_manager_t* self,
                                           const ha_discovery_category_t* cat)
{
    /* Decompress into a pre-allocated buffer.
     * The decompress_line buffer is used for line-by-line processing.
     * We need a larger buffer for the full decompressed data. */

    /* Use a stack buffer for decompressed data (max ~500KB per category) */
    uint8_t decompressed[512 * 1024];
    size_t decompressed_len = sizeof(decompressed);

    if (zlib_decompress(cat->data, cat->data_len, decompressed, &decompressed_len) != 0) {
        ESP_LOGE(TAG, "Failed to decompress %s", cat->name);
        return;
    }

    decompressed[decompressed_len] = '\0';

    /* Parse line by line */
    const char* p = (const char*)decompressed;
    const char* end = p + decompressed_len;

    while (p < end) {
        /* Find end of line */
        const char* line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;

        size_t line_len = (size_t)(line_end - p);
        if (line_len == 0) {
            p = line_end + 1;
            continue;
        }

        /* Copy line to our buffer */
        if (line_len >= sizeof(self->decompress_line) - 1) {
            line_len = sizeof(self->decompress_line) - 1;
        }
        memcpy(self->decompress_line, p, line_len);
        self->decompress_line[line_len] = '\0';

        process_entity_line(self, self->decompress_line);

        p = line_end + 1;
    }

    ESP_LOGI(TAG, "Category %s: processed (queue now has %u items)",
        cat->name, self->queue_count);
}
#endif

/* ------------------------------------------------------------------ */
/* Decompress task (ESP-IDF)                                            */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void decompress_task(void* arg)
{
    ha_discovery_manager_t* self = (ha_discovery_manager_t*)arg;

    self->state = ha_discovery_state_decompressing;

    ESP_LOGI(TAG, "Starting discovery decompression...");

    /* Process each category */
    for (size_t i = 0; i < num_categories; i++) {
        decompress_and_parse_category(self, &categories[i]);
    }

    /* Transition to publishing if we have items */
    if (self->queue_count > 0) {
        self->state = ha_discovery_state_publishing;
        ESP_LOGI(TAG, "Discovery complete: %u entities, %u filtered, %u queued",
            self->total_discovered, self->total_filtered, self->queue_count);

        /* Signal the publish task */
        if (self->work_semaphore) {
            xSemaphoreGive(self->work_semaphore);
        }
    } else {
        self->state = ha_discovery_state_complete;
        ESP_LOGI(TAG, "No discovery entities to publish");
    }

    /* Signal completion */
    if (self->done_semaphore) {
        xSemaphoreGive(self->done_semaphore);
    }

    vTaskDelete(NULL);
}
#endif

/* ------------------------------------------------------------------ */
/* Publish task (ESP-IDF)                                               */
/* ------------------------------------------------------------------ */

#ifdef USE_ESP_IDF
static void publish_task(void* arg)
{
    ha_discovery_manager_t* self = (ha_discovery_manager_t*)arg;

    if (self->work_semaphore == NULL) {
        vTaskDelete(NULL);
        return;
    }

    while (self->task_running) {
        /* Wait for work signal or timeout */
        if (xSemaphoreTake(self->work_semaphore, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        /* Drain queue */
        while (!queue_is_empty(self)) {
            const ha_discovery_item_t* item = queue_peek(self);
            if (!item) break;

            /* Publish */
            mqtt_client_publish_raw(self->mqtt_client,
                item->topic, item->payload, strlen(item->payload), true);

            queue_pop(self);
            self->total_published++;

            /* 50ms interval between publishes */
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        /* Check if queue is empty and we're done */
        if (queue_is_empty(self) && self->state == ha_discovery_state_publishing) {
            self->state = ha_discovery_state_complete;
            ESP_LOGI(TAG, "All discovery payloads published (%u total)",
                self->total_published);
        }
    }

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
    self->queue_head = 0;
    self->queue_tail = 0;
    self->queue_count = 0;
    self->get_time_ms = esphome::millis;

#ifdef USE_ESP_IDF
    self->work_semaphore = xSemaphoreCreateBinary();
    if (!self->work_semaphore) {
        ESP_LOGE(TAG, "Failed to create work semaphore");
    }
    self->done_semaphore = xSemaphoreCreateBinary();
    if (!self->done_semaphore) {
        ESP_LOGE(TAG, "Failed to create done semaphore");
    }
    self->queue_mutex = xSemaphoreCreateMutex();
    if (!self->queue_mutex) {
        ESP_LOGE(TAG, "Failed to create queue mutex");
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
    /* Start the publish task first (long-lived) */
    self->task_running = true;
    self->publish_task_handle = xTaskCreateStatic(
        publish_task,
        "ha_discovery_pub",
        2048,
        self,
        2,
        self->publish_task_stack,
        &self->publish_task_tcb);
    if (!self->publish_task_handle) {
        ESP_LOGE(TAG, "Failed to create publish task");
        self->state = ha_discovery_state_failed;
        self->task_running = false;
        return;
    }

    /* Start the decompress task (one-shot) */
    self->decompress_task_handle = xTaskCreateStatic(
        decompress_task,
        "ha_discovery_decomp",
        2048,
        self,
        2,
        self->decompress_task_stack,
        &self->decompress_task_tcb);
    if (!self->decompress_task_handle) {
        ESP_LOGE(TAG, "Failed to create decompress task");
        self->state = ha_discovery_state_failed;
        return;
    }
#else
    /* Non-ESP-IDF: run inline */
    self->state = ha_discovery_state_decompressing;

    /* For non-ESP-IDF, we can't decompress with zlib easily.
     * Mark as complete with no entities. */
    self->state = ha_discovery_state_complete;
#endif
}

void ha_discovery_manager_cleanup(ha_discovery_manager_t* self)
{
#ifdef USE_ESP_IDF
    /* Stop publish task */
    if (self->publish_task_handle) {
        self->task_running = false;
        if (self->work_semaphore) {
            xSemaphoreGive(self->work_semaphore);
        }
        if (self->done_semaphore) {
            xSemaphoreTake(self->done_semaphore, pdMS_TO_TICKS(1000));
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
        self->publish_task_handle = NULL;
    }

    /* Wait for decompress task to finish */
    if (self->decompress_task_handle) {
        if (self->done_semaphore) {
            xSemaphoreTake(self->done_semaphore, pdMS_TO_TICKS(1000));
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
        self->decompress_task_handle = NULL;
    }

    if (self->work_semaphore) {
        vSemaphoreDelete(self->work_semaphore);
        self->work_semaphore = NULL;
    }
    if (self->done_semaphore) {
        vSemaphoreDelete(self->done_semaphore);
        self->done_semaphore = NULL;
    }
    if (self->queue_mutex) {
        vSemaphoreDelete(self->queue_mutex);
        self->queue_mutex = NULL;
    }
#endif

    memset(self, 0, sizeof(*self));
}

void ha_discovery_manager_signal_work(ha_discovery_manager_t* self)
{
#ifdef USE_ESP_IDF
    if (self->work_semaphore) {
        xSemaphoreGive(self->work_semaphore);
    }
#else
    (void)self;
#endif
}

uint16_t ha_discovery_manager_run(
    ha_discovery_manager_t* self,
    uint16_t max_publishes)
{
    uint16_t published = 0;

    while (published < max_publishes && !queue_is_empty(self)) {
        const ha_discovery_item_t* item = queue_peek(self);
        if (!item) break;

        mqtt_client_publish_raw(self->mqtt_client,
            item->topic, item->payload, strlen(item->payload), true);

        queue_pop(self);
        self->total_published++;
        published++;
    }

    if (queue_is_empty(self) && self->state == ha_discovery_state_publishing) {
        self->state = ha_discovery_state_complete;
    }

    return published;
}

bool ha_discovery_manager_is_publishing(ha_discovery_manager_t* self)
{
    return self->state == ha_discovery_state_publishing;
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
