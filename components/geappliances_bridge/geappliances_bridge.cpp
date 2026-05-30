#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome_time_source.h"

#ifdef USE_ESP32
#include "esp_system.h"
#include "esp_task_wdt.h"
#endif

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "geappliances_bridge";

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

  // Initialize autodiscovery manager (timer-driven, self-driving)
  // Must be called AFTER ERD clients are initialized, so the manager can
  // subscribe to their activity events on valid interfaces.
  this->autodiscovery_manager_.init(
      &this->timer_group_,
      this->uart_ != nullptr ? &this->erd_client_.interface : nullptr,
      this->gea2_uart_ != nullptr ? &this->gea2_erd_client_.interface : nullptr,
      this->gea2_uart_ != nullptr ? &this->gea2_erd_client_adapter_.interface : nullptr,
      this->uart_ != nullptr,
      this->gea2_uart_ != nullptr,
      [this]() {
        // Signal the HSM to transition to the device_id phase.
        // The HSM handles DeviceIdentityManager::init() directly.
        tiny_hsm_send_signal(&this->startup_hsm_, signal_autodiscovery_complete, nullptr);
      });

  // Device ID will be set after autodiscovery completes (either configured or autogenerated)
  if (!this->configured_device_id_.empty()) {
    ESP_LOGI(TAG, "Device ID configured: %s (will be applied after autodiscovery)", this->configured_device_id_.c_str());
  } else {
    ESP_LOGI(TAG, "No device_id configured, will auto-generate after autodiscovery");
  }
  // device_id_state_ stays IDLE until autodiscovery completes

  // The startup HSM handles the boot stabilization delay before autodiscovery.
  ESP_LOGI(TAG, "Waiting %u seconds before starting autodiscovery...",
           AUTODISCOVERY_STARTUP_DELAY_MS / 1000);

  ESP_LOGCONFIG(TAG, "GE Appliances Bridge setup complete");
}

