#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome_time_source.h"

#ifdef USE_ESP32
#include "esp_system.h"
#endif

namespace esphome {
namespace geappliances_bridge {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif
static const char *const TAG = "geappliances_bridge";
#ifdef __clang__
#pragma clang diagnostic pop
#endif

static const tiny_gea3_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};

// GEA2 ERD client: one attempt per bridge-level retry cycle.
// request_retries = 0 means the ERD client sends exactly one copy of each
// request and fails cleanly after request_timeout ms, rather than queueing
// up to 11 copies in the GEA2 interface's send queue.  Multiple queued copies
// cause half-duplex collisions: the GEA2 interface starts sending a retry at
// the same time the appliance's response to the previous request arrives on
// the bus; the response bytes are treated as unexpected reflections in
// state_send, handle_send_failure() fires, and state_collision_cooldown
// silently discards the response — so no ACK is ever sent.  With retries=0,
// only one packet is ever in-flight at a time, eliminating the collision.
// Bridge-level retries (try_read_erd_with_retry_) are spaced ~500 ms apart
// (200 ms tight loop + 50 ms ESPHome gap + processing), giving appliances
// with slow first-access NVRAM lookups time to cache the value before the
// next attempt.
static const tiny_gea2_erd_client_configuration_t gea2_client_configuration = {
  .request_timeout = 250,
  .request_retries = 0
};

// Tick-counter time source for the GEA2 interface's internal timer group.
// The counter is incremented once per real millisecond inside the GEA2 tight
// loop so that tiny_gea2_interface's internal timers advance by at most 1 ms
// per event regardless of the ~50 ms ESPHome framework gap between loop() calls
// (see doc/geappliances_bridge.md §13 for the full explanation).
// Kept as file-scope statics so the tight-loop code and the tick function
// below can both access them without exposing them as class members.
static tiny_time_source_ticks_t s_gea2_tick_count = 0;
// Tracks the last millis() value at which the GEA2 msec interrupt was fired.
// Initialized to 0 (sentinel: "not yet started"); set to millis() on the first
// entry into the GEA2 tight loop so accumulated boot time is not replayed.
static uint32_t s_gea2_last_ms = 0;

static tiny_time_source_ticks_t gea2_tick_ticks(i_tiny_time_source_t *)
{
  return s_gea2_tick_count;
}
static const i_tiny_time_source_api_t kGea2TickApi = { gea2_tick_ticks };
static i_tiny_time_source_t g_gea2_tick_source = { &kGea2TickApi };

void GeappliancesBridge::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

  // Initialize timer group
  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

  // Initialize heap monitoring
  this->heap_monitor_.init(
      this->free_heap_sensor_,
      this->min_free_heap_sensor_,
      this->heap_fragmentation_sensor_);

  // Initialize autodiscovery manager
  this->autodiscovery_manager_.init(
      this->uart_ != nullptr ? &this->erd_client_.interface : nullptr,
      this->gea2_uart_ != nullptr ? &this->gea2_erd_client_.interface : nullptr,
      this->gea2_uart_ != nullptr ? &this->gea2_erd_client_adapter_.interface : nullptr,
      this->uart_ != nullptr,
      this->gea2_uart_ != nullptr,
      [this]() {
        this->sync_autodiscovery_legacy_members_();
        this->start_device_id_generation_();
      });

