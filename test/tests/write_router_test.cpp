/*
 * @file
 * @brief Unit tests for the WriteRouter class.
 *
 * Validates event subscription, WriteCommand construction, queue push,
 * and graceful handling of a full queue.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "write_router.h"
#include "double/mqtt_client_double.hpp"
#include <cstring>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(write_router)
{
  WriteQueue queue;
  mqtt_client_double_t mqtt;
  WriteRouter* router;

  void setup()
  {
    mqtt_client_double_init(&mqtt);
    router = nullptr;
  }

  void teardown()
  {
    delete router;
    router = nullptr;
  }

  // Construct with nullptr mqtt_client to skip real subscription
  // (we drive the event manually via the double)
  void construct_with_null_mqtt()
  {
    router = new WriteRouter(nullptr, &queue);
  }

  // Construct with real mqtt double — subscribes to on_write_request
  void construct_with_mqtt()
  {
    router = new WriteRouter(&mqtt.interface, &queue);
  }
};

/* ------------------------------------------------------------------ */
/* Construction with null mqtt_client — no crash                        */
/* ------------------------------------------------------------------ */

TEST(write_router, construct_with_null_mqtt_no_crash)
{
  construct_with_null_mqtt();
  // If we got here without crashing, the null guard worked
  CHECK(!queue.is_empty() || queue.is_empty());  // tautology to keep compiler happy
}

/* ------------------------------------------------------------------ */
/* Construction with real mqtt_client — subscribes                      */
/* ------------------------------------------------------------------ */

TEST(write_router, construct_with_mqtt_subscribes)
{
  construct_with_mqtt();
  // Trigger a write request — should arrive at the router's handler
  uint8_t value = 0x42;
  mqtt_client_double_trigger_write_request(&mqtt, 0x1001, 1, &value);

  CHECK(!queue.is_empty());
  CHECK_EQUAL(1, queue.size());
}

/* ------------------------------------------------------------------ */
/* WriteCommand fields are correctly populated                          */
/* ------------------------------------------------------------------ */

TEST(write_router, populates_erd_id)
{
  construct_with_mqtt();
  uint8_t value = 0xFF;
  mqtt_client_double_trigger_write_request(&mqtt, 0x1234, 1, &value);

  WriteCommand cmd;
  CHECK(queue.pop(cmd));
  CHECK_EQUAL(0x1234, cmd.erd_id);
}