void GeappliancesBridge::loop() {
  // ── MQTT Connection FSM ────────────────────────────────────────────────────
  // A 4-state FSM drives the MQTT (re)connection sequence so that each loop()
  // call performs at most one MQTT operation, keeping the main loop
  // non-blocking.
  //
  //   DISCONNECTED ─(is_connected)─▶ SUBSCRIBING ─(adapter_init)─▶ FLUSHING ─(empty)─▶ RUNNING
  //        ▲                                                              │                  │
  //        └──────────────────────────────────────────────────────────────┴──(disconnect)───┘
  //
  // Note: notify_disconnected() is intentionally NOT called on reconnect —
  // only on genuine connection loss.  Calling it on reconnect caused full GEA2
  // re-identification inside the GEA2 tight loop, leading to heap corruption
  // (see iteration_log.md).
  // ─────────────────────────────────────────────────────────────────────────
  {
    auto mqtt_client = mqtt::global_mqtt_client;
    if (mqtt_client != nullptr) {
      bool is_connected = mqtt_client->is_connected();
      if (!is_connected) {
        // Any state → DISCONNECTED on genuine loss of connection.
        if (this->mqtt_connection_state_ != MqttConnectionState::DISCONNECTED) {
          this->mqtt_connection_state_ = MqttConnectionState::DISCONNECTED;
          if (this->mqtt_client_adapter_initialized_) {
            esphome_mqtt_client_adapter_notify_disconnected(&this->mqtt_client_adapter_);
          }
        }
      } else {
        switch (this->mqtt_connection_state_) {
          case MqttConnectionState::DISCONNECTED:
            // Connect edge: log and signal the startup HSM, then advance to
            // SUBSCRIBING.  The HSM signal may unblock the feature_bits or
            // bridge_init phases.
            ESP_LOGI(TAG, "MQTT connected");
            tiny_hsm_send_signal(&this->startup_hsm_, signal_mqtt_connected, nullptr);
            this->mqtt_connection_state_ = MqttConnectionState::SUBSCRIBING;
            break;

          case MqttConnectionState::SUBSCRIBING:
            // Wait for adapter initialization, then register the single wildcard
            // write topic.  Stay in SUBSCRIBING until the adapter is ready so
            // the subscribe is not skipped when MQTT connects before adapter init.
            if (this->mqtt_client_adapter_initialized_) {
              esphome_mqtt_client_adapter_subscribe_write_topic(&this->mqtt_client_adapter_);
              this->mqtt_connection_state_ = MqttConnectionState::FLUSHING;
            }
            break;

          case MqttConnectionState::FLUSHING:
            // Drain pending ERD updates a few at a time.  Transition to
            // RUNNING once the queue is empty.
            if (this->mqtt_client_adapter_initialized_) {
              if (esphome_mqtt_client_adapter_drain_pending_updates(
                      &this->mqtt_client_adapter_) == 0) {
                this->mqtt_connection_state_ = MqttConnectionState::RUNNING;
              }
            } else {
              this->mqtt_connection_state_ = MqttConnectionState::RUNNING;
            }
            break;

          case MqttConnectionState::RUNNING:
            // Steady-state: drain any newly queued ERD updates.
            if (this->mqtt_client_adapter_initialized_) {
              esphome_mqtt_client_adapter_drain_pending_updates(&this->mqtt_client_adapter_);
            }
            break;
        }
      }
    }
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
#ifdef USE_ESP32
  // Feed the task watchdog after the protocol stack — the GEA2 tight loop
  // can run for 200 ms wall-clock time, exceeding the default TWDT timeout.
  esp_task_wdt_reset();
#endif

  // Initialize the startup HSM on the first loop() call.
  if (this->startup_hsm_.current == nullptr) {
    // Set the back-pointer so HSM state functions can invoke bridge
    // operations through IBridgeServices without a dependency on internals.
    set_bridge_services(this);
    tiny_hsm_init(&this->startup_hsm_, &startup_hsm_configuration,
                  startup_state_protocol_stack);
  }

  // Send the run_loop signal to the current HSM state — this drives
  // the ongoing work for whatever phase we're in.
  uint32_t hsm_start = esphome::millis();
  tiny_hsm_send_signal(&this->startup_hsm_, signal_run_loop, nullptr);
  uint32_t hsm_elapsed = esphome::millis() - hsm_start;
  if (hsm_elapsed >= 1000) {
    ESP_LOGW(TAG, "Long HSM run_loop: %ums", hsm_elapsed);
  }
#ifdef USE_ESP32
  // Feed the task watchdog after the HSM run_loop signal — in steady-state
  // this drains pending MQTT updates (each acquiring the IDF MQTT mutex)
  // and can block for hundreds of milliseconds.
  esp_task_wdt_reset();
#endif
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
    this->autodiscovery_manager_.is_gea2_protocol() ||
    this->gea2_protocol_active_ ||
    this->autodiscovery_manager_.get_state() == AUTODISCOVERY_GEA2_BROADCAST_PENDING ||
    this->autodiscovery_manager_.get_state() == AUTODISCOVERY_GEA2_BROADCAST_WAITING);

  // When both UARTs are configured, only enable the adapter for the active
  // protocol.  Both adapters register poll timers in the shared timer_group_,
  // so tiny_timer_group_run() fires both poll callbacks on every call.  By
  // disabling the inactive adapter, its poll() returns early without reading
  // bytes or publishing events to an interface that isn't being driven.
  uint32_t loop_start = esphome::millis();
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
    // Hard safety cap: never run longer than 2x the nominal duration.
    // If the loop exceeds this, break to avoid starving the ESPHome
    // framework watchdog (which fires at 30 ms intervals).
    static constexpr uint32_t GEA2_LOOP_HARD_CAP_MS = GEA2_LOOP_DURATION_MS * 2;
    while (millis() - loop_start_ms < GEA2_LOOP_DURATION_MS) {
      // Safety break: if we've exceeded the hard cap, exit immediately.
      // This can happen if millis() jumps (e.g., after deep sleep wake)
      // or if the interface_run call stalls unexpectedly.
      if (millis() - loop_start_ms >= GEA2_LOOP_HARD_CAP_MS) {
        ESP_LOGW(TAG, "GEA2 tight loop exceeded hard cap (%u ms), breaking",
                 static_cast<unsigned>(GEA2_LOOP_HARD_CAP_MS));
        break;
      }
#ifdef USE_ESP32
      // Feed the task watchdog inside the tight loop — 200 ms exceeds the
      // default TWDT timeout (usually 3-10 s depending on config, but
      // ESPHome's component watchdog is 30 ms).
      esp_task_wdt_reset();
#endif
      // Fire the GEA2 msec interrupt once per real millisecond. Doing this
      // here (not via a timer_group_ periodic timer) ensures the 1 ms
      // interrupt only fires inside the GEA2 tight loop and never starves
      // the GEA3/polling-bridge timers in the shared timer_group_.
      uint32_t now_ms = millis();
      // Safety cap on the inner msec-catchup loop: if millis() jumped
      // (e.g., deep sleep wake), don't fire thousands of backlogged
      // msec interrupts in one loop iteration.  Cap at 1000 interrupts
      // per loop entry — enough to cover a ~1 s gap without starving
      // the ESPHome watchdog.
      static constexpr uint32_t MSEC_CATCHUP_CAP = 1000;
      uint32_t catchup_count = 0;
      while (s_gea2_last_ms < now_ms && catchup_count < MSEC_CATCHUP_CAP) {
        s_gea2_tick_count++;
        tiny_event_publish(&this->gea2_msec_interrupt_, nullptr);
        s_gea2_last_ms++;
        catchup_count++;
      }
      // tiny_timer_group_run() services at most a single timer per call.
      // With two period-0 UART poll timers in the shared group, calling it
      // once would only fire one of them, effectively halving the polling
      // rate of the active UART and causing missed bytes / ERD read failures.
      // Drain both timers (the inactive one returns early from poll()).
      tiny_timer_group_run(&this->timer_group_);
      if (this->uart_ != nullptr) {
        tiny_timer_group_run(&this->timer_group_);
      }
      tiny_gea2_interface_run(&this->gea2_interface_);
    }
  } else {
    // Standard single-pass for GEA3 (or while awaiting autodiscovery).
    tiny_timer_group_run(&this->timer_group_);
    // When both UARTs are configured, the inactive adapter's period-0 poll
    // timer also fires from the shared timer group.  Drain it so it doesn't
    // steal the next call's slot from the GEA3 adapter's poll timer.
    // tiny_timer_group_run() services exactly one timer per call; with two
    // period-0 timers, calling it once leaves the other timer still pending,
    // which means on the next loop() iteration the GEA2 (inactive) timer
    // fires instead of the GEA3 one — effectively halving the GEA3 poll rate.
    if (this->gea2_uart_ != nullptr) {
      tiny_timer_group_run(&this->timer_group_);
    }
    if (this->uart_ != nullptr) {
      tiny_gea3_interface_run(&this->gea3_interface_);
    }
  }
  uint32_t loop_elapsed = esphome::millis() - loop_start;
  if (loop_elapsed >= 1000) {
    ESP_LOGW(TAG, "Long run_protocol_stack: %ums (mode=%s, polling=%s)",
             loop_elapsed, this->mode_ == BRIDGE_MODE_SUBSCRIBE ? "sub" : (this->mode_ == BRIDGE_MODE_AUTO ? "auto" : "poll"),
             this->mqtt_bridge_initialized_ ? "yes" : "no");
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
             new_state, this->erd_registry_.registered_erds().size());
    this->last_logged_poll_state_ = new_state;
  }
}