  // Initialize GEA3 components if GEA3 UART is configured
  if (this->uart_ != nullptr) {
    esphome_uart_adapter_init(&this->uart_adapter_, &this->timer_group_, this->uart_);

    tiny_gea3_interface_init(
      &this->gea3_interface_,
      &this->uart_adapter_.interface,
      this->client_address_,
      this->send_queue_buffer_,
      sizeof(this->send_queue_buffer_),
      this->receive_buffer_,
      sizeof(this->receive_buffer_),
      false);

    tiny_gea3_erd_client_init(
      &this->erd_client_,
      &this->timer_group_,
      &this->gea3_interface_.interface,
      this->client_queue_buffer_,
      sizeof(this->client_queue_buffer_),
      &client_configuration);

    tiny_event_subscription_init(
      &this->erd_client_activity_subscription_,
      this,
      +[](void* context, const void* args) {
        auto self = reinterpret_cast<GeappliancesBridge*>(context);
        auto activity_args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
        self->handle_erd_client_activity_(activity_args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(&this->erd_client_.interface),
      &this->erd_client_activity_subscription_);
  }

  // Initialize GEA2 components if GEA2 UART is configured
  if (this->gea2_uart_ != nullptr) {
    esphome_uart_adapter_init(&this->gea2_uart_adapter_, &this->timer_group_, this->gea2_uart_);

    // Initialize the GEA2 msec-interrupt event that drives the GEA2 interface's
    // internal timeout counters.  The event is published manually inside the
    // GEA2 tight loop (see loop()) so it only fires when GEA2 is actually
    // in use — keeping the shared timer_group_ free of a 1 ms periodic timer
    // that would starve GEA3/polling-bridge timers when GEA3 is active.
    tiny_event_init(&this->gea2_msec_interrupt_);

    tiny_gea2_interface_init(
      &this->gea2_interface_,
      &this->gea2_uart_adapter_.interface,
      &g_gea2_tick_source,
      &this->gea2_msec_interrupt_.interface,
      this->client_address_,
      this->gea2_send_queue_buffer_,
      sizeof(this->gea2_send_queue_buffer_),
      this->gea2_receive_buffer_,
      sizeof(this->gea2_receive_buffer_),
      false,
      1);

    tiny_gea2_erd_client_init(
      &this->gea2_erd_client_,
      &this->timer_group_,
      &this->gea2_interface_.interface,
      this->gea2_client_queue_buffer_,
      sizeof(this->gea2_client_queue_buffer_),
      &gea2_client_configuration);

    // Initialize the GEA2-to-GEA3 adapter and subscribe to its activity
    gea2_erd_client_adapter_init(&this->gea2_erd_client_adapter_, &this->gea2_erd_client_.interface);

    tiny_event_subscription_init(
      &this->gea2_activity_subscription_,
      this,
      +[](void* context, const void* args) {
        auto self = reinterpret_cast<GeappliancesBridge*>(context);
        auto activity_args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
        self->handle_erd_client_activity_(activity_args);
      });
    tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(&this->gea2_erd_client_adapter_.interface),
      &this->gea2_activity_subscription_);
  }

  // Device ID will be set after autodiscovery completes (either configured or autogenerated)
  if (!this->configured_device_id_.empty()) {
    ESP_LOGI(TAG, "Device ID configured: %s (will be applied after autodiscovery)", this->configured_device_id_.c_str());
  } else {
    ESP_LOGI(TAG, "No device_id configured, will auto-generate after autodiscovery");
  }
  // device_id_state_ stays IDLE until autodiscovery completes

  // Start the boot stabilization delay before autodiscovery traffic.
  // (AutodiscoveryManager uses AUTODISCOVERY_STARTUP_DELAY_MS internally)
  ESP_LOGI(TAG, "Waiting %u seconds before starting autodiscovery...",
           AUTODISCOVERY_STARTUP_DELAY_MS / 1000);

  ESP_LOGCONFIG(TAG, "GE Appliances Bridge setup complete");
}

void GeappliancesBridge::loop() {
  // Track MQTT connection state and notify the bridge on (re)connect.
  auto mqtt_client = mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    bool is_connected = mqtt_client->is_connected();
    if (is_connected && !this->mqtt_was_connected_) {
      this->on_mqtt_connected_();
      // Signal the startup HSM that MQTT is now connected — this may
      // unblock the feature_bits or bridge_init phases.
      tiny_hsm_send_signal(&this->startup_hsm_, signal_mqtt_connected, nullptr);
    } else if (!is_connected && this->mqtt_was_connected_) {
      // Notify the adapter that MQTT has disconnected so it resets the
      // connect timestamp and publishes the disconnect event to the bridge
      // HSMs.  Without this, the adapter would not re-apply the settle delay
      // or re-subscribe the wildcard write topic after reconnect.
      if (this->mqtt_client_adapter_initialized_) {
        esphome_mqtt_client_adapter_notify_disconnected(&this->mqtt_client_adapter_);
      }
    }
    // Drain pending ERD updates a few at a time each loop() call so that the
    // burst of up to MAX_PENDING_UPDATES publishes after an MQTT reconnect is
    // spread across multiple loop iterations (avoids a 1+ s stall from
    // acquiring the IDF MQTT API mutex for each publish in succession).
    // Also subscribes the wildcard write topic on first connect.
    if (is_connected && this->mqtt_client_adapter_initialized_) {
      esphome_mqtt_client_adapter_notify_connected(&this->mqtt_client_adapter_);
    }
    this->mqtt_was_connected_ = is_connected;
  }

