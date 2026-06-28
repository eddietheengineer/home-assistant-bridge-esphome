/*!
 * @file
 * @brief Unit tests for Home Assistant MQTT Discovery cleanup.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#ifdef new
#undef new
#endif

#include "ha_discovery_manager.h"
#include "double/mqtt_client_double.hpp"

static uint32_t test_time_ms = 0;
static uint32_t test_get_time_ms(void) { return test_time_ms; }

static mqtt_client_double_t test_mqtt;

static ha_discovery_manager_t make_manager(const char* device_id)
{
    ha_discovery_manager_t mgr;
    memset(&mgr, 0, sizeof(mgr));
    mgr.device_id = device_id;
    mgr.get_time_ms = test_get_time_ms;
    mgr.state = ha_discovery_state_cleaning;
    mgr.mqtt_client = &test_mqtt.interface;
    mgr.cleanup_yield_counter = 0;
    mgr.cleanup_pass_found_topics = false;
    mgr.cleanup_last_activity_ms = 0;
    return mgr;
}

TEST_GROUP(ha_discovery_cleanup)
{
    void setup()
    {
        mock().clear();
        mqtt_client_double_init(&test_mqtt);
    }
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
/* Direct publish via callback                                          */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, callback_publishes_empty_retained)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    mock().expectOneCall("publish_raw")
        .withParameter("topic",
            "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config")
        .withStringParameter("payload", "")
        .withParameter("payload_len", 0)
        .withParameter("retain", true);

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "{\"data\":1}", 10, &mgr);

    CHECK(mgr.cleanup_pass_found_topics);
    CHECK_EQUAL(1, mgr.cleanup_pass_received_count);
    CHECK_EQUAL(1, mgr.cleanup_pass_removed_count);
}

TEST(ha_discovery_cleanup, callback_publishes_multiple_topics)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    for (int i = 0; i < 5; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
            "homeassistant/sensor/Dishwasher_PDT715/field_%d/config", i);
        mock().expectOneCall("publish_raw")
            .withParameter("topic", topic)
            .withStringParameter("payload", "")
            .withParameter("payload_len", 0)
            .withParameter("retain", true);

        cleanup_topic_callback(topic, "{\"data\":1}", 10, &mgr);
    }

    CHECK_EQUAL(5, mgr.cleanup_pass_received_count);
    CHECK_EQUAL(5, mgr.cleanup_pass_removed_count);
}

/* ------------------------------------------------------------------ */
/* Malformed topic handling                                             */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, missing_config_suffix_skipped)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    mock().expectNoCall("publish_raw");

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/state",
        "{\"data\":1}", 10, &mgr);

    CHECK(!mgr.cleanup_pass_found_topics);
    CHECK_EQUAL(0, mgr.cleanup_pass_removed_count);
}

TEST(ha_discovery_cleanup, empty_payload_echo_skipped)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    mock().expectNoCall("publish_raw");

    cleanup_topic_callback(
        "homeassistant/binary_sensor/Dishwasher_PDT715/3218_washzones/config",
        "", 0, &mgr);

    CHECK(!mgr.cleanup_pass_found_topics);
    CHECK_EQUAL(0, mgr.cleanup_pass_removed_count);
}

TEST(ha_discovery_cleanup, too_short_topic_skipped)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");

    mock().expectNoCall("publish_raw");

    cleanup_topic_callback("abc", "{\"data\":1}", 10, &mgr);

    CHECK(!mgr.cleanup_pass_found_topics);
    CHECK_EQUAL(0, mgr.cleanup_pass_removed_count);
}

/* ------------------------------------------------------------------ */
/* cleanup_start resets                                                 */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_cleanup, cleanup_start_resets_fields)
{
    ha_discovery_manager_t mgr = make_manager("Dishwasher_PDT715");
    mgr.cleanup_yield_counter = 3;
    mgr.cleanup_clean_passes = 5;
    mgr.cleanup_pass_found_topics = true;
    mgr.cleanup_pass_received_count = 100;
    mgr.cleanup_pass_removed_count = 50;
    mgr.cleanup_current_domain = 10;
    mgr.cleanup_pass_number = 3;

    cleanup_start(&mgr);

    CHECK_EQUAL(0, mgr.cleanup_yield_counter);
    CHECK_EQUAL(0, mgr.cleanup_clean_passes);
    CHECK(!mgr.cleanup_pass_found_topics);
    CHECK_EQUAL(0, mgr.cleanup_pass_received_count);
    CHECK_EQUAL(0, mgr.cleanup_pass_removed_count);
    CHECK_EQUAL(0, mgr.cleanup_current_domain);
    CHECK_EQUAL(1, mgr.cleanup_pass_number);
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
