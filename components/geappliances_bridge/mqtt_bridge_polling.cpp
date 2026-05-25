/*!
 * @file
 * @brief MQTT polling bridge implementation.
 *
 * The polling bridge discovers the connected appliance by reading ERD 0x0008
 * (appliance type) on the broadcast address, then walks through a chain of
 * per-appliance ERD discovery states before settling into steady-state polling.
 *
 * State machine:
 *   state_identify_appliance
 *     → state_add_common_erds   (when no api_parsed_list)
 *       → state_add_energy_erds
 *         → state_add_appliance_api_feature_erds
 *           → state_add_appliance_erds → state_polling
 *     → state_add_appliance_api_feature_erds  (when api_parsed_list is set)
 *       → state_polling
 */

#include "mqtt_bridge_common.h"
#include "erd_lists.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
#include <map>
#include <set>
#include <vector>

using namespace std;

static const char* const TAG __attribute__((unused)) = "mqtt_bridge_polling";

// ============================================================================
// Polling bridge — forward declarations
// ============================================================================

static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_identify_appliance(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_appliance_api_feature_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

// ============================================================================
// Polling bridge — private helpers
// ============================================================================

static map<tiny_erd_t, vector<uint8_t>>& erd_cache(mqtt_bridge_polling_t* self)
{
  return *reinterpret_cast<map<tiny_erd_t, vector<uint8_t>>*>(self->erd_cache);
}

static void arm_polling_timer(mqtt_bridge_polling_t* self, tiny_timer_ticks_t ticks)
{
  tiny_timer_start(
    self->timer_group, &self->polling_timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<mqtt_bridge_polling_t*>(context)->hsm, signal_polling_timer_expired, nullptr);
    });
}

static void reset_lost_appliance_timer(mqtt_bridge_polling_t* self)
{
  tiny_timer_stop(self->timer_group, &self->appliance_lost_timer);
  tiny_timer_start(
    self->timer_group, &self->appliance_lost_timer, appliance_lost_timeout, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<mqtt_bridge_polling_t*>(context)->hsm, signal_appliance_lost, nullptr);
    });
}

// Growth increment for dynamic polling list reallocation.
// Large enough to amortize allocation cost, small enough to avoid wasting heap.
static const uint16_t POLLING_LIST_GROWTH_INCREMENT = 32;

// Allocate or grow the polling list to at least the requested capacity.
// If the list is already large enough, this is a no-op.
static void ensure_polling_list_capacity(mqtt_bridge_polling_t* self, uint16_t needed)
{
  if (needed <= self->polling_list_capacity) {
    return;
  }
  // Grow to the needed size, rounded up to the next growth increment.
  uint16_t new_capacity = needed + (POLLING_LIST_GROWTH_INCREMENT - 1);
  // Enforce a hard safety cap to prevent runaway allocations.
  if (new_capacity > POLLING_LIST_MAX_SIZE) {
    new_capacity = POLLING_LIST_MAX_SIZE;
  }
  tiny_erd_t* new_list = new tiny_erd_t[new_capacity];
  if (self->erd_polling_list) {
    // Copy existing entries.
    for (uint16_t i = 0; i < self->polling_list_count; i++) {
      new_list[i] = self->erd_polling_list[i];
    }
    delete[] self->erd_polling_list;
  }
  self->erd_polling_list = new_list;
  self->polling_list_capacity = new_capacity;
}

static set<tiny_erd_t>& pending_registration_set(mqtt_bridge_polling_t* self)
{
  return *reinterpret_cast<set<tiny_erd_t>*>(self->pending_registration_set);
}

static void add_erd_to_polling_list_no_register(mqtt_bridge_polling_t* self, tiny_erd_t erd)
{
  /* Add to erd_set to prevent add_erd_to_polling_list() from treating this
   * as a new ERD.  Add to pending_registration_set so signal_read_completed
   * knows to register on MQTT.  Do NOT register on MQTT yet — that's deferred
   * until the ERD is first successfully read. */
  if (erd_set(self).find(erd) == erd_set(self).end()) {
    erd_set(self).insert(erd);
    pending_registration_set(self).insert(erd);
    ensure_polling_list_capacity(self, self->polling_list_count + 1);
    self->erd_polling_list[self->polling_list_count] = erd;
    self->polling_list_count++;
  }
}

