/*!
 * @file
 * @brief Unit tests for the startup HSM phase transitions.
 *
 * Tests focus on the normal (non-timeout) happy-path transitions for all
 * startup phases.  MQTT connectivity gating has been removed — the HSM
 * transitions to bridge_init as soon as feature bits are complete.
 *
 * Note: Production headers with STL containers (<set>, <vector>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "geappliances_bridge_startup_hsm.h"
#include "i_bridge_services.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

using namespace esphome::geappliances_bridge;

// =============================================================================
// MockBridgeServices — thin CppUMock wrapper around IBridgeServices
// =============================================================================

class MockBridgeServices : public IBridgeServices {
 public:
  // -- Autodiscovery ----------------------------------------------------------
  void run_autodiscovery() override {
    mock().actualCall("run_autodiscovery").onObject(this);
  }
  bool is_autodiscovery_complete() const override {
    return mock().actualCall("is_autodiscovery_complete").onObject(this)
               .returnBoolValueOrDefault(false);
  }
  uint8_t get_discovered_host_address() const override {
    return (uint8_t)mock().actualCall("get_discovered_host_address").onObject(this)
               .returnIntValueOrDefault(0xC0);
  }
  bool is_discovered_gea2_protocol() const override {
    return mock().actualCall("is_discovered_gea2_protocol").onObject(this)
               .returnBoolValueOrDefault(false);
  }

  // -- Device ID --------------------------------------------------------------
  void init_device_id_reading() override {
    mock().actualCall("init_device_id_reading").onObject(this);
  }
  bool is_device_id_complete() const override {
    return mock().actualCall("is_device_id_complete").onObject(this)
               .returnBoolValueOrDefault(false);
  }

  // -- MQTT client adapter ----------------------------------------------------
  bool is_mqtt_client_initialized() const override {
    return mock().actualCall("is_mqtt_client_initialized").onObject(this)
               .returnBoolValueOrDefault(false);
  }
  void initialize_mqtt_client() override {
    mock().actualCall("initialize_mqtt_client").onObject(this);
  }

  // -- Feature bits -----------------------------------------------------------
  void start_feature_bit_reading() override {
    mock().actualCall("start_feature_bit_reading").onObject(this);
  }
  bool is_feature_bits_complete() const override {
    return mock().actualCall("is_feature_bits_complete").onObject(this)
               .returnBoolValueOrDefault(false);
  }

  // -- Bridge initialization --------------------------------------------------
  bool is_bridge_initialized() const override {
    return mock().actualCall("is_bridge_initialized").onObject(this)
               .returnBoolValueOrDefault(false);
  }
  void initialize_erd_bridge() override {
    mock().actualCall("initialize_erd_bridge").onObject(this);
  }

  // -- Operating mode ---------------------------------------------------------
  BridgeMode get_mode() const override {
    return static_cast<BridgeMode>(
      mock().actualCall("get_mode").onObject(this)
        .returnIntValueOrDefault(BRIDGE_MODE_AUTO));
  }
  bool is_subscription_mode_active() const override { return true; }

  // -- Startup delay ----------------------------------------------------------
  void record_startup_delay_start() override {
    mock().actualCall("record_startup_delay_start").onObject(this);
  }
  bool is_startup_delay_elapsed() const override {
    return mock().actualCall("is_startup_delay_elapsed").onObject(this)
               .returnBoolValueOrDefault(false);
  }

  // -- Recurring tasks --------------------------------------------------------
  void check_subscription_activity() override {}
  void maybe_start_custom_erd_polling() override {
    mock().actualCall("maybe_start_custom_erd_polling").onObject(this);
  }
  void initialize_erd_cache_publisher() override {
    mock().actualCall("initialize_erd_cache_publisher").onObject(this);
  }
  bool is_erd_cache_publisher_initialized() const override {
    return mock().actualCall("is_erd_cache_publisher_initialized").onObject(this)
               .returnBoolValueOrDefault(false);
  }
  void log_poll_state_transitions() override {}
  void run_all_managers() override {}
};

// =============================================================================
// Test group
// =============================================================================

TEST_GROUP(startup_hsm)
{
  tiny_hsm_t hsm;
  MockBridgeServices svc;

  void setup()
  {
    mock().clear();
    mock().strictOrder();
    set_bridge_services(&svc);
  }

  void teardown()
  {
    mock().clear();
    set_bridge_services(nullptr);
  }

  // Helper: expect the full mqtt_client_init -> feature_bits entry chain.
  // Called whenever the device_id phase transitions out (timeout or success).
  void expect_mqtt_client_init_and_feature_bits_entry(bool mqtt_already_initialized = false)
  {
    mock()
      .expectOneCall("is_mqtt_client_initialized")
      .onObject(&svc)
      .andReturnValue(mqtt_already_initialized);
    if (!mqtt_already_initialized) {
      mock().expectOneCall("initialize_mqtt_client").onObject(&svc);
    }
    mock().expectOneCall("is_erd_cache_publisher_initialized").onObject(&svc).andReturnValue(false);
    mock().expectOneCall("initialize_erd_cache_publisher").onObject(&svc);
    mock().expectOneCall("start_feature_bit_reading").onObject(&svc);
  }
};

// =============================================================================
// Device ID phase — manager completes synchronously during init (entry path)
// =============================================================================

TEST(startup_hsm, device_id_phase_complete_on_entry_transitions_to_mqtt_client_init)
{
  mock().expectOneCall("init_device_id_reading").onObject(&svc);
  mock().expectOneCall("is_device_id_complete").onObject(&svc).andReturnValue(true);
  expect_mqtt_client_init_and_feature_bits_entry();

  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_device_id);

  CHECK(hsm.current == startup_state_feature_bits);
  mock().checkExpectations();
}

// =============================================================================
// Device ID phase — manager completes on run_loop (self-driving)
// =============================================================================

TEST(startup_hsm, device_id_phase_complete_on_run_loop_transitions_to_feature_bits)
{
  mock().expectOneCall("init_device_id_reading").onObject(&svc);
  mock().expectOneCall("is_device_id_complete").onObject(&svc).andReturnValue(false);

  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_device_id);

  mock().expectOneCall("is_device_id_complete").onObject(&svc).andReturnValue(true);
  expect_mqtt_client_init_and_feature_bits_entry();

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);

  CHECK(hsm.current == startup_state_feature_bits);
  mock().checkExpectations();
}

// =============================================================================
// Device ID phase — external signal_device_id_complete transitions immediately
// =============================================================================

TEST(startup_hsm, device_id_phase_signal_complete_transitions_to_mqtt_client_init)
{
  mock().expectOneCall("init_device_id_reading").onObject(&svc);
  mock().expectOneCall("is_device_id_complete").onObject(&svc).andReturnValue(false);

  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_device_id);

  expect_mqtt_client_init_and_feature_bits_entry();

  tiny_hsm_send_signal(&hsm, signal_device_id_complete, nullptr);

  CHECK(hsm.current == startup_state_feature_bits);
  mock().checkExpectations();
}

// =============================================================================
// Feature bits phase — not yet complete, stays in feature_bits
// =============================================================================

TEST(startup_hsm, feature_bits_incomplete_stays_in_feature_bits)
{
  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_feature_bits);

  mock()
    .expectOneCall("is_feature_bits_complete")
    .onObject(&svc)
    .andReturnValue(false);

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);

  CHECK(hsm.current == startup_state_feature_bits);
  mock().checkExpectations();
}

// =============================================================================
// Feature bits phase — complete on run_loop transitions to bridge_init
// =============================================================================

TEST(startup_hsm, feature_bits_complete_on_run_loop_transitions_to_bridge_init)
{
  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_feature_bits);

  mock()
    .expectOneCall("is_feature_bits_complete")
    .onObject(&svc)
    .andReturnValue(true);

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);

  CHECK(hsm.current == startup_state_bridge_init);
  mock().checkExpectations();
}

// =============================================================================
// Feature bits phase — signal_feature_bits_complete transitions immediately
// =============================================================================

TEST(startup_hsm, feature_bits_signal_complete_transitions_to_bridge_init)
{
  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_feature_bits);

  tiny_hsm_send_signal(&hsm, signal_feature_bits_complete, nullptr);

  CHECK(hsm.current == startup_state_bridge_init);
  mock().checkExpectations();
}
// =============================================================================
// Full startup flow — protocol_stack through running
// =============================================================================

TEST(startup_hsm, full_startup_flow_reaches_running)
{
  /* Set up all mock expectations upfront in the order they will be called. */
  /* Phase 1: protocol_stack → startup_delay entry */
  mock().expectOneCall("record_startup_delay_start").onObject(&svc);
  /* Phase 2: startup_delay run_loop → autodiscovery entry */
  mock().expectOneCall("is_startup_delay_elapsed").onObject(&svc).andReturnValue(true);
  mock().expectOneCall("run_autodiscovery").onObject(&svc);
  /* Phase 3: autodiscovery_complete → device_id → mqtt_client_init → feature_bits */
  mock().expectOneCall("is_autodiscovery_complete").onObject(&svc).andReturnValue(true);
  mock().expectOneCall("init_device_id_reading").onObject(&svc);
  mock().expectOneCall("is_device_id_complete").onObject(&svc).andReturnValue(true);
  mock().expectOneCall("is_mqtt_client_initialized").onObject(&svc).andReturnValue(false);
  mock().expectOneCall("initialize_mqtt_client").onObject(&svc);
  mock().expectOneCall("is_erd_cache_publisher_initialized").onObject(&svc).andReturnValue(false);
  mock().expectOneCall("initialize_erd_cache_publisher").onObject(&svc);
  mock().expectOneCall("start_feature_bit_reading").onObject(&svc);
  /* Phase 5: bridge_init run_loop */
  mock().expectOneCall("is_bridge_initialized").onObject(&svc).andReturnValue(false);
  mock().expectOneCall("is_autodiscovery_complete").onObject(&svc).andReturnValue(true);
  mock().expectOneCall("initialize_erd_bridge").onObject(&svc);
  /* Phase 6: subscription_watch entry → running */
  mock().expectOneCall("get_mode").onObject(&svc).andReturnValue(BRIDGE_MODE_POLL);
  mock().expectOneCall("maybe_start_custom_erd_polling").onObject(&svc);
  /* Phase 7: running run_loop */
  mock().expectOneCall("get_mode").onObject(&svc).andReturnValue(BRIDGE_MODE_POLL);
  mock().expectOneCall("maybe_start_custom_erd_polling").onObject(&svc);
  /* Drive the HSM through all phases. */
  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_protocol_stack);
  CHECK(hsm.current == startup_state_startup_delay);

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);
  CHECK(hsm.current == startup_state_autodiscovery);

  tiny_hsm_send_signal(&hsm, signal_autodiscovery_complete, nullptr);
  CHECK(hsm.current == startup_state_feature_bits);

  tiny_hsm_send_signal(&hsm, signal_feature_bits_complete, nullptr);
  CHECK(hsm.current == startup_state_bridge_init);

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);
  CHECK(hsm.current == startup_state_bridge_init);

  tiny_hsm_send_signal(&hsm, signal_bridge_ready, nullptr);
  /* After bridge_ready, we should be in running (non-AUTO mode). */
  CHECK(hsm.current == startup_state_running);

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);
  CHECK(hsm.current == startup_state_running);

  mock().checkExpectations();
}
