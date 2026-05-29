/*!
 * @file
 * @brief Unit tests for the startup HSM phase timeout paths.
 *
 * Tests focus on the two timeout guards added in the Phase 3 refactoring:
 *
 *   1. Device ID phase timeout (30 s): when the DeviceIdentityManager stalls,
 *      the HSM transitions to mqtt_client_init using a fallback device ID.
 *
 *   2. Feature bits phase timeout (60 s): when feature bit ERD reads stall,
 *      the HSM marks the manager as timed-out and transitions to bridge_init
 *      as soon as MQTT becomes connected.
 *
 * Tests also cover the normal (non-timeout) happy-path transitions for these
 * two phases so regressions in the timeout guard don't mask broken happy paths.
 *
 * Note: Production headers with STL containers (<set>, <vector>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "geappliances_bridge_startup_hsm.h"
#include "i_bridge_services.h"
#include "esphome/components/mqtt/mqtt_client.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

using namespace esphome::geappliances_bridge;

// =============================================================================
// MockBridgeServices — thin CppUMock wrapper around IBridgeServices
//
// Only the methods exercised by the tested states need mock expectations;
// all others are stubs that do nothing / return a safe default.
// =============================================================================

class MockBridgeServices : public IBridgeServices {
 public:
  // -- Autodiscovery ----------------------------------------------------------
  void run_autodiscovery() override {}
  bool is_autodiscovery_complete() const override { return true; }
  uint8_t get_discovered_host_address() const override { return 0xC0; }
  bool is_discovered_gea2_protocol() const override { return false; }

  // -- Device ID --------------------------------------------------------------
  void init_device_id_reading() override {
    mock().actualCall("init_device_id_reading").onObject(this);
  }
  void run_device_id() override {
    mock().actualCall("run_device_id").onObject(this);
  }
  bool is_device_id_complete() const override {
    return mock().actualCall("is_device_id_complete").onObject(this)
               .returnBoolValueOrDefault(false);
  }
  bool is_device_id_failed() const override {
    return mock().actualCall("is_device_id_failed").onObject(this)
               .returnBoolValueOrDefault(false);
  }
  void record_device_id_phase_start() override {
    mock().actualCall("record_device_id_phase_start").onObject(this);
  }
  bool is_device_id_phase_timed_out() const override {
    return mock().actualCall("is_device_id_phase_timed_out").onObject(this)
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
  void run_feature_bits() override {
    mock().actualCall("run_feature_bits").onObject(this);
  }
  bool is_feature_bits_complete() const override {
    return mock().actualCall("is_feature_bits_complete").onObject(this)
               .returnBoolValueOrDefault(false);
  }
  void mark_feature_bits_timed_out() override {
    mock().actualCall("mark_feature_bits_timed_out").onObject(this);
  }
  void record_feature_bits_phase_start() override {
    mock().actualCall("record_feature_bits_phase_start").onObject(this);
  }
  bool is_feature_bits_phase_timed_out() const override {
    return mock().actualCall("is_feature_bits_phase_timed_out").onObject(this)
               .returnBoolValueOrDefault(false);
  }

  // -- Bridge initialization --------------------------------------------------
  bool is_bridge_initialized() const override { return false; }
  void initialize_mqtt_bridge() override {}

  // -- Operating mode ---------------------------------------------------------
  BridgeMode get_mode() const override { return BRIDGE_MODE_AUTO; }
  bool is_subscription_mode_active() const override { return true; }

  // -- Recurring tasks --------------------------------------------------------
  void check_subscription_activity() override {}
  void maybe_start_custom_erd_polling() override {}
  void log_poll_state_transitions() override {}
  void run_ha_discovery() override {}
  void run_all_managers() override {}
};

// =============================================================================
// HsmMockMqttClient — minimal stub for esphome::mqtt::MQTTClientComponent
//
// Named distinctly to avoid ODR collision with MockMqttClient in the adapter
// test TU, which has the same base class but a different is_connected() body.
// =============================================================================

class HsmMockMqttClient : public esphome::mqtt::MQTTClientComponent {
 public:
  bool is_connected() override {
    return mock().actualCall("is_connected").onObject(this)
               .returnBoolValueOrDefault(false);
  }
  void publish(const std::string&, const std::string&, uint8_t, bool) override {}
  void subscribe(const std::string&,
                 std::function<void(const std::string&, const std::string&)>,
                 uint8_t) override {}
};

// =============================================================================
// Test group
// =============================================================================

TEST_GROUP(startup_hsm)
{
  tiny_hsm_t hsm;
  MockBridgeServices svc;
  HsmMockMqttClient mqtt_client;

  void setup()
  {
    mock().clear();
    mock().strictOrder();
    esphome::mqtt::global_mqtt_client = nullptr;
    set_bridge_services(&svc);
  }

  void teardown()
  {
    mock().clear();
    esphome::mqtt::global_mqtt_client = nullptr;
    set_bridge_services(nullptr);
  }

  // Helper: expect the full mqtt_client_init → feature_bits entry chain.
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
    mock().expectOneCall("start_feature_bit_reading").onObject(&svc);
    mock().expectOneCall("record_feature_bits_phase_start").onObject(&svc);
  }
};

// =============================================================================
// Device ID phase — timeout path
// =============================================================================

TEST(startup_hsm, device_id_phase_timeout_transitions_to_feature_bits)
{
  // HSM entry into startup_state_device_id sends:
  //   init_device_id_reading, record_device_id_phase_start, is_device_id_complete
  mock().expectOneCall("init_device_id_reading").onObject(&svc);
  mock().expectOneCall("record_device_id_phase_start").onObject(&svc);
  mock().expectOneCall("is_device_id_complete").onObject(&svc).andReturnValue(false);

  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_device_id);

  // When run_loop fires and the phase has timed out, the HSM must transition
  // immediately to startup_state_mqtt_client_init (and then feature_bits).
  mock()
    .expectOneCall("is_device_id_phase_timed_out")
    .onObject(&svc)
    .andReturnValue(true);
  expect_mqtt_client_init_and_feature_bits_entry();

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);

  // Verify: current state is startup_state_feature_bits (mqtt_client_init is
  // a transient state that transitions during its own entry signal).
  CHECK(hsm.current == startup_state_feature_bits);

  mock().checkExpectations();
}

// =============================================================================
// Device ID phase — happy path (complete without timeout)
// =============================================================================

TEST(startup_hsm, device_id_phase_complete_transitions_to_feature_bits)
{
  // Entry expects
  mock().expectOneCall("init_device_id_reading").onObject(&svc);
  mock().expectOneCall("record_device_id_phase_start").onObject(&svc);
  mock().expectOneCall("is_device_id_complete").onObject(&svc).andReturnValue(false);

  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_device_id);

  // run_loop: no timeout, manager completes normally
  mock()
    .expectOneCall("is_device_id_phase_timed_out")
    .onObject(&svc)
    .andReturnValue(false);
  mock().expectOneCall("run_device_id").onObject(&svc);
  mock().expectOneCall("is_device_id_complete").onObject(&svc).andReturnValue(true);
  expect_mqtt_client_init_and_feature_bits_entry();

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);

  CHECK(hsm.current == startup_state_feature_bits);
  mock().checkExpectations();
}

// =============================================================================
// Feature bits phase — timeout path, MQTT not yet connected
// =============================================================================

TEST(startup_hsm, feature_bits_timeout_waits_for_mqtt_connection)
{
  // Entry expects
  mock().expectOneCall("record_feature_bits_phase_start").onObject(&svc);

  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_feature_bits);

  // global_mqtt_client is null → MQTT not connected → no transition yet.
  // The manager is marked timed-out so is_feature_bits_complete() returns true.
  mock()
    .expectOneCall("is_feature_bits_phase_timed_out")
    .onObject(&svc)
    .andReturnValue(true);
  mock().expectOneCall("mark_feature_bits_timed_out").onObject(&svc);
  mock().expectOneCall("run_feature_bits").onObject(&svc);
  mock()
    .expectOneCall("is_feature_bits_complete")
    .onObject(&svc)
    .andReturnValue(true);
  // mqtt::global_mqtt_client == nullptr → no transition

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);

  // Still in feature_bits — MQTT hasn't connected yet.
  CHECK(hsm.current == startup_state_feature_bits);
  mock().checkExpectations();
}

// =============================================================================
// Feature bits phase — timeout path then MQTT connects
// =============================================================================

TEST(startup_hsm, feature_bits_timeout_then_mqtt_connected_signal_transitions_to_bridge_init)
{
  // Entry expects
  mock().expectOneCall("record_feature_bits_phase_start").onObject(&svc);

  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_feature_bits);

  // Step 1: run_loop with timeout — MQTT not connected yet.
  mock()
    .expectOneCall("is_feature_bits_phase_timed_out")
    .onObject(&svc)
    .andReturnValue(true);
  mock().expectOneCall("mark_feature_bits_timed_out").onObject(&svc);
  mock().expectOneCall("run_feature_bits").onObject(&svc);
  mock()
    .expectOneCall("is_feature_bits_complete")
    .onObject(&svc)
    .andReturnValue(true);

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);
  CHECK(hsm.current == startup_state_feature_bits);

  // Step 2: MQTT connects — signal_mqtt_connected causes the state to check
  // is_feature_bits_complete() and transition to bridge_init.
  mock()
    .expectOneCall("is_feature_bits_complete")
    .onObject(&svc)
    .andReturnValue(true);

  tiny_hsm_send_signal(&hsm, signal_mqtt_connected, nullptr);

  CHECK(hsm.current == startup_state_bridge_init);
  mock().checkExpectations();
}

// =============================================================================
// Feature bits phase — happy path (complete + MQTT connected in same run_loop)
// =============================================================================

TEST(startup_hsm, feature_bits_complete_with_mqtt_connected_transitions_to_bridge_init)
{
  // Entry expects
  mock().expectOneCall("record_feature_bits_phase_start").onObject(&svc);

  tiny_hsm_init(&hsm, &startup_hsm_configuration, startup_state_feature_bits);

  // Set up a connected MQTT client
  esphome::mqtt::global_mqtt_client = &mqtt_client;

  // run_loop: not timed out, manager completes, MQTT connected → transition.
  // Expectation order matches actual call order in startup_state_feature_bits:
  //   is_feature_bits_phase_timed_out → run_feature_bits →
  //   is_feature_bits_complete → is_connected
  mock()
    .expectOneCall("is_feature_bits_phase_timed_out")
    .onObject(&svc)
    .andReturnValue(false);
  mock().expectOneCall("run_feature_bits").onObject(&svc);
  mock()
    .expectOneCall("is_feature_bits_complete")
    .onObject(&svc)
    .andReturnValue(true);
  mock()
    .expectOneCall("is_connected")
    .onObject(&mqtt_client)
    .andReturnValue(true);

  tiny_hsm_send_signal(&hsm, signal_run_loop, nullptr);

  CHECK(hsm.current == startup_state_bridge_init);
  mock().checkExpectations();
}
