#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome_time_source.h"
#include "erd_cache.h"

#ifdef USE_ESP32
#include "esp_system.h"
#include "esp_task_wdt.h"
#endif

GEA_TAG(TAG) = "geappliances_bridge";

namespace esphome {
namespace geappliances_bridge {

void GeappliancesBridge::add_custom_erd(tiny_erd_t erd)
{
  if (this->custom_erds_count_ >= CUSTOM_ERDS_MAX) return;
  this->custom_erds_[this->custom_erds_count_++] = erd;
}

static const tiny_gea3_erd_client_configuration_t client_configuration = {
  .request_timeout = 250,
  .request_retries = 10
};

// GEA2 ERD client: one internal retry.  With request_timeout=250ms and
// request_retries=1, each read gets two attempts (500ms total), giving
// appliances time to service slow first-access NVRAM lookups or transient
// bus collisions without needing bridge-level retry logic.
static const tiny_gea2_erd_client_configuration_t gea2_client_configuration = {
  .request_timeout = 250,
  .request_retries = 1
};

// Tick-counter time source for the GEA2 interface's internal timer group.
// The counter is incremented once per real millisecond inside the GEA2 tight
// loop so that tiny_gea2_interface's internal timers advance by at most 1 ms
// per event regardless of the ~50 ms ESPHome framework gap between loop() calls
// (see doc/geappliances_bridge.md §13 for the full explanation).
//
// The tick count and last_ms are class members (gea2_tick_count_, gea2_last_ms_)
// so they reset on re-init (deep sleep wake, ESPHome reconfiguration).
// The tick source API uses a file-scope pointer to the current bridge instance.
static GeappliancesBridge* g_gea2_bridge = nullptr;

tiny_time_source_ticks_t gea2_tick_ticks(i_tiny_time_source_t *)
{
  if (g_gea2_bridge) {
    return g_gea2_bridge->gea2_tick_count_;
  }
  return 0;
}
static const i_tiny_time_source_api_t kGea2TickApi = { gea2_tick_ticks };
static i_tiny_time_source_t g_gea2_tick_source = { &kGea2TickApi };

void GeappliancesBridge::setup() {
  // Reset GEA2 state on re-init (deep sleep wake, ESPHome reconfiguration)
  this->gea2_tick_count_ = 0;
  this->gea2_last_ms_ = 0;
  ESP_LOGCONFIG(TAG, "Setting up GE Appliances Bridge...");

  // Initialize timer group
  tiny_timer_group_init(&this->timer_group_, esphome_time_source_init());

  // Initialize the shared ERD cache before any component uses it.
  erd_cache_init(&this->erd_cache_);
  // Initialize the fixed-capacity set for tracking seen subscription ERDs.
  erd_set_init(&this->custom_erd_subscription_seen_erds_);
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
    g_gea2_bridge = this;

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
        tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_autodiscovery_complete, nullptr);
      });

  // Device ID will be set after autodiscovery completes (either configured or autogenerated)
  if (this->configured_device_id_[0] != '\0') {
    ESP_LOGI(TAG, "Device ID configured: %s (will be applied after autodiscovery)", this->configured_device_id_);
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

  // Drive the GEA2/GEA3 protocol stack FIRST so that UART bytes are
  // processed before any MQTT work.  The tight loop must run before
  // MQTT operations to avoid starving UART processing on single-core
  // ESP32 variants where a blocking MQTT call can delay response
  // processing past the appliance's timeout window.
  this->run_protocol_stack_();

  /* Tick ERD publish cooldowns once per second.
   * Uses a member variable (not static) so it resets on re-init and
   * avoids the first-tick race on cold start. Unsigned subtraction
   * handles millis() wrap correctly. */
  if (this->throttle_rate_seconds_ > 0) {
    uint32_t now = esphome::millis();
    if (now - this->last_cooldown_tick_ >= 1000) {
      this->last_cooldown_tick_ = now;
      erd_cache_tick_cooldowns(&this->erd_cache_);
    }
  }
#ifdef USE_ESP32
  // Feed the task watchdog after the protocol stack — the GEA2 tight loop
  // can run for 200 ms wall-clock time, exceeding the default TWDT timeout.
  esp_task_wdt_reset();
#endif

  // Initialize the startup HSM on the first loop() call.
  if (this->startup_hsm_wrapper_.hsm.current == nullptr) {
    startup_hsm_wrapper_init(&this->startup_hsm_wrapper_, this, startup_state_protocol_stack);
  }

  // Send the run_loop signal to the current HSM state — this drives
  // the ongoing work for whatever phase we're in.
  uint32_t hsm_start = esphome::millis();
  tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_run_loop, nullptr);
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

  // On ESP-IDF, signal the background MQTT publisher task instead of
  // blocking the main loop on the IDF MQTT mutex.  On non-ESP-IDF
  // platforms, fall back to the direct loop() call as before.
  // Pause ERD cache publishing during HA discovery cleanup & publish
  // to avoid competing for the ESP-IDF MQTT task's inbound/outbound
  // queues, which causes dropped retained messages during cleanup.
  bool ha_discovery_active = ha_discovery_manager_is_processing(&this->ha_discovery_manager_);

  if (ha_discovery_active) {
    if (this->erd_cache_publisher_.cache != nullptr) {
      erd_cache_mqtt_publisher_pause(&this->erd_cache_publisher_);
      if (!this->erd_cache_publisher_paused_) {
        ESP_LOGD(TAG, "ERD cache publisher paused during MQTT discovery payload generation");
        this->erd_cache_publisher_paused_ = true;
      }
    }
  } else {
    if (this->erd_cache_publisher_.cache != nullptr) {
      erd_cache_mqtt_publisher_resume(&this->erd_cache_publisher_);
      if (this->erd_cache_publisher_paused_) {
        ESP_LOGD(TAG, "ERD cache publisher resumed after MQTT discovery payload generation");
        this->erd_cache_publisher_paused_ = false;
        this->discovery_just_resumed_ = true;
      }
    }
  }

  /* Check steady state BEFORE signaling work — the background task sets
   * first_round_done during its drain, and we want to read it before
   * the next signal_work() wakes it again. */
  if (this->discovery_just_resumed_ &&
      erd_cache_mqtt_publisher_first_round_done(&this->erd_cache_publisher_)) {
    ESP_LOGI(TAG, "Device is in steady state");
    this->discovery_just_resumed_ = false;
  }

  if (this->erd_cache_publisher_.cache != nullptr && !ha_discovery_active) {
#ifdef USE_ESP_IDF
    erd_cache_mqtt_publisher_signal_work(&this->erd_cache_publisher_);
#else
    erd_cache_mqtt_publisher_loop(&this->erd_cache_publisher_, 5, 20);
#endif
  }

  // Start HA discovery once steady state is reached and generate_device_config is enabled.
  if (this->steady_state_reached_ && !this->ha_discovery_started_ && this->generate_device_config_) {
    this->ha_discovery_started_ = true;
    ha_discovery_manager_configure(
      &this->ha_discovery_manager_,
      this->device_identity_manager_.get_device_id(),
      this->device_identity_manager_.get_model_number(),
      this->device_identity_manager_.get_serial_number(),
      this->device_identity_manager_.get_appliance_type(),
      this->filter_config_topics_,
      &this->erd_cache_,
      &this->mqtt_client_adapter_.interface);
    ha_discovery_manager_start(&this->ha_discovery_manager_);
  }

  /* Drive the HA discovery consumer (publishes at rate-limited intervals). */
  if (ha_discovery_manager_is_processing(&this->ha_discovery_manager_)) {
    ha_discovery_manager_run(&this->ha_discovery_manager_);
  }

  /* If cleanup-only finished, restart the device so normal boot republishes. */
  if (this->discovery_refresh_in_progress_) {
#ifdef USE_ESP_IDF
    ha_discovery_cleanup_run(&this->ha_discovery_manager_.cleanup);
    if (ha_discovery_cleanup_is_done(&this->ha_discovery_manager_.cleanup)) {
      this->discovery_refresh_in_progress_ = false;
      ESP_LOGI(TAG, "HA discovery cleanup complete, restarting device...");
      // Allow final retained-clear publishes to transmit before reboot (fixes C5).
      vTaskDelay(pdMS_TO_TICKS(500));
      esphome::App.reboot();
    }
#endif
  }

  // Publish ERD/MQTT publish rate + cache stats sensors every ~60 seconds.
  if (this->erd_publish_rate_sensor_ != nullptr || this->mqtt_publish_rate_sensor_ != nullptr) {
    uint32_t now = esphome::millis();
    if (now - this->last_erd_publish_rate_publish_ >= ERD_PUBLISH_RATE_INTERVAL_MS) {
      if (this->erd_publish_rate_sensor_ != nullptr) {
        uint32_t count = erd_cache_get_update_rate(&this->erd_cache_);
        this->erd_publish_rate_sensor_->publish_state(static_cast<float>(count));
      }
      if (this->mqtt_publish_rate_sensor_ != nullptr) {
        uint32_t count = erd_cache_mqtt_publisher_get_publish_rate(&this->erd_cache_publisher_);
        this->mqtt_publish_rate_sensor_->publish_state(static_cast<float>(count));
      }
      this->last_erd_publish_rate_publish_ = now;
    }
  }

  // Publish cache stats sensors every ~60 seconds.
  if (this->erd_cache_entries_sensor_ != nullptr || this->erd_cache_updates_sensor_ != nullptr) {
    uint32_t now = esphome::millis();
    if (now - this->last_erd_cache_stats_publish_ >= ERD_PUBLISH_RATE_INTERVAL_MS) {
      if (this->erd_cache_entries_sensor_ != nullptr) {
        this->erd_cache_entries_sensor_->publish_state(
          static_cast<float>(erd_cache_get_count(&this->erd_cache_)));
      }
      if (this->erd_cache_updates_sensor_ != nullptr) {
        this->erd_cache_updates_sensor_->publish_state(
          static_cast<float>(erd_cache_get_required_update_rate(&this->erd_cache_)));
      }
      this->last_erd_cache_stats_publish_ = now;
    }
  }
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
    // Initialize gea2_last_ms_ on first entry so we don't replay accumulated
    // boot time as thousands of spurious msec interrupts.
    if (this->gea2_last_ms_ == 0) {
      this->gea2_last_ms_ = loop_start_ms;
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
      // Feed the task watchdog inside the tight loop — 100 ms exceeds the
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
      while (this->gea2_last_ms_ < now_ms && catchup_count < MSEC_CATCHUP_CAP) {
        this->gea2_tick_count_++;
        tiny_event_publish(&this->gea2_msec_interrupt_, nullptr);
        this->gea2_last_ms_++;
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
    // GEA3 path: run a tight loop at 1ms intervals to ensure UART bytes
    // at 230400 baud are processed without missing messages.  The tight
    // loop runs whenever GEA3 UART is configured and GEA2 is not active.
    // This covers all phases: startup (autodiscovery, device_id, feature_bits),
    // bridge initialization, and steady-state polling/subscription.
    if (this->uart_ != nullptr) {
      uint32_t gea3_loop_start_ms = millis();
      static constexpr uint32_t GEA3_LOOP_HARD_CAP_MS = GEA3_LOOP_DURATION_MS * 2;
      while (millis() - gea3_loop_start_ms < GEA3_LOOP_DURATION_MS) {
        if (millis() - gea3_loop_start_ms >= GEA3_LOOP_HARD_CAP_MS) {
          ESP_LOGW(TAG, "GEA3 tight loop exceeded hard cap (%u ms), breaking",
                   static_cast<unsigned>(GEA3_LOOP_HARD_CAP_MS));
          break;
        }
#ifdef USE_ESP32
        esp_task_wdt_reset();
#endif
        tiny_timer_group_run(&this->timer_group_);
        if (this->gea2_uart_ != nullptr) {
          tiny_timer_group_run(&this->timer_group_);
        }
        tiny_gea3_interface_run(&this->gea3_interface_);
      }
    } else {
      // No GEA3 UART configured (GEA2-only or neither).  Single-pass to
      // keep timers advancing for autodiscovery or other background work.
      tiny_timer_group_run(&this->timer_group_);
      if (this->gea2_uart_ != nullptr) {
        tiny_timer_group_run(&this->timer_group_);
      }
    }
  }
  uint32_t loop_elapsed = esphome::millis() - loop_start;
  if (loop_elapsed >= 1000) {
    ESP_LOGW(TAG, "Long run_protocol_stack: %ums (mode=%s, polling=%s)",
             loop_elapsed, this->mode_ == BRIDGE_MODE_SUBSCRIBE ? "sub" : (this->mode_ == BRIDGE_MODE_AUTO ? "auto" : "poll"),
             this->erd_bridge_initialized_ ? "yes" : "no");
  }
}