  // ── Startup HSM ────────────────────────────────────────────────────────
  // The bridge progresses through a linear sequence of startup phases via
  // a tiny_hsm-based state machine.  Each state handles its own entry/exit
  // logic and waits for signals from managers before transitioning.
  //
  // Phase dependency chain:
  //   PROTOCOL → AUTODISCOVERY → DEVICE_ID → MQTT_CLIENT → FEATURE_BITS
  //           → BRIDGE_INIT → SUBSCRIPTION_WATCH → HA_DISCOVERY → HEAP
  //           → RUNNING (steady-state)
  // ────────────────────────────────────────────────────────────────────────

  // Drive the GEA2/GEA3 protocol stack on every loop iteration so that
  // UART bytes are processed and ERD read responses are delivered to the
  // active manager (autodiscovery, device ID, feature bits, polling bridge).
  this->run_protocol_stack_();

  // Run heap monitoring every loop() regardless of HSM state so the
  // heap sensors always show data, even if the HSM is stalled waiting
  // for MQTT to connect.
  this->heap_monitor_.run();

  // Initialize the startup HSM on the first loop() call.
  if (this->startup_hsm_.current == nullptr) {
    // Set the back-pointer so HSM state functions can access the bridge
    // without using container_of/offsetof on a non-POD C++ class.
    set_bridge_instance(this);
    tiny_hsm_init(&this->startup_hsm_, &startup_hsm_configuration,
                  startup_state_protocol_stack);
  }

  // Send the run_loop signal to the current HSM state — this drives
  // the ongoing work for whatever phase we're in.
  tiny_hsm_send_signal(&this->startup_hsm_, signal_run_loop, nullptr);
}

// ---------------------------------------------------------------------------
// Phase 0: Drive the GEA2/GEA3 hardware stack
// ---------------------------------------------------------------------------

void GeappliancesBridge::run_protocol_stack_()
{
  // When GEA2 is active (or during GEA2 autodiscovery), run a 200 ms
  // wall-clock busy loop so the full TX→RX cycle at 19200 baud completes
  // within a single loop() call.  See doc/geappliances_bridge.md §13.
  bool need_gea2_loop = this->gea2_uart_ != nullptr && (
    this->gea2_protocol_active_ ||
    this->autodiscovery_state_ == AUTODISCOVERY_GEA2_BROADCAST_PENDING ||
    this->autodiscovery_state_ == AUTODISCOVERY_GEA2_BROADCAST_WAITING);

  // When both UARTs are configured, only enable the adapter for the active
  // protocol.  Both adapters register poll timers in the shared timer_group_,
  // so tiny_timer_group_run() fires both poll callbacks on every call.  By
  // disabling the inactive adapter, its poll() returns early without reading
  // bytes or publishing events to an interface that isn't being driven.
  if (this->gea2_uart_ != nullptr && this->uart_ != nullptr) {
    esphome_uart_adapter_set_enabled(&this->uart_adapter_, !need_gea2_loop);
    esphome_uart_adapter_set_enabled(&this->gea2_uart_adapter_, need_gea2_loop);
  }

  if (need_gea2_loop) {
    uint32_t loop_start_ms = millis();
    // Initialize s_gea2_last_ms on first entry so we don't replay accumulated
    // boot time as thousands of spurious msec interrupts.
    if (s_gea2_last_ms == 0) {
      s_gea2_last_ms = loop_start_ms;
    }
    while (millis() - loop_start_ms < GEA2_LOOP_DURATION_MS) {
      // Fire the GEA2 msec interrupt once per real millisecond. Doing this
      // here (not via a timer_group_ periodic timer) ensures the 1 ms
      // interrupt only fires inside the GEA2 tight loop and never starves
      // the GEA3/polling-bridge timers in the shared timer_group_.
      uint32_t now_ms = millis();
      while (s_gea2_last_ms < now_ms) {
        s_gea2_tick_count++;
        tiny_event_publish(&this->gea2_msec_interrupt_, nullptr);
        s_gea2_last_ms++;
      }
      tiny_timer_group_run(&this->timer_group_);
      tiny_gea2_interface_run(&this->gea2_interface_);
    }
  } else {
    // Standard single-pass for GEA3 (or while awaiting autodiscovery).
    tiny_timer_group_run(&this->timer_group_);
    if (this->uart_ != nullptr) {
      tiny_gea3_interface_run(&this->gea3_interface_);
    }
  }
}