void GeappliancesBridge::handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args) {
  // Subscription publications: track AUTO mode activity and reset the HA
  // discovery quiet window for both AUTO and SUBSCRIBE modes.
  if (this->mqtt_bridge_initialized_ &&
      args->address == this->autodiscovery_manager_.get_host_address() &&
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

  // Device ID + feature bit reads (after discovery, before bridge init)
  if (!this->mqtt_bridge_initialized_ && args->address == this->autodiscovery_manager_.get_host_address()) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed) {
      tiny_erd_t erd = args->read_completed.erd;
      const uint8_t* data = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t size = args->read_completed.data_size;
      if (this->should_route_to_feature_bits_(erd)) {
        this->feature_bit_manager_.on_erd_read_completed(erd, data, size);
        if (this->feature_bit_manager_.is_complete()) {
          // Signal the startup HSM that feature bits are ready.
          tiny_hsm_send_signal(&this->startup_hsm_, signal_feature_bits_complete, nullptr);
        }
      } else {
        this->device_identity_manager_.on_erd_read_completed(erd, data, size);
        if (this->device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE) {
          // Signal the startup HSM that device ID is ready.
          tiny_hsm_send_signal(&this->startup_hsm_, signal_device_id_complete, nullptr);
        }
      }
    } else if (args->type == tiny_gea3_erd_client_activity_type_read_failed) {
      tiny_erd_t erd = args->read_failed.erd;
      if (this->should_route_to_feature_bits_(erd)) {
        this->feature_bit_manager_.on_erd_read_failed(erd);
        // If feature bits failed, signal the HSM so it can continue.
        if (this->feature_bit_manager_.is_failed()) {
          tiny_hsm_send_signal(&this->startup_hsm_, signal_feature_bits_complete, nullptr);
        }
      } else {
        this->device_identity_manager_.on_erd_read_failed(erd);
      }
    }
  }
}

