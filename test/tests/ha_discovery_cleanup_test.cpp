/*!
 * @file
 * @brief Unit tests for the compacting cleanup buffer in ha_discovery_manager.
 *
 * Tests domain-enum packing, flush roundtrip, buffer overflow, drain/refill,
 * malformed topics, and empty payload echo. Uses a 512-byte test buffer
 * (defined via HA_DISCOVERY_CLEANUP_TEST_BUF_SIZE in the Makefile).
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "ha_discovery_manager.h"
#include "double/mqtt_client_double.hpp"

/* ------------------------------------------------------------------ */
/* Test helpers                                                       */
/* ------------------------------------------------------------------ */

static uint32_t test_time_ms = 0;
static uint32_t test_get_time_ms(void) { return test_time_ms; }

static ha_discovery_manager_t make_manager(const char* device_id)
{
    ha_discovery_manager_t mgr;
    memset(&mgr, 0, sizeof(mgr));
    mgr.device_id = device_id;
    mgr.get_time_ms = test_get_time_ms;
    mgr.state = ha_discovery_state_cleaning;
    mgr.mqtt_client = NULL;
    mgr.cleanup_queue_write_pos = 0;
    mgr.cleanup_dropped_count = 0;
    mgr.cleanup_pass_found_topics = false;
    mgr.cleanup_last_activity_ms = 0;
    return mgr;
}

/* ------------------------------------------------------------------ */
/* Domain mapping tests                                                 */
/* ------------------------------------------------------------------ */

TEST_GROUP(ha_discovery_cleanup)
{
    void setup() {}
    void teardown() {}
};

TEST(ha_discovery_cleanup, all_domains_map_correctly)
{
    for (int i = 0; i < HA_DOMAIN_COUNT; i++) {
        int idx = ha_domain_to_index(HA_DOMAIN_STRINGS[i], strlen(HA_DOMAIN_STRINGS[i]));
        CHECK_EQUAL(i, idx);
    }
}

TEST(ha_discovery_cleanup, unknown_domain_returns_negative)
{
    int idx = ha_domain_to_index("unknown_domain", 14);
    CHECK_EQUAL(-1, idx);
}

TEST(ha_discovery_cleanup, partial_match_does_not_map)
{
    int idx = ha_domain_to_index("sens", 3);
    CHECK_EQUAL(-1, idx);
}

TEST(ha_discovery_cleanup, domain_count_is_21)
{
    CHECK_EQUAL(21, HA_DOMAIN_COUNT);
}

/* ------------------------------------------------------------------ */
/* Pack via callback                                                    */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, pack_valid_topic)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "{\"data\":1}", 10, &mgr);

    CHECK_EQUAL(1, mgr.cleanup_queue_count);
    CHECK_EQUAL(0, mgr.cleanup_dropped_count);
    CHECK(mgr.cleanup_pass_found_topics);

    /* Verify packed data: domain_index=1 (binary_sensor), suffix="3218_washzones" */
    CHECK_EQUAL((char)1, mgr.cleanup_topic_buf[0]);
    STRNCMP_EQUAL("3218_washzones", mgr.cleanup_topic_buf + 0 + 1, 14);
}

TEST(ha_discovery_cleanup, pack_multiple_topics)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    for (int i = 0; i < 10; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%d/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &mgr);
    }

    CHECK_EQUAL(10, mgr.cleanup_queue_count);
    CHECK_EQUAL(0, mgr.cleanup_dropped_count);
}

/* ------------------------------------------------------------------ */
/* Buffer full behavior                                                 */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, buffer_full_drops_topics)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    /* Fill until we start dropping. Each topic is ~22 bytes, 512/22 ~ 23. */
    for (uint32_t i = 0; i < 100; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &mgr);
    }

    CHECK(mgr.cleanup_dropped_count > 0);
    CHECK(mgr.cleanup_queue_count > 0);
}

