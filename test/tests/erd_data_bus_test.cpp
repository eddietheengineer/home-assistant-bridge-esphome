/*!
 * @file
 * @brief Unit tests for ErdDataBus.
 *
 * Validates write/read, dirty tracking, mark_all_dirty, for_each_dirty,
 * clear_dirty, and per-ERD change notification subscriptions.
 */

/* Include STL headers before CppUTest to avoid the operator-new macro clash */
#include <map>
#include <vector>
#include <string>
#include <functional>

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "erd_data_bus.h"

using namespace esphome::geappliances_bridge;

// ---------------------------------------------------------------------------
// Helper: capture change callbacks in a vector for later inspection
// ---------------------------------------------------------------------------

struct CapturedChange {
  tiny_erd_t erd;
  std::vector<uint8_t> data;
};

static void capture_change(void* context, const void* args)
{
  auto* vec = static_cast<std::vector<CapturedChange>*>(context);
  auto* a = static_cast<const ErdChangedArgs*>(args);
  CapturedChange c;
  c.erd = a->erd;
  c.data.assign(
    static_cast<const uint8_t*>(a->data),
    static_cast<const uint8_t*>(a->data) + a->size);
  vec->push_back(c);
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TEST_GROUP(erd_data_bus)
{
  ErdDataBus bus;

  void setup() {}
  void teardown() { mock().clear(); }
};

// ── write / read ─────────────────────────────────────────────────────────────

TEST(erd_data_bus, write_and_read_single_byte)
{
  uint8_t value = 0x42;
  bus.write(0x0100, &value, sizeof(value));

  uint8_t out = 0;
  CHECK_TRUE(bus.read(0x0100, &out, sizeof(out)));
  CHECK_EQUAL(0x42, out);
}

TEST(erd_data_bus, read_returns_false_for_unknown_erd)
{
  uint8_t out = 0;
  CHECK_FALSE(bus.read(0x9999, &out, sizeof(out)));
}

TEST(erd_data_bus, read_returns_false_for_wrong_size)
{
  uint16_t value = 0x1234;
  bus.write(0x0200, &value, sizeof(value));

  uint8_t out = 0;
  CHECK_FALSE(bus.read(0x0200, &out, sizeof(out)));  // wrong size
}

TEST(erd_data_bus, write_overwrites_previous_value)
{
  uint8_t v1 = 0xAA;
  bus.write(0x0300, &v1, sizeof(v1));

  uint8_t v2 = 0xBB;
  bus.write(0x0300, &v2, sizeof(v2));

  uint8_t out = 0;
  CHECK_TRUE(bus.read(0x0300, &out, sizeof(out)));
  CHECK_EQUAL(0xBB, out);
}

TEST(erd_data_bus, write_multi_byte_erd)
{
  uint8_t value[4] = {0x01, 0x02, 0x03, 0x04};
  bus.write(0x0400, value, sizeof(value));

  uint8_t out[4] = {};
  CHECK_TRUE(bus.read(0x0400, out, sizeof(out)));
  CHECK_EQUAL(0x01, out[0]);
  CHECK_EQUAL(0x02, out[1]);
  CHECK_EQUAL(0x03, out[2]);
  CHECK_EQUAL(0x04, out[3]);
}

// ── contains / data_size ─────────────────────────────────────────────────────

TEST(erd_data_bus, contains_returns_false_before_write)
{
  CHECK_FALSE(bus.contains(0x0500));
}

TEST(erd_data_bus, contains_returns_true_after_write)
{
  uint8_t v = 1;
  bus.write(0x0500, &v, 1);
  CHECK_TRUE(bus.contains(0x0500));
}

TEST(erd_data_bus, data_size_returns_zero_for_unknown_erd)
{
  CHECK_EQUAL(0, bus.data_size(0x0600));
}

TEST(erd_data_bus, data_size_returns_correct_size)
{
  uint8_t v[3] = {1, 2, 3};
  bus.write(0x0600, v, sizeof(v));
  CHECK_EQUAL(3, bus.data_size(0x0600));
}

// ── dirty flag ───────────────────────────────────────────────────────────────

TEST(erd_data_bus, write_marks_erd_dirty)
{
  uint8_t v = 1;
  bus.write(0x0700, &v, 1);
  CHECK_EQUAL(1u, bus.dirty_count());
}

TEST(erd_data_bus, clear_dirty_removes_dirty_flag)
{
  uint8_t v = 1;
  bus.write(0x0700, &v, 1);
  bus.clear_dirty(0x0700);
  CHECK_EQUAL(0u, bus.dirty_count());
}

TEST(erd_data_bus, dirty_count_reflects_multiple_erds)
{
  uint8_t v = 1;
  bus.write(0x0800, &v, 1);
  bus.write(0x0801, &v, 1);
  bus.write(0x0802, &v, 1);
  CHECK_EQUAL(3u, bus.dirty_count());

  bus.clear_dirty(0x0801);
  CHECK_EQUAL(2u, bus.dirty_count());
}

TEST(erd_data_bus, mark_all_dirty_sets_all_existing_entries)
{
  uint8_t v = 1;
  bus.write(0x0900, &v, 1);
  bus.write(0x0901, &v, 1);
  bus.clear_dirty(0x0900);
  bus.clear_dirty(0x0901);
  CHECK_EQUAL(0u, bus.dirty_count());

  bus.mark_all_dirty();
  CHECK_EQUAL(2u, bus.dirty_count());
}

TEST(erd_data_bus, for_each_dirty_visits_only_dirty_entries)
{
  uint8_t v = 1;
  bus.write(0x0A00, &v, 1);  // dirty
  bus.write(0x0A01, &v, 1);  // dirty
  bus.clear_dirty(0x0A01);   // no longer dirty

  std::vector<tiny_erd_t> visited;
  bus.for_each_dirty([&](tiny_erd_t erd, const void* /*data*/, uint8_t /*size*/) {
    visited.push_back(erd);
  });

  CHECK_EQUAL(1u, visited.size());
  CHECK_EQUAL(0x0A00, visited[0]);
}

TEST(erd_data_bus, for_each_dirty_provides_correct_data)
{
  uint8_t value[2] = {0xCA, 0xFE};
  bus.write(0x0B00, value, sizeof(value));

  bus.for_each_dirty([](tiny_erd_t erd, const void* data, uint8_t size) {
    CHECK_EQUAL(0x0B00, erd);
    CHECK_EQUAL(2, size);
    const auto* bytes = static_cast<const uint8_t*>(data);
    CHECK_EQUAL(0xCA, bytes[0]);
    CHECK_EQUAL(0xFE, bytes[1]);
  });
}

// ── change subscriptions ─────────────────────────────────────────────────────

TEST(erd_data_bus, change_callback_fires_on_write)
{
  std::vector<CapturedChange> changes;
  tiny_event_subscription_t sub{};
  bus.subscribe_change(0x0C00, &sub, capture_change, &changes);

  uint8_t v = 0x55;
  bus.write(0x0C00, &v, 1);

  CHECK_EQUAL(1u, changes.size());
  CHECK_EQUAL(0x0C00, changes[0].erd);
  CHECK_EQUAL(1u, changes[0].data.size());
  CHECK_EQUAL(0x55, changes[0].data[0]);
}

TEST(erd_data_bus, change_callback_fires_on_each_write)
{
  std::vector<CapturedChange> changes;
  tiny_event_subscription_t sub{};
  bus.subscribe_change(0x0D00, &sub, capture_change, &changes);

  uint8_t v1 = 0x01;
  bus.write(0x0D00, &v1, 1);
  uint8_t v2 = 0x02;
  bus.write(0x0D00, &v2, 1);

  CHECK_EQUAL(2u, changes.size());
  CHECK_EQUAL(0x01, changes[0].data[0]);
  CHECK_EQUAL(0x02, changes[1].data[0]);
}

TEST(erd_data_bus, change_callback_not_fired_for_different_erd)
{
  std::vector<CapturedChange> changes;
  tiny_event_subscription_t sub{};
  bus.subscribe_change(0x0E00, &sub, capture_change, &changes);

  uint8_t v = 1;
  bus.write(0x0E01, &v, 1);  // different ERD

  CHECK_EQUAL(0u, changes.size());
}

TEST(erd_data_bus, subscribe_before_first_write_still_fires)
{
  // Subscribe to an ERD that has not been written yet.
  std::vector<CapturedChange> changes;
  tiny_event_subscription_t sub{};
  bus.subscribe_change(0x0F00, &sub, capture_change, &changes);

  uint8_t v = 0x77;
  bus.write(0x0F00, &v, 1);

  CHECK_EQUAL(1u, changes.size());
  CHECK_EQUAL(0x77, changes[0].data[0]);
}

TEST(erd_data_bus, multiple_subscribers_for_same_erd)
{
  std::vector<CapturedChange> changes_a;
  std::vector<CapturedChange> changes_b;
  tiny_event_subscription_t sub_a{};
  tiny_event_subscription_t sub_b{};
  bus.subscribe_change(0x1000, &sub_a, capture_change, &changes_a);
  bus.subscribe_change(0x1000, &sub_b, capture_change, &changes_b);

  uint8_t v = 0xAB;
  bus.write(0x1000, &v, 1);

  CHECK_EQUAL(1u, changes_a.size());
  CHECK_EQUAL(1u, changes_b.size());
}