bool GeappliancesBridge::should_route_to_feature_bits_(tiny_erd_t erd)
{
  auto is_device_info_erd = [](tiny_erd_t e) {
    return e == ERD_APPLIANCE_TYPE || e == ERD_MODEL_NUMBER || e == ERD_SERIAL_NUMBER;
  };

  // Feature bits are "active" if the manager has been initialized but not
  // yet completed or failed (i.e., still in the middle of reading ERDs).
  bool feature_bit_active = !this->feature_bit_manager_.is_complete() &&
                            !this->feature_bit_manager_.is_failed() &&
                            !this->feature_bit_manager_.is_parse_pending();
  return feature_bit_active &&
    (is_feature_bit_erd(erd) ||
     (is_device_info_erd(erd) && this->device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE));
}

void GeappliancesBridge::on_ha_discovery_erd_seen_(tiny_erd_t erd)
{
  this->ha_discovery_manager_.on_erd_seen(erd);
}

void GeappliancesBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "GE Appliances Bridge:");
  if (!this->configured_device_id_.empty()) {
    ESP_LOGCONFIG(TAG, "  Configured Device ID: %s", this->configured_device_id_.c_str());
  }
  {
    const std::string& device_id = this->device_identity_manager_.get_device_id();
    if (!device_id.empty()) {
      ESP_LOGCONFIG(TAG, "  Device ID: %s", device_id.c_str());
    }
  }
  ESP_LOGCONFIG(TAG, "  Client Address: 0x%02X", this->client_address_);
  ESP_LOGCONFIG(TAG, "  Host Address: 0x%02X", this->autodiscovery_manager_.get_host_address());
  if (this->uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA3 UART: configured (baud %lu)", baud);
  }
  if (this->gea2_uart_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  GEA2 UART: configured (baud %u)", 19200u);
  }
  if (this->autodiscovery_manager_.get_state() == AUTODISCOVERY_COMPLETE) {
    ESP_LOGCONFIG(TAG, "  Active Protocol: %s", this->autodiscovery_manager_.is_gea2_protocol() ? "GEA2" : "GEA3");
  }

  // Display bridge mode
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
  const char* mode_str = "Unknown";
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
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
  if (this->feature_bit_manager_.is_valid_list_ready()) {
    ESP_LOGCONFIG(TAG, "  Appliance API Valid ERDs: %zu", this->feature_bit_manager_.get_valid_erds().size());
  }
  if (!this->custom_erds_vec_.empty()) {
    ESP_LOGCONFIG(TAG, "  Custom ERDs: %zu configured", this->custom_erds_vec_.size());
  }

  // Display current startup state for debugging
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
  const char* phase_str = "Unknown";
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  if (this->startup_hsm_.current == startup_state_protocol_stack)       phase_str = "Protocol Stack";
  else if (this->startup_hsm_.current == startup_state_startup_delay)   phase_str = "Startup Delay";
  else if (this->startup_hsm_.current == startup_state_autodiscovery)    phase_str = "Autodiscovery";
  else if (this->startup_hsm_.current == startup_state_device_id)        phase_str = "Device ID";
  else if (this->startup_hsm_.current == startup_state_mqtt_client_init) phase_str = "MQTT Client Init";
  else if (this->startup_hsm_.current == startup_state_feature_bits)     phase_str = "Feature Bits";
  else if (this->startup_hsm_.current == startup_state_bridge_init)      phase_str = "Bridge Init";
  else if (this->startup_hsm_.current == startup_state_subscription_watch) phase_str = "Subscription Watch";
  else if (this->startup_hsm_.current == startup_state_ha_discovery)     phase_str = "HA Discovery";
  else if (this->startup_hsm_.current == startup_state_running)          phase_str = "Running";
  ESP_LOGCONFIG(TAG, "  Startup State: %s", phase_str);
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after MQTT (priority 50), same priority as UART (priority 600)
  return setup_priority::DATA;  // Priority 600
}

bool GeappliancesBridge::teardown() {
  // Clean up HA discovery manager first (may have a running FreeRTOS task).
  this->ha_discovery_manager_.cleanup();

  // Destroy whichever bridge(s) were actually initialized.
  // Using explicit ownership flags makes this unambiguous and prevents
  // double-free or missed cleanup.
  if (this->subscription_bridge_initialized_) {
    mqtt_bridge_destroy(&this->mqtt_bridge_);
  }
  if (this->polling_bridge_initialized_) {
    mqtt_bridge_polling_destroy(&this->mqtt_bridge_polling_);
  }

  // Free heap-allocated members of the MQTT client adapter to prevent
  // memory leaks (device_id string, pending_updates map, etc.).
  if (this->mqtt_client_adapter_initialized_) {
    esphome_mqtt_client_adapter_destroy(&this->mqtt_client_adapter_);
  }
  Component::teardown();
  return true;
}