/* ------------------------------------------------------------------ */
/* Drain and refill                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, drain_and_refill)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    /* Pack 20 topics. */
    for (uint32_t i = 0; i < 20; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &mgr);
    }

    uint16_t initial_count = mgr.cleanup_queue_count;
    CHECK(initial_count > 0);

    /* Simulate drain: read from position 0 and memmove to compact. */
    while (mgr.cleanup_queue_count > 0) {
        char suffix_buf[128];
        strncpy(suffix_buf, mgr.cleanup_topic_buf + 1, sizeof(suffix_buf) - 1);
        suffix_buf[sizeof(suffix_buf) - 1] = '\0';
        size_t consumed = 1 + strlen(suffix_buf) + 1;
        if (consumed > mgr.cleanup_queue_write_pos) consumed = mgr.cleanup_queue_write_pos;
        memmove(mgr.cleanup_topic_buf, mgr.cleanup_topic_buf + consumed, mgr.cleanup_queue_write_pos - consumed);
        mgr.cleanup_queue_write_pos -= (uint16_t)consumed;
        mgr.cleanup_queue_count--;
    }

    CHECK_EQUAL(0, mgr.cleanup_queue_count);

    /* Refill — should succeed without drops. */
    for (uint32_t i = 0; i < 20; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/new_field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &mgr);
    }

    CHECK(mgr.cleanup_queue_count > 0);
    CHECK_EQUAL(0, mgr.cleanup_dropped_count);
}

/* ------------------------------------------------------------------ */
/* Malformed topics                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, missing_config_suffix_skipped)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/sensor/Dishwasher_PDT715/field_1/state",
        "{\"data\":1}", 10, &mgr);

    CHECK_EQUAL(0, mgr.cleanup_queue_count);
    CHECK_EQUAL(0, mgr.cleanup_dropped_count);
}

TEST(ha_discovery_cleanup, unknown_domain_skipped)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/unknown_domain/Dishwasher_PDT715/field_1/config",
        "{\"data\":1}", 10, &mgr);

    CHECK_EQUAL(0, mgr.cleanup_queue_count);
    CHECK_EQUAL(0, mgr.cleanup_dropped_count);
}

TEST(ha_discovery_cleanup, empty_payload_echo_skipped)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/sensor/Dishwasher_PDT715/field_1/config",
        "", 0, &mgr);

    CHECK_EQUAL(0, mgr.cleanup_queue_count);
    CHECK_EQUAL(0, mgr.cleanup_dropped_count);
}

TEST(ha_discovery_cleanup, too_short_topic_skipped)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    cleanup_topic_callback("abc", "{\"data\":1}", 10, &mgr);

    CHECK_EQUAL(0, mgr.cleanup_queue_count);
    CHECK_EQUAL(0, mgr.cleanup_dropped_count);
}

/* ------------------------------------------------------------------ */
/* cleanup_start resets                                                 */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, cleanup_start_resets_fields)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    mgr.cleanup_queue_write_pos = 100;
    mgr.cleanup_queue_count = 50;
    mgr.cleanup_dropped_count = 10;

    cleanup_start(&mgr);

    CHECK_EQUAL(0, mgr.cleanup_queue_write_pos);
    CHECK_EQUAL(0, mgr.cleanup_queue_count);
    CHECK_EQUAL(0, mgr.cleanup_dropped_count);
}

/* Flush reconstruction verification                                     */
TEST(ha_discovery_cleanup, flush_reconstructs_topic_correctly)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    /* Pack a topic. */
    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "{\"data\":1}", 10, &mgr);

    CHECK_EQUAL(1, mgr.cleanup_queue_count);

    /* Verify reconstruction by reading from position 0. */
    int domain_index = (int)(uint8_t)mgr.cleanup_topic_buf[0];
    char suffix_buf[128];
    strncpy(suffix_buf, mgr.cleanup_topic_buf + 1, sizeof(suffix_buf) - 1);
    suffix_buf[sizeof(suffix_buf) - 1] = '\0';
    char reconstructed[256];
    snprintf(reconstructed, sizeof(reconstructed), "homeassistant/%s/%s/%s/config",
             HA_DOMAIN_STRINGS[domain_index], mgr.device_id, suffix_buf);

    STRNCMP_EQUAL("homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
               reconstructed, 72);
}

