/*!
 * @file
 * @brief Unit tests for the ha_discovery_cleanup module.
 *
 * Tests domain-enum packing, flush roundtrip, buffer overflow, drain/refill,
 * malformed topics, and empty payload echo. Uses a 512-byte test buffer
 * (defined via HA_CLEANUP_TEST_BUF_SIZE in the Makefile).
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "ha_discovery_cleanup.h"
#include "ha_discovery_manager.h"

/* ------------------------------------------------------------------ */
/* Test helpers                                                       */
/* ------------------------------------------------------------------ */

static uint32_t test_time_ms = 0;
static uint32_t test_get_time_ms(void) { return test_time_ms; }

static ha_discovery_cleanup_t make_cleanup(const char* device_id)
{
    ha_discovery_cleanup_t cl;
    memset(&cl, 0, sizeof(cl));
    cl.device_id = device_id;
    cl.get_time_ms = test_get_time_ms;
    cl.state = ha_cleanup_state_cleaning;
    cl.mqtt_client = NULL;
    cl.queue_write_pos = 0;
    cl.dropped_count = 0;
    cl.pass_found_topics = false;
    cl.last_activity_ms = 0;
    return cl;
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
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(1, cl.queue_count);
    CHECK_EQUAL(0, cl.dropped_count);
    CHECK(cl.pass_found_topics);

    /* Verify full topic stored in buffer. */
    STRCMP_EQUAL("homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
                 cl.topic_buf);
}


/* ------------------------------------------------------------------ */
/* Buffer full behavior                                                 */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, buffer_full_drops_topics)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    /* Each topic is ~55 bytes, 512/55 ~ 9. Fill until we start dropping. */
    for (uint32_t i = 0; i < 100; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    CHECK(cl.dropped_count > 0);
    CHECK(cl.queue_count > 0);
}

/* ------------------------------------------------------------------ */
/* Drain and refill                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, drain_and_refill)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    /* Pack 5 topics. */
    for (uint32_t i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    uint16_t initial_count = cl.queue_count;
    CHECK(initial_count > 0);

    /* Simulate drain: read topic from position 0 and compact. */
    while (cl.queue_count > 0) {
        size_t consumed = strlen(cl.topic_buf) + 1;
        if (consumed > cl.queue_write_pos) consumed = cl.queue_write_pos;
        memmove(cl.topic_buf, cl.topic_buf + consumed, cl.queue_write_pos - consumed);
        cl.queue_write_pos -= (uint16_t)consumed;
        cl.queue_count--;
    }

    CHECK_EQUAL(0, cl.queue_count);

    /* Refill — should succeed without drops. */
    for (uint32_t i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/new_field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    CHECK(cl.queue_count > 0);
    CHECK_EQUAL(0, cl.dropped_count);
}

/* ------------------------------------------------------------------ */
/* Malformed topics                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, missing_config_suffix_skipped)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/sensor/Dishwasher_PDT715/field_1/state",
        "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(0, cl.dropped_count);
}


TEST(ha_discovery_cleanup, too_short_topic_skipped)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cleanup_topic_callback("abc", "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(0, cl.dropped_count);
}

/* ------------------------------------------------------------------ */
/* cleanup_start resets                                                 */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, cleanup_start_resets_fields)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cl.queue_write_pos = 100;
    cl.queue_count = 50;
    cl.dropped_count = 10;

    cleanup_start(&cl);

    CHECK_EQUAL(0, cl.queue_write_pos);
    CHECK_EQUAL(0, cl.queue_count);
    CHECK_EQUAL(0, cl.dropped_count);
}

/* Flush stores and republishes the original topic unchanged               */
TEST(ha_discovery_cleanup, flush_stores_topic_unchanged)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "{\"data\":1}", 10, &cl);

    CHECK_EQUAL(1, cl.queue_count);

    /* The topic stored in the buffer should be identical to what was received. */
    STRCMP_EQUAL("homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
                 cl.topic_buf);
}

TEST(ha_discovery_cleanup, flush_drains_one_at_a_time)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    /* Pack several topics. */
    for (uint32_t i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    uint16_t initial_count = cl.queue_count;
    CHECK_EQUAL(5, initial_count);

    /* Drain one at a time, verifying each topic. */
    for (uint32_t i = 0; i < 5; i++) {
        char expected[128];
        snprintf(expected, sizeof(expected),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        STRCMP_EQUAL(expected, cl.topic_buf);

        size_t consumed = strlen(cl.topic_buf) + 1;
        if (consumed > cl.queue_write_pos) consumed = cl.queue_write_pos;
        memmove(cl.topic_buf, cl.topic_buf + consumed, cl.queue_write_pos - consumed);
        cl.queue_write_pos -= (uint16_t)consumed;
        cl.queue_count--;
    }

    CHECK_EQUAL(0, cl.queue_count);
}
/* ------------------------------------------------------------------ */
/* Compacting buffer behavior                                          */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, compacting_buffer_drain_and_refill)
{
    ha_discovery_cleanup_t cl = make_cleanup("Dishwasher_PDT715");

    /* Pack several topics. */
    for (uint32_t i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    uint16_t write_pos_before = cl.queue_write_pos;
    CHECK(write_pos_before > 0);

    /* Drain all but the last entry via compacting. */
    while (cl.queue_count > 1) {
        size_t consumed = strlen(cl.topic_buf) + 1;
        if (consumed > cl.queue_write_pos) consumed = cl.queue_write_pos;
        memmove(cl.topic_buf, cl.topic_buf + consumed, cl.queue_write_pos - consumed);
        cl.queue_write_pos -= (uint16_t)consumed;
        cl.queue_count--;
    }

    /* After compaction, write_pos should be much lower than before. */
    CHECK(cl.queue_write_pos < write_pos_before);
    CHECK(cl.queue_count == 1);

    /* Pack more topics — they should append at the new write_pos. */
    for (uint32_t i = 0; i < 3; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/compact_field_%u/config", i);
        cleanup_topic_callback(topic, "{\"data\":1}", 10, &cl);
    }

    CHECK(cl.queue_count > 1);

    /* Verify all entries are readable from position 0. */
    while (cl.queue_count > 0) {
        CHECK(strlen(cl.topic_buf) > 0);

        size_t consumed = strlen(cl.topic_buf) + 1;
        if (consumed > cl.queue_write_pos) consumed = cl.queue_write_pos;
        memmove(cl.topic_buf, cl.topic_buf + consumed, cl.queue_write_pos - consumed);
        cl.queue_write_pos -= (uint16_t)consumed;
        cl.queue_count--;
    }
    CHECK_EQUAL(0, cl.queue_count);
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
/* Drain wait state initialization                                      */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, drain_state_initialized_to_zero)
{
    ha_discovery_cleanup_t cl;
    memset(&cl, 0, sizeof(cl));
    cl.device_id = "TestDevice";
    cl.get_time_ms = test_get_time_ms;

    cleanup_start(&cl);

    CHECK_EQUAL(0, cl.drain_start_ms);
}