// ---------------------------------------------------------------------------
// Debug helper: log polling HSM state transitions (called every loop)
// ---------------------------------------------------------------------------

void GeappliancesBridge::log_poll_state_transitions_()
{
  if (!this->mqtt_bridge_initialized_) {
    return;
  }
  bool is_poll_mode = !((this->mode_ == BRIDGE_MODE_SUBSCRIBE) ||
                        (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_));
  if (!is_poll_mode) {
    return;
  }
  const char* new_state = this->mqtt_bridge_polling_.current_state_name;
  if (new_state != nullptr && new_state != this->last_logged_poll_state_) {
    ESP_LOGD(TAG, "Polling bridge state: %s (ERDs registered: %zu)",
             new_state, this->ha_registered_erds_.size());
    this->last_logged_poll_state_ = new_state;
  }
}

void GeappliancesBridge::handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args) {
  // Subscription publications: track AUTO mode activity and reset the HA
  // discovery quiet window for both AUTO and SUBSCRIBE modes.
  if (this->mqtt_bridge_initialized_ &&
      args->address == this->host_address_ &&
      args->type == tiny_gea3_erd_client_activity_type_subscription_publication_received) {
    if (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_ &&
        !this->subscription_activity_detected_) {
      ESP_LOGI(TAG, "Subscription activity detected - subscription mode is working");
      this->subscription_activity_detected_ = true;
    }
    if (this->custom_erd_subscription_seen_erds_.insert(args->subscription_publication_received.erd).second) {
      this->custom_erd_subscription_last_activity_ = millis();
    }
    // Reset the HA discovery quiet window only for new ERD IDs. Repeated value
    // updates for already-seen ERDs do not extend the wait.
    this->on_ha_discovery_erd_seen_(args->subscription_publication_received.erd);
  }

  // Handle autodiscovery: first responder on GEA3 or GEA2 broadcast
  bool in_gea3_discovery = (this->autodiscovery_manager_.get_state() == AUTODISCOVERY_GEA3_BROADCAST_WAITING);
  bool in_gea2_discovery = (this->autodiscovery_manager_.get_state() == AUTODISCOVERY_GEA2_BROADCAST_WAITING);
  if (in_gea3_discovery || in_gea2_discovery) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed &&
        args->read_completed.erd == ERD_APPLIANCE_TYPE &&
        this->autodiscovery_manager_.get_active_erd_client() == nullptr &&
        args->read_completed.data_size >= 1) {
      uint8_t app_type = reinterpret_cast<const uint8_t*>(args->read_completed.data)[0];
      ESP_LOGD(TAG, "Board discovered: address=0x%02X appliance_type=%u (%s)",
               args->address, app_type, appliance_type_to_string(app_type).c_str());
      this->autodiscovery_manager_.on_broadcast_response(args->address, app_type, in_gea3_discovery);
      // Sync legacy members immediately for backward compatibility.
      this->sync_autodiscovery_legacy_members_();
    }
    return;
  }

  // Device ID + feature bit reads (after discovery, before bridge init)
  if (!this->mqtt_bridge_initialized_ && args->address == this->host_address_) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed) {
      tiny_erd_t erd = args->read_completed.erd;
      const uint8_t* data = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t size = args->read_completed.data_size;
      if (this->should_route_to_feature_bits_(erd)) {
        this->feature_bit_manager_.on_erd_read_completed(erd, data, size);
        // Sync legacy members for backward compatibility.
        this->sync_feature_bit_legacy_members_();
        if (this->feature_bit_manager_.is_complete()) {
          // Signal the startup HSM that feature bits are ready.
          tiny_hsm_send_signal(&this->startup_hsm_, signal_feature_bits_complete, nullptr);
        }
      } else {
        this->device_identity_manager_.on_erd_read_completed(erd, data, size);
        // Sync legacy members for backward compatibility.
        this->device_id_state_ = this->device_identity_manager_.get_state();
        if (this->device_identity_manager_.is_complete()) {
          this->appliance_type_ = this->device_identity_manager_.get_appliance_type();
          this->model_number_   = this->device_identity_manager_.get_model_number();
          this->serial_number_  = this->device_identity_manager_.get_serial_number();
          this->generated_device_id_ = this->device_identity_manager_.get_generated_device_id();
          this->final_device_id_     = this->device_identity_manager_.get_device_id();
          // Signal the startup HSM that device ID is ready.
          tiny_hsm_send_signal(&this->startup_hsm_, signal_device_id_complete, nullptr);
        }
      }
    } else if (args->type == tiny_gea3_erd_client_activity_type_read_failed) {
      tiny_erd_t erd = args->read_failed.erd;
      if (this->should_route_to_feature_bits_(erd)) {
        this->feature_bit_manager_.on_erd_read_failed(erd);
        // Sync legacy members for backward compatibility.
        this->sync_feature_bit_legacy_members_();
        // If feature bits failed, signal the HSM so it can continue.
        if (this->feature_bit_manager_.is_failed()) {
          tiny_hsm_send_signal(&this->startup_hsm_, signal_feature_bits_complete, nullptr);
        }
      } else {
        ESP_LOGW(TAG, "Failed to read ERD 0x%04X for device ID generation (reason: %u), will retry",
                 erd, args->read_failed.reason);
        this->device_identity_manager_.on_erd_read_failed(erd);
        // Sync legacy members for backward compatibility.
        this->device_id_state_ = this->device_identity_manager_.get_state();
        if (this->device_identity_manager_.is_complete()) {
          this->appliance_type_ = this->device_identity_manager_.get_appliance_type();
          this->model_number_   = this->device_identity_manager_.get_model_number();
          this->serial_number_  = this->device_identity_manager_.get_serial_number();
          this->generated_device_id_ = this->device_identity_manager_.get_generated_device_id();
          this->final_device_id_     = this->device_identity_manager_.get_device_id();
          // Signal the startup HSM that device ID is ready (even on failure, we have a fallback).
          tiny_hsm_send_signal(&this->startup_hsm_, signal_device_id_complete, nullptr);
        } else if (this->device_identity_manager_.is_failed()) {
          this->final_device_id_     = this->device_identity_manager_.get_device_id();
          this->generated_device_id_ = this->device_identity_manager_.get_generated_device_id();
          tiny_hsm_send_signal(&this->startup_hsm_, signal_device_id_failed, nullptr);
        }
      }
    }
  }
}