TEST(write_router, populates_value_and_size)
{
  construct_with_mqtt();
  uint8_t value[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  mqtt_client_double_trigger_write_request(&mqtt, 0x1001, 4, value);

  WriteCommand cmd;
  CHECK(queue.pop(cmd));
  CHECK_EQUAL(4, cmd.value_size);
  CHECK_EQUAL(0xDE, cmd.value[0]);
  CHECK_EQUAL(0xAD, cmd.value[1]);
  CHECK_EQUAL(0xBE, cmd.value[2]);
  CHECK_EQUAL(0xEF, cmd.value[3]);
}

TEST(write_router, populates_appliance_address)
{
  construct_with_mqtt();
  uint8_t value = 0x01;
  mqtt_client_double_trigger_write_request(&mqtt, 0x1001, 1, &value);

  WriteCommand cmd;
  CHECK(queue.pop(cmd));
  CHECK_EQUAL(0, cmd.appliance_address);  // 0 = use WriteHandler's configured address
}

/* ------------------------------------------------------------------ */
/* Multiple write requests queued in order                              */
/* ------------------------------------------------------------------ */

TEST(write_router, queues_multiple_writes_in_order)
{
  construct_with_mqtt();

  uint8_t v1 = 0x11;
  uint8_t v2 = 0x22;
  uint8_t v3 = 0x33;

  mqtt_client_double_trigger_write_request(&mqtt, 0x1001, 1, &v1);
  mqtt_client_double_trigger_write_request(&mqtt, 0x1002, 1, &v2);
  mqtt_client_double_trigger_write_request(&mqtt, 0x1003, 1, &v3);

  CHECK_EQUAL(3, queue.size());

  WriteCommand cmd;
  queue.pop(cmd); CHECK_EQUAL(0x1001, cmd.erd_id); CHECK_EQUAL(0x11, cmd.value[0]);
  queue.pop(cmd); CHECK_EQUAL(0x1002, cmd.erd_id); CHECK_EQUAL(0x22, cmd.value[0]);
  queue.pop(cmd); CHECK_EQUAL(0x1003, cmd.erd_id); CHECK_EQUAL(0x33, cmd.value[0]);
}

/* ------------------------------------------------------------------ */
/* Full queue — command is discarded with a warning                     */
/* ------------------------------------------------------------------ */

TEST(write_router, discards_when_queue_full)
{
  construct_with_mqtt();

  // Fill the queue
  for (size_t i = 0; i < MAX_PENDING_WRITES; i++) {
    uint8_t v = static_cast<uint8_t>(i);
    mqtt_client_double_trigger_write_request(&mqtt, static_cast<tiny_erd_t>(0x2000 + i), 1, &v);
  }

  CHECK_EQUAL(MAX_PENDING_WRITES, queue.size());

  // Next write request should be discarded (queue full)
  uint8_t v = 0xFF;
  mqtt_client_double_trigger_write_request(&mqtt, 0xFFFF, 1, &v);

  // Queue size unchanged — command was discarded
  CHECK_EQUAL(MAX_PENDING_WRITES, queue.size());
}

/* ------------------------------------------------------------------ */
/* Zero-size value handled                                              */
/* ------------------------------------------------------------------ */

TEST(write_router, handles_zero_size_value)
{
  construct_with_mqtt();
  mqtt_client_double_trigger_write_request(&mqtt, 0x1001, 0, nullptr);

  WriteCommand cmd;
  CHECK(queue.pop(cmd));
  CHECK_EQUAL(0x1001, cmd.erd_id);
  CHECK_EQUAL(0, cmd.value_size);
}

/* ------------------------------------------------------------------ */
/* Null value pointer handled                                           */
/* ------------------------------------------------------------------ */

TEST(write_router, handles_null_value_pointer)
{
  construct_with_mqtt();
  mqtt_client_double_trigger_write_request(&mqtt, 0x1001, 3, nullptr);

  WriteCommand cmd;
  CHECK(queue.pop(cmd));
  CHECK_EQUAL(0x1001, cmd.erd_id);
  CHECK_EQUAL(3, cmd.value_size);
  // Value bytes should be zeroed since input was null
  CHECK_EQUAL(0, cmd.value[0]);
  CHECK_EQUAL(0, cmd.value[1]);
  CHECK_EQUAL(0, cmd.value[2]);
}

/* ------------------------------------------------------------------ */
/* Destructor unsubscribes — no crash on publish after destruction      */
/* ------------------------------------------------------------------ */

TEST(write_router, destructor_unsubscribes)
{
  // Create router with real mqtt double
  WriteRouter* r = new WriteRouter(&mqtt.interface, &queue);

  // Trigger one write
  uint8_t v = 0xAA;
  mqtt_client_double_trigger_write_request(&mqtt, 0x1001, 1, &v);
  CHECK_EQUAL(1, queue.size());

  // Destroy the router — should unsubscribe
  delete r;

  // Trigger another write — should NOT reach the queue (no subscriber)
  uint8_t v2 = 0xBB;
  mqtt_client_double_trigger_write_request(&mqtt, 0x1002, 1, &v2);
  CHECK_EQUAL(1, queue.size());  // Still 1 — second write was not received
}

/* ------------------------------------------------------------------ */
/* Large value clamped to MAX_ERD_VALUE_SIZE                            */
/* ------------------------------------------------------------------ */

TEST(write_router, clamps_value_to_max_size)
{
  construct_with_mqtt();

  // Provide more bytes than MAX_ERD_VALUE_SIZE
  uint8_t big_value[64];
  for (int i = 0; i < 64; i++) {
    big_value[i] = static_cast<uint8_t>(i);
  }
  mqtt_client_double_trigger_write_request(&mqtt, 0x1001, 64, big_value);

  WriteCommand cmd;
  CHECK(queue.pop(cmd));
  CHECK_EQUAL(MAX_ERD_VALUE_SIZE, cmd.value_size);  // clamped to max
  // Only MAX_ERD_VALUE_SIZE bytes were actually copied
  CHECK_EQUAL(0x1F, cmd.value[MAX_ERD_VALUE_SIZE - 1]);
}