static void add_erd_to_polling_list(mqtt_bridge_polling_t* self, tiny_erd_t erd)
{
  if (erd_set(self).find(erd) == erd_set(self).end()) {
    mqtt_client_register_erd(self->mqtt_client, erd);
    erd_set(self).insert(erd);
    // Ensure there's room in the dynamic array.
    ensure_polling_list_capacity(self, self->polling_list_count + 1);
    self->erd_polling_list[self->polling_list_count] = erd;
    self->polling_list_count++;
  }
}

static bool send_next_read_request(mqtt_bridge_polling_t* self)
{
  reset_lost_appliance_timer(self);
  self->erd_index++;
  bool more_erds_to_try = (self->erd_index < self->appliance_erd_list_count);
  if (more_erds_to_try) {
    self->request_id++;
    bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    /* If the ERD client queue is full (e.g., because a subscription bridge
     * is sharing the same client and flooding it with acknowledgments),
     * don't arm the retry timer — that would compound the queue pressure
     * and risk ring-buffer overflow corrupting adjacent heap metadata.
     * The retry timer will be armed by the next signal_read_completed or
     * signal_timer_expired that arrives once the queue drains. */
    if (queued) {
      arm_timer(self, retry_delay);
    }
  }
  return more_erds_to_try;
}

static void send_next_poll_read_request(mqtt_bridge_polling_t* self)
{
  if (self->erd_index < self->polling_list_count) {
    self->request_id++;
    // Fire-and-forget: always advance erd_index and arm the retry timer,
    // regardless of whether the queue accepted the read. This allows
    // simultaneous reads during steady-state polling, significantly
    // improving throughput when the shared ERD client queue is under
    // pressure from subscription traffic. The 8KB queue provides enough
    // headroom to handle bursts of simultaneous reads without overflow.
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->erd_polling_list[self->erd_index]);
    self->erd_index++;
    arm_timer(self, retry_delay);
  }
}

// Shared entry handler for all discovery states (common, energy, appliance API, appliance).
static tiny_hsm_result_t handle_discovery_list_signals(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case signal_timer_expired:
      if (!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, self->next_discovery_state);
      }
      break;

    case signal_read_completed:
      disarm_timer(self);
      add_erd_to_polling_list(self, args->read_completed.erd);
      mqtt_client_update_erd(
        self->mqtt_client,
        args->read_completed.erd,
        args->read_completed.data,
        args->read_completed.data_size);
      if (!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, self->next_discovery_state);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Polling bridge — state handlers
// ============================================================================

