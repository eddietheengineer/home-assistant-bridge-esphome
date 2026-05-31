/*
 * @file
 * @brief Unit tests for the SubscriptionHandler class.
 *
 * Validates that SubscriptionHandler compiles and has the expected interface.
 * Full HSM lifecycle testing is deferred to integration tests.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "subscription_handler.h"
#include "erd_state_table.h"
#include <cstring>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(subscription_handler)
{
  ErdStateTable state_table_;

  void setup()
  {
  }

  void teardown()
  {
  }
};

/* ------------------------------------------------------------------ */
/* Interface compilation tests                                          */
/* ------------------------------------------------------------------ */

TEST(subscription_handler, compiles_with_null_params)
{
  // Verify the class compiles and basic interface works
  // We can't fully test without a real ERD client double,
  // but verify the interface exists and doesn't crash on construction
  SubscriptionHandler handler(nullptr, &state_table_, nullptr);
  // Don't call start() — it would dereference null erd_client_
  CHECK(!handler.is_known_erd(0x1001));
  CHECK_EQUAL(0, handler.get_known_erds().size());
}