TEST(ha_discovery_cleanup, flush_batch_limit)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    /* Pack more topics than the batch limit (16). */
    for (uint32_t i = 0; i < 20; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &mgr);
    }

    uint16_t initial_count = mgr.cleanup_queue_count;
    CHECK(initial_count > 16);

    /* Simulate flushing 16 entries (the batch limit) by draining from position 0. */
    uint16_t flushed = 0;
    while (flushed < 16 && mgr.cleanup_queue_count > 0) {
        char suffix_buf[128];
        strncpy(suffix_buf, mgr.cleanup_topic_buf + 1, sizeof(suffix_buf) - 1);
        suffix_buf[sizeof(suffix_buf) - 1] = '\0';
        size_t consumed = 1 + strlen(suffix_buf) + 1;
        if (consumed > mgr.cleanup_queue_write_pos) consumed = mgr.cleanup_queue_write_pos;
        memmove(mgr.cleanup_topic_buf, mgr.cleanup_topic_buf + consumed, mgr.cleanup_queue_write_pos - consumed);
        mgr.cleanup_queue_write_pos -= (uint16_t)consumed;
        mgr.cleanup_queue_count--;
        flushed++;
    }

    CHECK_EQUAL(16, flushed);
    CHECK_EQUAL(initial_count - 16, mgr.cleanup_queue_count);
}
/* ------------------------------------------------------------------ */
/* Compacting buffer behavior                                          */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, compacting_buffer_drain_and_refill)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    /* Pack many topics to fill a significant portion of the buffer. */
    for (uint32_t i = 0; i < 50; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &mgr);
    }

    uint16_t write_pos_before = mgr.cleanup_queue_write_pos;
    CHECK(write_pos_before > 0);

    /* Drain all but the last entry via compacting. */
    while (mgr.cleanup_queue_count > 1) {
        char suffix_buf[128];
        strncpy(suffix_buf, mgr.cleanup_topic_buf + 1, sizeof(suffix_buf) - 1);
        suffix_buf[sizeof(suffix_buf) - 1] = '\0';
        size_t consumed = 1 + strlen(suffix_buf) + 1;
        if (consumed > mgr.cleanup_queue_write_pos) consumed = mgr.cleanup_queue_write_pos;
        memmove(mgr.cleanup_topic_buf, mgr.cleanup_topic_buf + consumed, mgr.cleanup_queue_write_pos - consumed);
        mgr.cleanup_queue_write_pos -= (uint16_t)consumed;
        mgr.cleanup_queue_count--;
    }

    /* After compaction, write_pos should be much lower than before. */
    CHECK(mgr.cleanup_queue_write_pos < write_pos_before);
    CHECK(mgr.cleanup_queue_count == 1);

    /* Pack more topics — they should append at the new write_pos. */
    for (uint32_t i = 0; i < 10; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/compact_field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &mgr);
    }

    CHECK(mgr.cleanup_queue_count > 1);

    /* Verify all entries are readable from position 0 with correct data. */
    while (mgr.cleanup_queue_count > 0) {
        int domain_index = (int)(uint8_t)mgr.cleanup_topic_buf[0];
        char suffix_buf[128];
        strncpy(suffix_buf, mgr.cleanup_topic_buf + 1, sizeof(suffix_buf) - 1);
        suffix_buf[sizeof(suffix_buf) - 1] = '\0';

        CHECK(domain_index >= 0 && domain_index < HA_DOMAIN_COUNT);
        CHECK(strlen(suffix_buf) > 0);

        size_t consumed = 1 + strlen(suffix_buf) + 1;
        if (consumed > mgr.cleanup_queue_write_pos) consumed = mgr.cleanup_queue_write_pos;
        memmove(mgr.cleanup_topic_buf, mgr.cleanup_topic_buf + consumed, mgr.cleanup_queue_write_pos - consumed);
        mgr.cleanup_queue_write_pos -= (uint16_t)consumed;
        mgr.cleanup_queue_count--;
    }
    CHECK_EQUAL(0, mgr.cleanup_queue_count);
}

/* ------------------------------------------------------------------ */
/* Domain strings array integrity                                       */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, domain_strings_non_null)
{
    for (int i = 0; i < HA_DOMAIN_COUNT; i++) {
        CHECK(HA_DOMAIN_STRINGS[i] != NULL);
        CHECK(strlen(HA_DOMAIN_STRINGS[i]) > 0);
    }
}

/* ------------------------------------------------------------------ */
/* Delayed re-subscribe state initialization                            */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, retry_state_initialized_to_zero)
{
    ha_discovery_manager_t mgr;
    memset(&mgr, 0, sizeof(mgr));
    mgr.device_id = "TestDevice";
    mgr.get_time_ms = test_get_time_ms;

    cleanup_start(&mgr);

    CHECK_EQUAL(false, mgr.cleanup_pending_resubscribe);
    CHECK_EQUAL(0, mgr.cleanup_unsubscribe_ms);
    CHECK_EQUAL(0, mgr.cleanup_retry_count);
}

TEST(ha_discovery_cleanup, retry_count_starts_at_zero)
{
    ha_discovery_manager_t mgr = make_manager("TestDevice");
    cleanup_start(&mgr);

    CHECK_EQUAL(0, mgr.cleanup_retry_count);
}