// ---------------------------------------------------------------------------
// Debug helper: log polling HSM state transitions (called every loop)
// ---------------------------------------------------------------------------

void GeappliancesBridge::log_poll_state_transitions_()
{
  if (!this->erd_bridge_initialized_) {
    return;
  }

  subscription_state_t sub_state = this->get_subscription_state();

  // Log polling bridge state changes.  The polling bridge is always active
  // when erd_bridge_initialized_ is true (either as the primary bridge in
  // poll mode, or as the custom-ERD polling bridge alongside subscription).
  if (this->polling_bridge_initialized_) {
    polling_state_t poll_state = this->get_polling_state();
    if (poll_state != polling_state_none && poll_state != this->last_logged_poll_state_) {
      this->last_logged_poll_state_ = poll_state;
    }
  }

  // Log subscription bridge state changes.
  if (this->subscription_bridge_initialized_) {
    if (sub_state != subscription_state_none && sub_state != this->last_logged_subscribe_state_) {
      ESP_LOGI(TAG, "Subscription bridge state: %s (ERDs cached: %u)",
               subscription_state_name(sub_state), erd_cache_get_count(&this->erd_cache_));
      this->last_logged_subscribe_state_ = sub_state;
    }
  }
}

void GeappliancesBridge::handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args) {
  // Subscription publications: track ERDs covered by subscription for
  // custom ERD polling bridge filtering.
  if (this->erd_bridge_initialized_ &&
      args->address == this->autodiscovery_manager_.get_host_address() &&
      args->type == tiny_gea3_erd_client_activity_type_subscription_publication_received) {
    erd_set_insert(&this->custom_erd_subscription_seen_erds_, args->subscription_publication_received.erd);
  }

  // Device ID reads (after discovery, before bridge init)
  // Note: FeatureBitManager subscribes directly to ERD client activity events,
  // so the bridge no longer routes feature bit ERDs to it.
  if (!this->erd_bridge_initialized_ && args->address == this->autodiscovery_manager_.get_host_address()) {
    if (args->type == tiny_gea3_erd_client_activity_type_read_completed) {
      tiny_erd_t erd = args->read_completed.erd;
      const uint8_t* data = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t size = args->read_completed.data_size;
      if (!this->should_route_to_feature_bits_(erd)) {
        this->device_identity_manager_.on_erd_read_completed(erd, data, size);
        if (this->device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE) {
          // Signal the startup HSM that device ID is ready.
          tiny_hsm_send_signal(&this->startup_hsm_wrapper_.hsm, signal_device_id_complete, nullptr);
        }
      }
    } else if (args->type == tiny_gea3_erd_client_activity_type_read_failed) {
      tiny_erd_t erd = args->read_failed.erd;
      if (!this->should_route_to_feature_bits_(erd)) {
        this->device_identity_manager_.on_erd_read_failed(erd);
      }
    }
  }
}