bool GeappliancesBridge::should_route_to_feature_bits_(tiny_erd_t erd)
{
  auto is_device_info_erd = [](tiny_erd_t e) {
    return e == ERD_APPLIANCE_TYPE || e == ERD_MODEL_NUMBER || e == ERD_SERIAL_NUMBER;
  };

  bool feature_bit_active = (this->feature_bit_state_ != FEATURE_BIT_STATE_IDLE &&
                               this->feature_bit_state_ != FEATURE_BIT_STATE_COMPLETE &&
                               this->feature_bit_state_ != FEATURE_BIT_STATE_FAILED);
  return feature_bit_active &&
    (is_feature_bit_erd(erd) ||
     (is_device_info_erd(erd) && this->device_id_state_ == DEVICE_ID_STATE_COMPLETE));
}

void GeappliancesBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "GE Appliances Bridge:");
  if (!this->configured_device_id_.empty()) {
    ESP_LOGCONFIG(TAG, "  Configured Device ID: %s", this->configured_device_id_.c_str());
  }
  if (!this->final_device_id_.empty()) {
    ESP_LOGCONFIG(TAG, "  Device ID: %s", this->final_device_id_.c_str());
  }
  if (!this->generated_device_id_.empty()) {
    ESP_LOGCONFIG(TAG, "  Generated Device ID: %s", this->generated_device_id_.c_str());
    ESP_LOGCONFIG(TAG, "    Appliance Type: %u", this->appliance_type_);
    ESP_LOGCONFIG(TAG, "    Model Number: %s", this->model_number_.c_str());
    ESP_LOGCONFIG(TAG, "    Serial Number: %s", this->serial_number_.c_str());
  }
  if (this->device_id_state_ == DEVICE_ID_STATE_FAILED) {
    ESP_LOGCONFIG(TAG, "  Device ID Generation: FAILED (see logs for details)");
  }
  ESP_LOGCONFIG(TAG, "  Client Address: 0x%02X", this->client_address_);
  ESP_LOGCONFIG(TAG, "  Host Address: 0x%02X", this->host_address_);
  if (this->uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA3 UART: configured (baud %lu)", baud);
  }
  if (this->gea2_uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA2 UART: configured (baud %u)", 19200u);
  }
  if (this->autodiscovery_state_ == AUTODISCOVERY_COMPLETE) {
    ESP_LOGCONFIG(TAG, "  Active Protocol: %s", this->gea2_protocol_active_ ? "GEA2" : "GEA3");
  }

  // Display bridge mode
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
  const char* mode_str = "Unknown";
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  if (this->mode_ == BRIDGE_MODE_POLL) {
    mode_str = "Polling";
  } else if (this->mode_ == BRIDGE_MODE_SUBSCRIBE) {
    mode_str = "Subscription";
  } else if (this->mode_ == BRIDGE_MODE_AUTO) {
    if (this->subscription_mode_active_) {
      mode_str = "Auto (Subscription)";
    } else {
      mode_str = "Auto (Polling - fallback)";
    }
  }
  ESP_LOGCONFIG(TAG, "  Mode: %s", mode_str);
  
  if (this->mode_ == BRIDGE_MODE_POLL || !this->subscription_mode_active_) {
    ESP_LOGCONFIG(TAG, "  Polling Interval: %u ms", this->polling_interval_ms_);
    ESP_LOGCONFIG(TAG, "  Only Publish On Change: %s", this->polling_only_publish_on_change_ ? "yes" : "no");
  }
  ESP_LOGCONFIG(TAG, "  Appliance API Parsing: %s", this->appliance_api_parsing_ ? "enabled" : "disabled");
  if (this->appliance_api_valid_list_ready_) {
    ESP_LOGCONFIG(TAG, "  Appliance API Valid ERDs: %zu", this->appliance_api_valid_erds_.size());
  }
  if (!this->custom_erds_vec_.empty()) {
    ESP_LOGCONFIG(TAG, "  Custom ERDs: %zu configured", this->custom_erds_vec_.size());
  }

  // Display current startup state for debugging
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
  const char* phase_str = "Unknown";
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  if (this->startup_hsm_.current == startup_state_protocol_stack)       phase_str = "Protocol Stack";
  else if (this->startup_hsm_.current == startup_state_autodiscovery)    phase_str = "Autodiscovery";
  else if (this->startup_hsm_.current == startup_state_device_id)        phase_str = "Device ID";
  else if (this->startup_hsm_.current == startup_state_mqtt_client_init) phase_str = "MQTT Client Init";
  else if (this->startup_hsm_.current == startup_state_feature_bits)     phase_str = "Feature Bits";
  else if (this->startup_hsm_.current == startup_state_bridge_init)      phase_str = "Bridge Init";
  else if (this->startup_hsm_.current == startup_state_subscription_watch) phase_str = "Subscription Watch";
  else if (this->startup_hsm_.current == startup_state_ha_discovery)     phase_str = "HA Discovery";
  else if (this->startup_hsm_.current == startup_state_heap_monitor)     phase_str = "Heap Monitor";
  else if (this->startup_hsm_.current == startup_state_running)          phase_str = "Running";
  ESP_LOGCONFIG(TAG, "  Startup State: %s", phase_str);
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after UART (priority 600) and MQTT (priority 50)
  return setup_priority::DATA;  // Priority 600
}

