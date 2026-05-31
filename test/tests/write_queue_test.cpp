/*
 * @file
 * @brief Unit tests for the WriteQueue class.
 *
 * Validates push/pop, FIFO ordering, full/empty conditions,
 * and the caller-retry pattern when the queue is full.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "write_queue.h"
#include <cstring>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(write_queue)
{
  WriteQueue queue;

  void setup()
  {
  }

  void teardown()
  {
  }

  WriteCommand make_cmd(tiny_erd_t erd, uint8_t addr, uint8_t val)
  {
    WriteCommand cmd;
    cmd.erd_id = erd;
    cmd.value[0] = val;
    cmd.value_size = 1;
    cmd.appliance_address = addr;
    return cmd;
  }
};

/* ------------------------------------------------------------------ */
/* Empty queue behaviour                                                */
/* ------------------------------------------------------------------ */

TEST(write_queue, starts_empty)
{
  CHECK(queue.is_empty());
  CHECK_EQUAL(0, queue.size());
}

TEST(write_queue, pop_from_empty_returns_false)
{
  WriteCommand cmd;
  CHECK(!queue.pop(cmd));
}

/* ------------------------------------------------------------------ */
/* Push and pop — basic FIFO                                           */
/* ------------------------------------------------------------------ */

TEST(write_queue, push_then_pop)
{
  WriteCommand cmd = make_cmd(0x1001, 0xC0, 0xAA);
  CHECK(queue.push(cmd));

  WriteCommand out;
  CHECK(queue.pop(out));

  CHECK_EQUAL(0x1001, out.erd_id);
  CHECK_EQUAL(0xC0, out.appliance_address);
  CHECK_EQUAL(1, out.value_size);
  CHECK_EQUAL(0xAA, out.value[0]);
}

TEST(write_queue, fifo_ordering)
{
  WriteCommand cmd1 = make_cmd(0x1001, 0xC0, 0x01);
  WriteCommand cmd2 = make_cmd(0x1002, 0xC0, 0x02);
  WriteCommand cmd3 = make_cmd(0x1003, 0xC0, 0x03);

  CHECK(queue.push(cmd1));
  CHECK(queue.push(cmd2));
  CHECK(queue.push(cmd3));

  CHECK_EQUAL(3, queue.size());

  WriteCommand out;
  queue.pop(out); CHECK_EQUAL(0x1001, out.erd_id);
  queue.pop(out); CHECK_EQUAL(0x1002, out.erd_id);
  queue.pop(out); CHECK_EQUAL(0x1003, out.erd_id);

  CHECK(queue.is_empty());
}

/* ------------------------------------------------------------------ */
/* Queue full — push returns false                                     */
/* ------------------------------------------------------------------ */

TEST(write_queue, push_returns_false_when_full)
{
  // Fill the queue
  for (size_t i = 0; i < MAX_PENDING_WRITES; i++) {
    WriteCommand cmd = make_cmd(static_cast<tiny_erd_t>(0x2000 + i), 0xC0, static_cast<uint8_t>(i));
    CHECK(queue.push(cmd));
  }

  CHECK_EQUAL(0, static_cast<int>(queue.size()) - static_cast<int>(MAX_PENDING_WRITES));
  CHECK(!queue.is_empty());

  // Next push should fail
  WriteCommand cmd = make_cmd(0xFFFF, 0xFF, 0xFF);
  CHECK(!queue.push(cmd));
  CHECK_EQUAL(MAX_PENDING_WRITES, queue.size());
}

/* ------------------------------------------------------------------ */
/* Wrap-around — push/pop cycles beyond buffer size                    */
/* ------------------------------------------------------------------ */

TEST(write_queue, wrap_around_after_pop_and_push)
{
  // Fill the queue
  for (size_t i = 0; i < MAX_PENDING_WRITES; i++) {
    WriteCommand cmd = make_cmd(static_cast<tiny_erd_t>(0x3000 + i), 0xC0, static_cast<uint8_t>(i));
    CHECK(queue.push(cmd));
  }
  CHECK(!queue.push(make_cmd(0xFFFF, 0xFF, 0xFF)));

  // Pop all
  WriteCommand out;
  for (size_t i = 0; i < MAX_PENDING_WRITES; i++) {
    CHECK(queue.pop(out));
    CHECK_EQUAL(static_cast<tiny_erd_t>(0x3000 + i), out.erd_id);
  }
  CHECK(queue.is_empty());

  // Push again — should work after wrapping
  WriteCommand cmd = make_cmd(0x4001, 0xC0, 0x42);
  CHECK(queue.push(cmd));
  CHECK_EQUAL(1, queue.size());

  CHECK(queue.pop(out));
  CHECK_EQUAL(0x4001, out.erd_id);
}

/* ------------------------------------------------------------------ */
/* Size tracking                                                       */
/* ------------------------------------------------------------------ */

TEST(write_queue, size_increases_and_decreases)
{
  CHECK_EQUAL(0, queue.size());

  WriteCommand cmd1 = make_cmd(0x5001, 0xC0, 0x01);
  WriteCommand cmd2 = make_cmd(0x5002, 0xC0, 0x02);

  CHECK(queue.push(cmd1));
  CHECK_EQUAL(1, queue.size());

  CHECK(queue.push(cmd2));
  CHECK_EQUAL(2, queue.size());

  WriteCommand out;
  CHECK(queue.pop(out));
  CHECK_EQUAL(1, queue.size());

  CHECK(queue.pop(out));
  CHECK_EQUAL(0, queue.size());
}

/* ------------------------------------------------------------------ */
/* Multiple value bytes                                                */
/* ------------------------------------------------------------------ */

TEST(write_queue, preserves_value_size_and_data)
{
  WriteCommand cmd;
  cmd.erd_id = 0x6001;
  cmd.value[0] = 0xDE;
  cmd.value[1] = 0xAD;
  cmd.value[2] = 0xBE;
  cmd.value[3] = 0xEF;
  cmd.value_size = 4;
  cmd.appliance_address = 0xC0;

  CHECK(queue.push(cmd));

  WriteCommand out;
  CHECK(queue.pop(out));

  CHECK_EQUAL(0x6001, out.erd_id);
  CHECK_EQUAL(4, out.value_size);
  CHECK_EQUAL(0xDE, out.value[0]);
  CHECK_EQUAL(0xAD, out.value[1]);
  CHECK_EQUAL(0xBE, out.value[2]);
  CHECK_EQUAL(0xEF, out.value[3]);
}

/* ------------------------------------------------------------------ */
/* Caller retry pattern                                                */
/* ------------------------------------------------------------------ */

TEST(write_queue, caller_can_retry_after_pop_frees_space)
{
  // Fill the queue
  for (size_t i = 0; i < MAX_PENDING_WRITES; i++) {
    WriteCommand cmd = make_cmd(static_cast<tiny_erd_t>(0x7000 + i), 0xC0, static_cast<uint8_t>(i));
    CHECK(queue.push(cmd));
  }

  // Push fails — caller retries later
  WriteCommand retry_cmd = make_cmd(0x7FFF, 0xC0, 0xFF);
  CHECK(!queue.push(retry_cmd));

  // Pop one to free space
  WriteCommand out;
  CHECK(queue.pop(out));

  // Retry succeeds
  CHECK(queue.push(retry_cmd));
  CHECK_EQUAL(MAX_PENDING_WRITES, queue.size());
}