static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  switch (signal) {
    case signal_write_requested: {
      auto args = reinterpret_cast<const mqtt_client_on_write_request_args_t*>(data);
      tiny_gea3_erd_client_write(self->erd_client, &self->request_id, self->erd_host_address, args->erd, args->value, args->size);
    } break;

    case signal_appliance_lost:
      // When a pre-known host address was supplied at init (custom ERD bridge
      // alongside a subscription bridge), preserve it on re-identification so
      // that state_identify_appliance skips the 0xFF broadcast and resumes
      // polling directly at the correct address.  For a normal polling bridge
      // (known_host_address == 0) fall back to broadcast discovery as usual.
      self->erd_host_address = (self->known_host_address != 0)
        ? self->known_host_address
        : static_cast<uint8_t>(tiny_gea_broadcast_address);
      tiny_hsm_transition(hsm, state_identify_appliance);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_identify_appliance(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case tiny_hsm_signal_entry:
      self->polling_list_complete = false;
      self->current_state_name = "identify_appliance";
      // If the caller pre-initialized the host address (via
      // mqtt_bridge_polling_init_at_address), skip the broadcast and transition
      // directly to the appropriate state.  This is used by the custom ERD bridge
      // (start_custom_erd_polling_) which already knows the appliance address and
      // supplies an api_parsed_list (the custom ERDs), so full discovery and
      // feature-bit reads are unnecessary.
      if (self->erd_host_address != tiny_gea_broadcast_address) {
        // If we already have a polling list (re-entry after appliance lost),
        // clear everything so ERDs are re-added via _no_register and will
        // be re-registered on first read.
        if (self->polling_list_count > 0 && self->api_parsed_list != nullptr) {
          erd_set(self).clear();
          pending_registration_set(self).clear();
          self->polling_list_count = 0;
        }
        tiny_hsm_state_t next = (self->api_parsed_list != nullptr)
          ? state_polling
          : state_add_common_erds;
        tiny_hsm_transition(hsm, next);
        break;
      }
      __attribute__((fallthrough));

    case signal_timer_expired: {
      self->request_id++;
      bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, 0x0008);
      if (queued) {
        arm_timer(self, retry_delay);
      }
    } break;

    case signal_read_completed:
      // Ignore reads for ERDs other than the appliance type ERD (0x0008); they
      // are from concurrent activity on the shared bus and must not trigger a
      // premature transition out of identification with erd_host_address still
      // set to the broadcast address (0xFF).
      if (args->read_completed.erd != 0x0008) {
        break;
      }
      disarm_timer(self);
      reset_lost_appliance_timer(self);
      if (args->read_completed.data_size >= 1) {
        self->erd_host_address = args->address;
        self->appliance_type = *reinterpret_cast<const uint8_t*>(args->read_completed.data);
      }
      // If an API-parsed ERD list is available, skip straight to the appliance
      // API feature ERD discovery state and then into polling. Otherwise run the
      // full chain: common → energy → appliance_api_feature → appliance.
      if (self->api_parsed_list != nullptr) {
        tiny_hsm_transition(hsm, state_add_appliance_api_feature_erds);
      } else {
        tiny_hsm_transition(hsm, state_add_common_erds);
      }
      break;

    case tiny_hsm_signal_exit:
      disarm_timer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    self->current_state_name      = "add_common_erds";
    self->next_discovery_state    = state_add_energy_erds;
    self->appliance_erd_list       = commonErds;
    self->appliance_erd_list_count = commonErdCount;
    self->erd_index                = 0;
    // Reset the polling list and the erd_set that deduplicates it. This is
    // the correct place to clear erd_set: it only runs in the full-discovery
    // path (appliance first seen, or appliance_lost re-discovery), NOT on every
    // transient MQTT reconnect. Clearing in the disconnect handler caused the
    // set's tree nodes to be freed and reallocated on each reconnect, fragmenting
    // the heap over time.
    erd_set(self).clear();
    pending_registration_set(self).clear();
    self->polling_list_count       = 0;
    self->request_id++;
    bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    if (queued) {
      arm_timer(self, retry_delay);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    self->current_state_name      = "add_energy_erds";
    self->next_discovery_state    = state_add_appliance_api_feature_erds;
    self->appliance_erd_list       = energyErds;
    self->appliance_erd_list_count = energyErdCount;
    self->erd_index                = 0;
    self->request_id++;
    bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    if (queued) {
      arm_timer(self, retry_delay);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_appliance_api_feature_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    self->current_state_name = "add_appliance_api_feature_erds";
    // When reached via the api_parsed_list path (from state_identify_appliance),
    // go directly to polling after the mandatory feature-bit ERDs.
    // When reached via the full discovery chain (from state_add_energy_erds),
    // continue to appliance-specific ERDs.
    self->next_discovery_state    = (self->api_parsed_list != nullptr) ? state_polling : state_add_appliance_erds;
    self->appliance_erd_list       = applianceApiFeatureErds;
    self->appliance_erd_list_count = applianceApiFeatureErdCount;
    self->erd_index                = 0;
    self->request_id++;
    bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    if (queued) {
      arm_timer(self, retry_delay);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    if (self->appliance_type >= maximumApplianceType) {
      self->appliance_type = 0;
    }
    self->current_state_name      = "add_appliance_erds";
    self->next_discovery_state    = state_polling;
    self->appliance_erd_list       = applianceTypeToErdGroupTranslation[self->appliance_type].erdList;
    self->appliance_erd_list_count = applianceTypeToErdGroupTranslation[self->appliance_type].erdCount;
    self->erd_index                = 0;
    self->request_id++;
    bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    if (queued) {
      arm_timer(self, retry_delay);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  mqtt_bridge_polling_t* self = container_of(mqtt_bridge_polling_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case tiny_hsm_signal_entry:
      erd_cache(self).clear();
      // When using an API-parsed list, add all ERDs to the polling list
      // without registering them yet — they'll be registered on first
      // successful read, just like the discovery path.
      if (self->api_parsed_list != nullptr) {
        for (uint16_t i = 0; i < self->api_parsed_list_count; i++) {
          add_erd_to_polling_list_no_register(self, self->api_parsed_list[i]);
        }
      }
      // Add user-configured custom ERDs to the polling list without
      // registering them yet — same reasoning as above.
      if (self->custom_erd_list != nullptr) {
        for (uint16_t i = 0; i < self->custom_erd_list_count; i++) {
          add_erd_to_polling_list_no_register(self, self->custom_erd_list[i]);
        }
      }
      // Pin erd_index to polling_list_count so that the first
      // signal_polling_timer_expired detects this as the "first cycle" and
      // resets to 0, starting the first read from ERD[0].
      //
      // This also corrects the "first N ERDs skipped" bug from the full-discovery
      // path: previous HSM states (state_add_*) leave erd_index pointing at the
      // end of the *last* discovery list, which can be less than polling_list_count
      // (the sum of all discovered ERDs across all states).  Forcing erd_index to
      // polling_list_count ensures signal_polling_timer_expired always resets to 0
      // and starts from ERD[0] on the very first polling pass.
      self->erd_index = self->polling_list_count;
      self->cycle_completed_count = 0;
      arm_polling_timer(self, self->polling_interval_ms);
      self->polling_list_complete = true;
      self->current_state_name    = "polling";
      break;

    case signal_timer_expired:
      // Retry timer fired — a read is still in-flight after retry_delay.
      // Do NOT resend; the ERD client handles retries internally (10 × 250ms).
      // The retry timer is armed in send_next_poll_read_request() as a safety
      // net and is disarmed in signal_read_completed / signal_read_failed.
      // If we reach here, the read is still pending — just re-arm the timer.
      arm_timer(self, retry_delay);
      break;

    case signal_polling_timer_expired: {
      // With simultaneous reads, the polling timer fires all reads at once
      // when starting a new cycle. Forward progress during the cycle is driven
      // by signal_read_completed / signal_read_failed calling
      // send_next_poll_read_request() for the next ERD in sequence.
      bool first_cycle = (self->erd_index == self->polling_list_count);
      bool all_completed = (self->cycle_completed_count >= self->polling_list_count);
      if (first_cycle || all_completed) {
        self->erd_index = 0;
        self->cycle_completed_count = 0;
        self->cycle_start_ms = esphome::millis();
        // Fire all reads simultaneously by calling send_next_poll_read_request
        // for each ERD in the polling list.
        while (self->erd_index < self->polling_list_count) {
          send_next_poll_read_request(self);
        }
      }
      // Always re-arm the polling timer so the next cycle boundary is tracked
      // even when the current cycle is still in progress.
      arm_polling_timer(self, self->polling_interval_ms);
      break;
    }

    case signal_read_completed: {
      disarm_timer(self);
      reset_lost_appliance_timer(self);
      tiny_erd_t      erd       = args->read_completed.erd;
      const uint8_t*  erd_data  = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t         data_size = args->read_completed.data_size;

      // If the ERD is in pending_registration_set (added via _no_register in
      // entry), register it on MQTT now — confirming it's present on the
      // appliance.  If not in erd_set at all, it's a late discovery response.
      if (pending_registration_set(self).find(erd) != pending_registration_set(self).end()) {
        mqtt_client_register_erd(self->mqtt_client, erd);
        pending_registration_set(self).erase(erd);
      } else if (erd_set(self).find(erd) == erd_set(self).end()) {
        add_erd_to_polling_list(self, erd);
      }

      bool should_publish;
      if (self->only_publish_on_change) {
        auto& cache = erd_cache(self);
        auto  it    = cache.find(erd);
        bool  data_changed;
        if (it == cache.end()) {
          data_changed = true;
        } else {
          data_changed = (it->second.size() != data_size) ||
                         (memcmp(it->second.data(), erd_data, data_size) != 0);
        }
        if (data_changed) {
          cache[erd] = vector<uint8_t>(erd_data, erd_data + data_size);
        }
        should_publish = data_changed;
      } else {
        should_publish = true;
      }

      if (should_publish) {
        mqtt_client_update_erd(self->mqtt_client, erd, erd_data, data_size);
      }
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        self->last_cycle_time_ms = (uint32_t)(esphome::millis() - self->cycle_start_ms);
        self->cycle_count++;
      }
      // With simultaneous reads, all ERDs were already fired from the polling
      // timer. Do NOT call send_next_poll_read_request() here — the next cycle
      // will be started by the polling timer when all ERDs have completed.
      break;
    }

    case signal_read_failed:
      // A read failed (all retries exhausted).  Count it as completed so the
      // cycle can advance — the polling timer will restart the cycle from
      // index 0 once all ERDs have either succeeded or failed.
      disarm_timer(self);
      reset_lost_appliance_timer(self);
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        self->last_cycle_time_ms = (uint32_t)(esphome::millis() - self->cycle_start_ms);
        self->cycle_count++;
      }
      // With simultaneous reads, all ERDs were already fired from the polling
      // timer. Do NOT call send_next_poll_read_request() here.
      break;

    case signal_mqtt_disconnected:
      // MQTT broker lost connection; the appliance is still on the GEA bus
      // and already identified.  Continue polling — ERD values are queued in
      // pending_updates and flushed to MQTT when the broker reconnects.
      // Transitioning to state_identify_appliance here would unnecessarily
      // broadcast to 0xFF and re-register all ERDs, causing multi-second
      // delays and spurious re-registrations on every MQTT blip.
      break;

    case tiny_hsm_signal_exit:
      disarm_timer(self);
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

// ============================================================================
// Polling bridge — HSM configuration
// ============================================================================

static const tiny_hsm_state_descriptor_t poll_hsm_state_descriptors[] = {
  { .state = poll_state_top,                      .parent = nullptr         },
  { .state = state_identify_appliance,            .parent = poll_state_top  },
  { .state = state_add_common_erds,               .parent = poll_state_top  },
  { .state = state_add_energy_erds,               .parent = poll_state_top  },
  { .state = state_add_appliance_api_feature_erds,.parent = poll_state_top  },
  { .state = state_add_appliance_erds,            .parent = poll_state_top  },
  { .state = state_polling,                       .parent = poll_state_top  }
};
static const tiny_hsm_configuration_t poll_hsm_configuration = {
  .states      = poll_hsm_state_descriptors,
  .state_count = element_count(poll_hsm_state_descriptors)
};

// ============================================================================
// Polling bridge — public API
// ============================================================================

// Shared initialization helper.  Sets self->erd_host_address = initial_host_address
// and self->api_parsed_list BEFORE calling tiny_hsm_init(), so that
// state_identify_appliance's entry signal can inspect them and skip the
// broadcast when the host is already known.
static void mqtt_bridge_polling_init_impl(
  mqtt_bridge_polling_t*    self,
  tiny_timer_group_t*       timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t*          mqtt_client,
  uint32_t                  polling_interval_ms,
  bool                      only_publish_on_change,
  uint8_t                   initial_host_address,
  const tiny_erd_t*         api_parsed_list,
  uint16_t                  api_parsed_list_count)
{
  self->timer_group            = timer_group;
  self->erd_client             = erd_client;
  self->mqtt_client            = mqtt_client;
  self->polling_interval_ms    = polling_interval_ms;
  self->only_publish_on_change = only_publish_on_change;
  // Must be set before tiny_hsm_init() so state_identify_appliance entry
  // can decide whether to broadcast or skip straight to discovery/polling.
  self->erd_host_address       = initial_host_address;
  // Store the pre-known address so that signal_appliance_lost can restore it
  // after a transient read failure instead of falling back to 0xFF broadcast.
  // Zero means "unknown — use broadcast" (set by mqtt_bridge_polling_init()).
  self->known_host_address     = (initial_host_address != tiny_gea_broadcast_address)
    ? initial_host_address : 0;
  self->api_parsed_list        = api_parsed_list;
  self->api_parsed_list_count  = api_parsed_list_count;
  self->custom_erd_list        = nullptr;
  self->custom_erd_list_count  = 0;
  self->erd_polling_list       = nullptr;
  self->polling_list_count     = 0;
  self->polling_list_capacity  = 0;
  self->erd_set   = reinterpret_cast<void*>(new set<tiny_erd_t>());
  self->erd_cache = reinterpret_cast<void*>(new map<tiny_erd_t, vector<uint8_t>>());
  self->pending_registration_set = reinterpret_cast<void*>(new set<tiny_erd_t>());

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<mqtt_bridge_polling_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      switch (args->type) {
        case tiny_gea3_erd_client_activity_type_read_completed:
          tiny_hsm_send_signal(&self->hsm, signal_read_completed, args);
          break;
        case tiny_gea3_erd_client_activity_type_read_failed:
          tiny_hsm_send_signal(&self->hsm, signal_read_failed, args);
          break;
        case tiny_gea3_erd_client_activity_type_write_completed:
        case tiny_gea3_erd_client_activity_type_write_failed:
          handle_write_result(self->mqtt_client, args);
          break;
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  setup_write_request_subscription(self, mqtt_client);
  setup_disconnect_subscription(self, mqtt_client);

  tiny_hsm_init(&self->hsm, &poll_hsm_configuration, state_identify_appliance);
}

void mqtt_bridge_polling_init(
  mqtt_bridge_polling_t*    self,
  tiny_timer_group_t*       timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t*          mqtt_client,
  uint32_t                  polling_interval_ms,
  bool                      only_publish_on_change)
{
  mqtt_bridge_polling_init_impl(
    self, timer_group, erd_client, mqtt_client, polling_interval_ms, only_publish_on_change,
    tiny_gea_broadcast_address, nullptr, 0);
}

void mqtt_bridge_polling_init_at_address(
  mqtt_bridge_polling_t*    self,
  tiny_timer_group_t*       timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t*          mqtt_client,
  uint32_t                  polling_interval_ms,
  bool                      only_publish_on_change,
  uint8_t                   known_host_address,
  const tiny_erd_t*         api_list,
  uint16_t                  api_list_count)
{
  mqtt_bridge_polling_init_impl(
    self, timer_group, erd_client, mqtt_client, polling_interval_ms, only_publish_on_change,
    known_host_address, api_list, api_list_count);
}

void mqtt_bridge_polling_destroy(mqtt_bridge_polling_t* self)
{
  // Guard against destroy() being called on a never-initialized struct (e.g.
  // in test teardowns that always call both bridge and polling destroy).
  if (!self->timer_group) {
    return;
  }

  // Stop all active timers so they cannot fire after the bridge is torn down.
  // tiny_timer_stop() is idempotent: safe to call even if a timer is not active.
  tiny_timer_stop(self->timer_group, &self->timer);
  tiny_timer_stop(self->timer_group, &self->appliance_lost_timer);
  tiny_timer_stop(self->timer_group, &self->polling_timer);

  // Remove all event subscriptions before freeing heap state.
  //
  // mqtt_bridge_polling_init() subscribes three event callbacks that reference
  // this struct: erd_client_activity_subscription,
  // mqtt_write_request_subscription, and mqtt_disconnect_subscription.  If
  // these remain registered after destroy(), any subsequent event fires the
  // HSM which dereferences self->erd_set / self->erd_cache (freed below) — a
  // use-after-free that corrupts the heap.
  tiny_event_unsubscribe(
    tiny_gea3_erd_client_on_activity(self->erd_client),
    &self->erd_client_activity_subscription);
  tiny_event_unsubscribe(
    mqtt_client_on_write_request(self->mqtt_client),
    &self->mqtt_write_request_subscription);
  tiny_event_unsubscribe(
    mqtt_client_on_mqtt_disconnect(self->mqtt_client),
    &self->mqtt_disconnect_subscription);

  delete reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
  delete reinterpret_cast<map<tiny_erd_t, vector<uint8_t>>*>(self->erd_cache);
  delete reinterpret_cast<set<tiny_erd_t>*>(self->pending_registration_set);
  self->erd_set = nullptr;
  self->erd_cache = nullptr;
  self->pending_registration_set = nullptr;

  // Free the dynamically allocated polling list.
  delete[] self->erd_polling_list;
  self->erd_polling_list = nullptr;
  self->polling_list_count = 0;
  self->polling_list_capacity = 0;
}