bool GeappliancesBridge::teardown() {
  // Clean up HA discovery manager first (may have a running FreeRTOS task).
  this->ha_discovery_manager_.cleanup();

  // Destroy whichever bridge(s) were initialized.
  if (this->custom_erd_polling_started_) {
    mqtt_bridge_polling_destroy(&this->mqtt_bridge_polling_);
  }
  if (this->mqtt_bridge_initialized_) {
    bool is_poll_mode = !((this->mode_ == BRIDGE_MODE_SUBSCRIBE) ||
                          (this->mode_ == BRIDGE_MODE_AUTO && this->subscription_mode_active_));
    if (is_poll_mode) {
      // Polling bridge may be in mqtt_bridge_polling_ (custom ERDs) or
      // mqtt_bridge_polling_ (fallback from subscription).
      if (!this->custom_erd_polling_started_) {
        mqtt_bridge_polling_destroy(&this->mqtt_bridge_polling_);
      }
    } else {
      mqtt_bridge_destroy(&this->mqtt_bridge_);
    }
  }

  // Free heap-allocated members of the MQTT client adapter to prevent
  // memory leaks (device_id string, pending_updates map, etc.).
  if (this->mqtt_client_adapter_initialized_) {
    esphome_mqtt_client_adapter_destroy(&this->mqtt_client_adapter_);
  }
  Component::teardown();
  return true;
}

}  // namespace geappliances_bridge
}  // namespace esphome