// =============================================================================
// IBridgeServices implementation
//
// These thin wrappers are the only entry points the startup HSM is allowed to
// use when interacting with the bridge. Each delegates to an existing internal
// method or member, keeping the HSM decoupled from GeappliancesBridge internals.
// =============================================================================

// -- Autodiscovery ------------------------------------------------------------

void GeappliancesBridge::run_autodiscovery()
{
  autodiscovery_manager_.start();
}

bool GeappliancesBridge::is_autodiscovery_complete() const
{
  return autodiscovery_manager_.get_state() == AUTODISCOVERY_COMPLETE;
}

uint8_t GeappliancesBridge::get_discovered_host_address() const
{
  return autodiscovery_manager_.get_host_address();
}

bool GeappliancesBridge::is_discovered_gea2_protocol() const
{
  return autodiscovery_manager_.is_gea2_protocol();
}

// -- Device ID ----------------------------------------------------------------

void GeappliancesBridge::init_device_id_reading()
{
  if (device_identity_manager_.get_state() != DEVICE_ID_STATE_COMPLETE) {
    device_identity_manager_.init(
        configured_device_id_,
        autodiscovery_manager_.get_active_erd_client(),
        autodiscovery_manager_.get_host_address());
  }
}

bool GeappliancesBridge::is_device_id_complete() const
{
  return device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE;
}

// -- MQTT client adapter ------------------------------------------------------

bool GeappliancesBridge::is_mqtt_client_initialized() const
{
  return mqtt_client_adapter_initialized_;
}

void GeappliancesBridge::initialize_mqtt_client()
{
  initialize_mqtt_client_();
}

// -- Feature bits -------------------------------------------------------------

void GeappliancesBridge::start_feature_bit_reading()
{
  start_feature_bit_reading_();
}

void GeappliancesBridge::run_feature_bits()
{
  feature_bit_manager_.run();
}

bool GeappliancesBridge::is_feature_bits_complete() const
{
  return feature_bit_manager_.is_complete();
}

void GeappliancesBridge::mark_feature_bits_timed_out()
{
  feature_bit_manager_.mark_timed_out();
}

void GeappliancesBridge::record_feature_bits_phase_start()
{
  feature_bits_phase_start_ms_ = millis();
}

bool GeappliancesBridge::is_feature_bits_phase_timed_out() const
{
  return millis() - feature_bits_phase_start_ms_ >= FEATURE_BITS_PHASE_TIMEOUT_MS;
}

void GeappliancesBridge::record_startup_delay_start()
{
  startup_delay_start_ms_ = millis();
}

bool GeappliancesBridge::is_startup_delay_elapsed() const
{
  return millis() - startup_delay_start_ms_ >= AUTODISCOVERY_STARTUP_DELAY_MS;
}

// -- Bridge initialization ----------------------------------------------------

bool GeappliancesBridge::is_bridge_initialized() const
{
  return mqtt_bridge_initialized_;
}

void GeappliancesBridge::initialize_mqtt_bridge()
{
  initialize_mqtt_bridge_();
}

// -- Operating mode -----------------------------------------------------------

BridgeMode GeappliancesBridge::get_mode() const
{
  return mode_;
}

bool GeappliancesBridge::is_subscription_mode_active() const
{
  return subscription_mode_active_;
}

// -- Recurring tasks ----------------------------------------------------------

void GeappliancesBridge::check_subscription_activity()
{
  check_subscription_activity_();
}

void GeappliancesBridge::maybe_start_custom_erd_polling()
{
  maybe_start_custom_erd_polling_();
}

void GeappliancesBridge::log_poll_state_transitions()
{
  log_poll_state_transitions_();
}

void GeappliancesBridge::run_ha_discovery()
{
  ha_discovery_manager_.run(
      !((mode_ == BRIDGE_MODE_SUBSCRIBE) ||
        (mode_ == BRIDGE_MODE_AUTO && subscription_mode_active_)),
      mqtt_bridge_polling_.polling_list_complete,
      subscription_activity_detected_,
      mqtt::global_mqtt_client);
}

void GeappliancesBridge::run_all_managers()
{
  feature_bit_manager_.run();
}

}  // namespace geappliances_bridge
}  // namespace esphome