bool GeappliancesBridge::should_route_to_feature_bits_(tiny_erd_t erd)
{
  FeatureBitState state = this->feature_bit_manager_.get_state();
  bool feature_bit_active = (state != FEATURE_BIT_STATE_COMPLETE &&
                            state != FEATURE_BIT_STATE_FAILED);
  return feature_bit_active && is_feature_bit_erd(erd);
}

void GeappliancesBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "GE Appliances Bridge:");
  if (this->configured_device_id_[0] != '\0') {
    ESP_LOGCONFIG(TAG, "  Configured Device ID: %s", this->configured_device_id_);
  }
  {
    const char* device_id = this->device_identity_manager_.get_device_id();
    if (device_id != nullptr && device_id[0] != '\0') {
      ESP_LOGCONFIG(TAG, "  Device ID: %s", device_id);
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
  const char* mode_str = "Unknown";
  if (this->mode_ == BRIDGE_MODE_POLL) {
    mode_str = "Polling";
  } else if (this->mode_ == BRIDGE_MODE_SUBSCRIBE) {
    mode_str = "Subscription";
  } else if (this->mode_ == BRIDGE_MODE_AUTO) {
    subscription_state_t sub_state = this->get_subscription_state();
    if (subscription_is_active(sub_state)) {
      mode_str = "Auto (Subscription)";
    } else {
      mode_str = "Auto (Polling - fallback)";
    }
  }
  (void)mode_str;
  ESP_LOGCONFIG(TAG, "  Mode: %s", mode_str);

  {
    subscription_state_t sub_state = this->get_subscription_state();
    if (this->mode_ == BRIDGE_MODE_POLL || !subscription_is_active(sub_state)) {
      ESP_LOGCONFIG(TAG, "  Polling Interval: %u ms", this->polling_interval_ms_);
    }
  }
  ESP_LOGCONFIG(TAG, "  Appliance API Parsing: %s", this->appliance_api_parsing_ ? "enabled" : "disabled");
  if (this->feature_bit_manager_.get_state() == FEATURE_BIT_STATE_COMPLETE) {
    ESP_LOGCONFIG(TAG, "  Appliance API Valid ERDs: %u", this->feature_bit_manager_.get_valid_erd_count());
  }
  if (this->polling_bridge_initialized_) {
    polling_state_t poll_state = this->get_polling_state();
    const char* poll_state_str = polling_state_name(poll_state);
    if (poll_state_str != nullptr) {
      ESP_LOGCONFIG(TAG, "  Polling Bridge State: %s", poll_state_str);
    }
  }
  if (this->custom_erds_count_ > 0) {
    ESP_LOGCONFIG(TAG, "  Custom ERDs: %u configured", this->custom_erds_count_);
  }

  // Display current startup state for debugging
  const char* phase_str = "Unknown";
  if (this->startup_hsm_wrapper_.hsm.current == startup_state_protocol_stack)       phase_str = "Protocol Stack";
  else if (this->startup_hsm_wrapper_.hsm.current == startup_state_startup_delay)   phase_str = "Startup Delay";
  else if (this->startup_hsm_wrapper_.hsm.current == startup_state_autodiscovery)    phase_str = "Autodiscovery";
  else if (this->startup_hsm_wrapper_.hsm.current == startup_state_device_id)        phase_str = "Device ID";
  else if (this->startup_hsm_wrapper_.hsm.current == startup_state_mqtt_client_init) phase_str = "MQTT Client Init";
  else if (this->startup_hsm_wrapper_.hsm.current == startup_state_feature_bits)     phase_str = "Feature Bits";
  else if (this->startup_hsm_wrapper_.hsm.current == startup_state_bridge_init)      phase_str = "Bridge Init";
  else if (this->startup_hsm_wrapper_.hsm.current == startup_state_subscription_watch) phase_str = "Subscription Watch";
  else if (this->startup_hsm_wrapper_.hsm.current == startup_state_running)          phase_str = "Running";
  (void)phase_str;
  ESP_LOGCONFIG(TAG, "  Startup State: %s", phase_str);
}

float GeappliancesBridge::get_setup_priority() const {
  // Run after MQTT (priority 50), same priority as UART (priority 600)
  return setup_priority::DATA;  // Priority 600
}

bool GeappliancesBridge::teardown() {
  // Reset GEA2 globals so re-init starts fresh
  g_gea2_bridge = nullptr;
  this->gea2_tick_count_ = 0;
  this->gea2_last_ms_ = 0;
  // Clean up feature bit manager (unsubscribe from ERD client events, stop timers).
  this->feature_bit_manager_.cleanup();
  // Destroy whichever bridge(s) were actually initialized.
  // Using explicit ownership flags makes this unambiguous and prevents
  // double-free or missed cleanup.
  if (this->subscription_bridge_initialized_) {
    erd_bridge_subscribe_destroy(&this->erd_bridge_subscribe_);
  }
  if (this->polling_bridge_initialized_) {
    erd_bridge_poll_destroy(&this->erd_bridge_poll_);
  }
  if (this->write_bridge_initialized_) {
    erd_write_bridge_destroy(&this->erd_write_bridge_);
  }

  // Destroy the shared ERD cache after bridges are torn down.

  // Clean up the HA discovery manager before the ERD cache publisher.
  // The discovery manager holds pointers to erd_cache_ and mqtt_client_adapter_
  // which are destroyed later in the teardown sequence.
  ha_discovery_manager_cleanup(&this->ha_discovery_manager_);

  // Destroy the ERD cache publisher before the adapter is destroyed.
  if (this->erd_cache_publisher_.cache) {
    erd_cache_mqtt_publisher_destroy(&this->erd_cache_publisher_);
  }
  erd_cache_destroy(&this->erd_cache_);

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

bool GeappliancesBridge::is_feature_bits_complete() const
{
  return feature_bit_manager_.get_state() == FEATURE_BIT_STATE_COMPLETE ||
         feature_bit_manager_.get_state() == FEATURE_BIT_STATE_FAILED;
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
  return erd_bridge_initialized_;
}

void GeappliancesBridge::initialize_erd_bridge()
{
  initialize_erd_bridge_();
}

// -- Operating mode -----------------------------------------------------------

BridgeMode GeappliancesBridge::get_mode() const
{
  return mode_;
}

subscription_state_t GeappliancesBridge::get_subscription_state() const
{
  return this->erd_bridge_subscribe_.current_state;
}

polling_state_t GeappliancesBridge::get_polling_state() const
{
  if (!this->polling_bridge_initialized_) {
    return polling_state_none;
  }
  return this->erd_bridge_poll_.current_state;
}
bool GeappliancesBridge::check_steady_state()
{
  // Steady when: not already reached, subscription bridge is steady (if
  // initialized), polling bridge is polling (if initialized), and at least
  // one bridge has been initialized.
  bool steady = !this->steady_state_reached_ &&
    (!this->subscription_bridge_initialized_ ||
     this->erd_bridge_subscribe_.current_state == subscription_state_steady) &&
    (!this->polling_bridge_initialized_ ||
     this->erd_bridge_poll_.current_state == polling_state_polling) &&
    this->erd_bridge_initialized_;

  if (steady) {
    this->steady_state_reached_ = true;
    ESP_LOGI(TAG, "Appliance Bridge is in steady state (ERDs cached: %u)",
             erd_cache_get_count(&this->erd_cache_));
  }

  return steady;
}


void GeappliancesBridge::maybe_start_custom_erd_polling()
{
  maybe_start_custom_erd_polling_();
}


void GeappliancesBridge::log_poll_state_transitions()
{
  log_poll_state_transitions_();
}

void GeappliancesBridge::run_all_managers()
{
  // FeatureBitManager is self-driving (owns its own timers and event subscriptions).
  // No polling needed from the bridge loop.
}

// -- ERD cache MQTT publisher ------------------------------------------------

void GeappliancesBridge::initialize_erd_cache_publisher()
{
  init_erd_cache_publisher_();
}

bool GeappliancesBridge::is_erd_cache_publisher_initialized() const
{
  return erd_cache_publisher_.cache != nullptr;
}

void GeappliancesBridge::init_erd_cache_publisher_()
{
  if (this->erd_cache_publisher_.cache) return; // already initialized

  /* Apply rate limit configuration before starting the publisher.
   * On ESP-IDF the background task starts immediately in init() and
   * could drain cache entries before the rate limit takes effect. */
  erd_cache_set_throttle_rate_seconds(&this->erd_cache_, this->throttle_rate_seconds_);

  erd_cache_mqtt_publisher_init(
    &this->erd_cache_publisher_,
    &this->erd_cache_,
    &this->mqtt_client_adapter_.interface,
    this->device_identity_manager_.get_device_id());

  // Start the background publishing task on ESP-IDF platforms.
#ifdef USE_ESP_IDF
  erd_cache_mqtt_publisher_start(&this->erd_cache_publisher_);
#endif

  // Initialize the HA discovery manager (lazy-started on steady state).
  ha_discovery_manager_init(&this->ha_discovery_manager_);
}

void GeappliancesBridge::trigger_discovery_refresh()
{
  if (this->discovery_refresh_in_progress_) {
    ESP_LOGW(TAG, "Discovery refresh already in progress, ignoring");
    return;
  }

  if (!this->steady_state_reached_) {
    ESP_LOGW(TAG, "Cannot refresh discovery: appliance bridge not in steady state");
    return;
  }

  // If the discovery manager is still processing from a previous run,
  // wait for it to finish before starting cleanup.
  if (ha_discovery_manager_is_processing(&this->ha_discovery_manager_)) {
    ESP_LOGW(TAG, "Cannot refresh discovery: manager still processing");
    return;
  }

  ESP_LOGI(TAG, "Starting HA discovery cleanup...");

  // Use the embedded cleanup module directly for cleanup-only mode.
#ifdef USE_ESP_IDF
  ha_discovery_cleanup_configure(&this->ha_discovery_manager_.cleanup,
      this->device_identity_manager_.get_device_id(),
      &this->mqtt_client_adapter_.interface, esphome::millis);
  ha_discovery_cleanup_start(&this->ha_discovery_manager_.cleanup);
#else
  (void)this->device_identity_manager_.get_device_id();
  (void)this->mqtt_client_adapter_.interface;
#endif
  this->discovery_refresh_in_progress_ = true;
}

}  // namespace geappliances_bridge
}  // namespace esphome
