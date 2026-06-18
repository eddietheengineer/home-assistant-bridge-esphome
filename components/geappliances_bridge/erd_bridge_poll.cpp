/*!
 * @file
 * @brief ERD polling bridge implementation.
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
 *       → state_probe_api_parsed_erds  (reads each api_parsed_list ERD; only successful reads added)
 *         → state_polling
 */

#include "erd_bridge_common.h"
#include "erd_lists.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include "erd_cache.h"
#include <set>

using namespace std;

static const char* const TAG __attribute__((unused)) = "erd_bridge_poll";

// ============================================================================
// Polling bridge — forward declarations
// ============================================================================

static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_identify_appliance(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_appliance_api_feature_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_probe_api_parsed_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_add_custom_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
static void send_poll_read_requests_bounded(erd_bridge_poll_t* self, uint32_t budget_ms);
static bool send_cycle_reads(erd_bridge_poll_t* self);
static constexpr uint32_t POLL_YIELD_MS = 50;          // per-batch time budget
static constexpr uint32_t POLL_CYCLE_SEND_BUDGET_MS = 500;  // max time per send invocation
static constexpr uint32_t POLL_CYCLE_RESUME_MS = 100;   // timer interval when send budget exceeded

// ============================================================================
// Polling bridge — private helpers
// ============================================================================

static void arm_polling_timer(erd_bridge_poll_t* self, tiny_timer_ticks_t ticks)
{
  self->polling_timer_armed = true;
  tiny_timer_start(
    self->timer_group, &self->polling_timer, ticks, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<erd_bridge_poll_t*>(context)->hsm, signal_polling_timer_expired, nullptr);
    });
}

static void reset_lost_appliance_timer(erd_bridge_poll_t* self)
{
  tiny_timer_stop(self->timer_group, &self->appliance_lost_timer);
  tiny_timer_start(
    self->timer_group, &self->appliance_lost_timer, appliance_lost_timeout, self, +[](void* context) {
      tiny_hsm_send_signal(&reinterpret_cast<erd_bridge_poll_t*>(context)->hsm, signal_appliance_lost, nullptr);
    });
}

// Growth increment for dynamic polling list reallocation.
// Large enough to amortize allocation cost, small enough to avoid wasting heap.
static const uint16_t POLLING_LIST_GROWTH_INCREMENT = 32;

// Allocate or grow the polling list to at least the requested capacity.
// If the list is already large enough, this is a no-op.
// POLLING_LIST_MAX_SIZE is defined in erd_lists.h (included via erd_bridge_common.h).
static void ensure_polling_list_capacity(erd_bridge_poll_t* self, uint16_t needed)
{
  if (needed <= self->polling_list_capacity) {
    return;
  }
  // Compute new_capacity in a wider type to avoid uint16_t overflow before the cap check.
  uint32_t new_capacity = (uint32_t)needed + (POLLING_LIST_GROWTH_INCREMENT - 1);
  // Enforce a hard safety cap to prevent runaway allocations.
  if (new_capacity > POLLING_LIST_MAX_SIZE) {
    new_capacity = POLLING_LIST_MAX_SIZE;
  }
  tiny_erd_t* new_list = new tiny_erd_t[(uint16_t)new_capacity];
  if (self->erd_polling_list) {
    // Copy existing entries.
    for (uint16_t i = 0; i < self->polling_list_count; i++) {
      new_list[i] = self->erd_polling_list[i];
    }
    delete[] self->erd_polling_list;
  }
  self->erd_polling_list = new_list;
  self->polling_list_capacity = (uint16_t)new_capacity;
}

/* Reset the polling bridge's discovery state (erd_set and polling list).
 * Called from the canonical discovery entry points so that a new discovery
 * phase always starts from a clean slate.  There are two callers:
 *   - state_add_common_erds (full-discovery path)
 *   - state_identify_appliance re-entry (api_parsed path after appliance lost)
 * Adding a third caller in the future requires only calling this helper,
 * rather than duplicating the clear sequence.
 *
 * Does NOT clear the shared erd_cache — that cache may be shared with the
 * subscription bridge.  Clearing it here would destroy subscription data
 * when the polling bridge re-discovers after appliance loss. */
static void clear_discovery_state(erd_bridge_poll_t* self)
{
  erd_set(self).clear();
  self->polling_list_count = 0;
}


static void add_erd_to_polling_list(erd_bridge_poll_t* self, tiny_erd_t erd)
{
  if (erd_set(self).find(erd) == erd_set(self).end()) {
    erd_set(self).insert(erd);
    // Ensure there's room in the dynamic array.
    ensure_polling_list_capacity(self, self->polling_list_count + 1);
    self->erd_polling_list[self->polling_list_count] = erd;
    self->polling_list_count++;
  }
}

// Called when all ERDs in the current polling cycle have responded.
// Records cycle metrics and decides whether to start the next cycle
// immediately or wait for the polling timer.  When 'immediate' is true,
// the next cycle starts right away (used when restart_pending is set or
// the timer has already expired).  When 'immediate' is false, only arms
// the timer if it isn't already armed (used from send_next_poll_read_request
// where restart_pending is never set).
static void on_polling_cycle_complete(erd_bridge_poll_t* self, bool immediate)
{
  uint32_t now = esphome::millis();
  self->last_cycle_time_ms = (uint32_t)(now - self->cycle_start_ms);
  self->cycle_count++;

  if (immediate) {
    self->restart_pending = false;
    self->erd_index = 0;
    self->cycle_completed_count = 0;
    self->cycle_start_ms = now;
    if (send_cycle_reads(self)) {
      arm_polling_timer(self, self->polling_interval_ms);
    } else {
      arm_polling_timer(self, POLL_CYCLE_RESUME_MS);
    }
  } else if (!self->polling_timer_armed) {
    arm_polling_timer(self, self->polling_interval_ms);
  }
  // else: timer still armed — wait for it to fire and restart.
}

/* Send the next discovery-phase read request.  This operates on
 * appliance_erd_list / appliance_erd_list_count — NOT on erd_polling_list.
 * It is used exclusively during discovery states (common, energy, appliance
 * API feature, probe, appliance, custom) to advance one ERD at a time.
 * For steady-state polling, see send_next_poll_read_request. */
static bool send_next_read_request(erd_bridge_poll_t* self)
{
  reset_lost_appliance_timer(self);
  self->erd_index++;
  bool more_erds_to_try = (self->erd_index < self->appliance_erd_list_count);
  if (more_erds_to_try) {
    self->request_id++;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
  }
  return more_erds_to_try;
}

static void send_next_poll_read_request(erd_bridge_poll_t* self)
{
  if (self->erd_index < self->polling_list_count) {
    self->request_id++;
    uint32_t t0 = esphome::millis();
    bool queued = tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->erd_polling_list[self->erd_index]);
    uint32_t elapsed = esphome::millis() - t0;
    self->erd_index++;
    if (!queued) {
      /* Queue full — the read was silently dropped. Count it as completed
       * (failed) so the cycle can advance. The ERD will be retried on the
       * next cycle. */
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self, false);
      }
    }
    if (elapsed >= 500) {
      ESP_LOGW(TAG, "Slow read request: %ums for ERD 0x%04x (queued=%s)",
               elapsed, self->erd_polling_list[self->erd_index - 1], queued ? "yes" : "no");
    }
  }
}

// Send remaining poll read requests with a time budget to avoid blocking
// the ESPHome main loop for too long.  With large ERD lists (100+),
// sending all reads synchronously can take hundreds of milliseconds.
// This helper processes in batches, yielding after POLL_YIELD_MS so the
// framework can service other tasks.
//
// POLL_YIELD_MS — per-batch time budget in milliseconds.  Values below 20 ms
// risk excessive context-switch overhead; values above 100 ms may cause
// noticeable blocking for other ESPHome components.  Tune based on appliance
// ERD count and hardware platform.
//
// The caller wraps this in a while loop:
//     while (self->erd_index < self->polling_list_count) {
//       send_poll_read_requests_bounded(self, POLL_YIELD_MS);
//       esphome::delay(0); // yield to main loop
//     }
// so that all ERDs are queued before proceeding.

static void send_poll_read_requests_bounded(erd_bridge_poll_t* self, uint32_t budget_ms)
{
  uint32_t start = esphome::millis();
  uint16_t count = 0;
  while (self->erd_index < self->polling_list_count) {
    send_next_poll_read_request(self);
    count++;
    // Check time budget every 10 ERDs to minimize overhead on small lists.
    if (count % 10 == 0 && (esphome::millis() - start) >= budget_ms) {
      break;
    }
  }
}

/* Send cycle read requests within a time budget.  Does NOT arm any timer —
 * the caller is responsible for arming the next timer based on the return
 * value.  This prevents a single blocking call from holding the main loop
 * long enough to trigger the ESP32 task watchdog timer.
 *
 * Returns true if all reads were sent within budget, false if the budget
 * was exceeded and a resume timer should be armed. */
static bool send_cycle_reads(erd_bridge_poll_t* self)
{
  uint32_t send_start = esphome::millis();
  while (self->erd_index < self->polling_list_count) {
    send_poll_read_requests_bounded(self, POLL_YIELD_MS);
    esphome::delay(0);
    if ((esphome::millis() - send_start) >= POLL_CYCLE_SEND_BUDGET_MS) {
      self->cycle_sending_in_progress = true;
      return false;
    }
  }
  /* All reads sent. */
  self->cycle_sending_in_progress = false;
  uint32_t elapsed = esphome::millis() - send_start;
  if (elapsed >= 1000) {
    ESP_LOGW(TAG, "Long cycle send: %ums for %u ERDs", elapsed, self->polling_list_count);
  }
  return true;
}

// Shared handler for all discovery states (common, energy, appliance API, appliance).
// Each ERD read waits for a definitive GEA-client response before the next is sent.
//
// Contract: callers MUST handle tiny_hsm_signal_entry before delegating here.
// This handler only processes signal_read_completed and signal_read_failed, both
// of which carry non-null data from the GEA client activity callback.  Any signal
// with null data (entry, exit, etc.) is deferred — if a caller forgets to handle
// entry, the signal silently defers rather than causing undefined behavior.
static tiny_hsm_result_t handle_discovery_list_signals(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);
  if (data == nullptr) {
    // Signal with no payload (entry, exit, or unknown).  Callers are
    // responsible for handling entry before reaching this point.
    return tiny_hsm_result_signal_deferred;
  }
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case signal_read_completed:
      add_erd_to_polling_list(self, args->read_completed.erd);
      erd_cache_update(self->erd_cache, args->read_completed.erd,
        reinterpret_cast<const uint8_t*>(args->read_completed.data), args->read_completed.data_size, true);
      if (!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, self->next_discovery_state);
      }
      break;

    case signal_read_failed:
      // Both not_supported and retries_exhausted are definitive — the GEA client
      // has finished its work.  Exclude the ERD from the polling list by simply
      // not adding it.  Do NOT insert into erd_set: that set is a deduplication
      // guard for successfully-added ERDs only.  Inserting failed ERDs here would
      // block the same ERD from being independently re-probed in a later discovery
      // phase (e.g., a custom ERD that shares a code with a failed standard ERD).
      if (!send_next_read_request(self)) {
        tiny_hsm_transition(hsm, self->next_discovery_state);
      }
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}
static tiny_hsm_result_t poll_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, [[maybe_unused]] const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);

  switch (signal) {
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
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case tiny_hsm_signal_entry:
      self->polling_list_complete = false;
      self->current_state_name = "identify_appliance";
      // If the caller pre-initialized the host address (via
      // erd_bridge_poll_init with a non-broadcast address), skip the broadcast
      // and transition directly to the appropriate state.  This is used by the
      // custom ERD bridge (start_custom_erd_polling_) and the main polling bridge
      // (initialize_erd_bridge_), both of which already know the appliance
      // address from autodiscovery.
      if (self->erd_host_address != tiny_gea_broadcast_address) {
        // Re-entry after appliance lost: polling list is non-empty — clear
        // everything so ERDs are re-added via _no_register and lazily
        // re-registered on first read (appliance-lost behavior is deferred).
        // First entry: polling list is empty — skip broadcast and go straight
        // to Phase 2 probe (when api_parsed_list is set) or discovery.
        bool is_reentry = (self->polling_list_count > 0 && self->api_parsed_list != nullptr);
        if (is_reentry) {
          clear_discovery_state(self);
        }
        tiny_hsm_state_t next;
        if (self->api_parsed_list != nullptr) {
          // First entry: verify ERDs via Phase 2 before polling.
          // Re-entry (appliance lost): skip re-probe; go directly to polling.
          next = is_reentry ? state_polling : state_probe_api_parsed_erds;
        } else {
          next = state_add_common_erds;
        }
        tiny_hsm_transition(hsm, next);
        break;
      }
      // Broadcast read for appliance type ERD (0x0008).
      self->request_id++;
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, 0x0008);
      break;

    case signal_read_completed:
      // Ignore reads for ERDs other than the appliance type ERD (0x0008); they
      // are from concurrent activity on the shared bus and must not trigger a
      // premature transition out of identification with erd_host_address still
      // set to the broadcast address (0xFF).
      if (args->read_completed.erd != 0x0008) {
        break;
      }
      reset_lost_appliance_timer(self);
      // NOTE: The appliance type is read from the first response to the
      // broadcast.  In multi-appliance environments, this may not be the
      // intended target; the bridge assumes a single appliance on the bus.
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

    case signal_read_failed:
      // Broadcast read for 0x0008 timed out — retry.
      if (args->read_failed.erd == 0x0008) {
        self->request_id++;
        tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, 0x0008);
      }
      break;

    case tiny_hsm_signal_exit:
      break;

    default:
      return tiny_hsm_result_signal_deferred;
  }

  return tiny_hsm_result_signal_consumed;
}

static tiny_hsm_result_t state_add_common_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);

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
    // the heap.  In the full-discovery path the polling bridge is the only
    // consumer of the shared cache, so it is safe to reset it here.
    erd_cache_init(self->erd_cache);
    clear_discovery_state(self);
    self->request_id++;
    tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_energy_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    self->current_state_name      = "add_energy_erds";
    self->next_discovery_state    = state_add_appliance_api_feature_erds;
    self->appliance_erd_list       = energyErds;
    self->appliance_erd_list_count = energyErdCount;
    self->erd_index                = 0;
    self->request_id++;
    if (self->appliance_erd_list_count > 0) {
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    } else {
      // No energy ERDs to discover; transition directly.
      tiny_hsm_transition(hsm, self->next_discovery_state);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_appliance_api_feature_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    self->current_state_name = "add_appliance_api_feature_erds";
    // When reached via the api_parsed_list path (from
    // state_identify_appliance), probe each ERD in api_parsed_list before
    // polling to filter out ERDs the appliance does not actually support.
    // When reached via the full discovery chain (from state_add_energy_erds),
    // continue to appliance-specific ERDs as before.
    // The clear of erd_set/pending_registration_set/polling_list_count that
    // was previously done here for the api_parsed path is no longer needed:
    // on first entry these are already empty (freshly allocated in init),
    // and on re-entry after appliance_lost, state_identify_appliance clears
    // them before transitioning directly to state_polling, skipping this
    // state entirely.
    if (self->api_parsed_list != nullptr) {
      self->next_discovery_state = state_probe_api_parsed_erds;
    } else {
      self->next_discovery_state = state_add_appliance_erds;
    }
    self->appliance_erd_list       = applianceApiFeatureErds;
    self->appliance_erd_list_count = applianceApiFeatureErdCount;
    self->erd_index                = 0;
    self->request_id++;
    if (self->appliance_erd_list_count > 0) {
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    } else {
      // No appliance API feature ERDs to discover; transition directly.
      tiny_hsm_transition(hsm, self->next_discovery_state);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_probe_api_parsed_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    self->current_state_name      = "probe_api_parsed_erds";
    // If custom ERDs are configured, discover them after api_parsed_list probe.
    self->next_discovery_state    = (self->custom_erd_list != nullptr && self->custom_erd_list_count > 0)
      ? state_add_custom_erds
      : state_polling;
    self->appliance_erd_list       = self->api_parsed_list;
    self->appliance_erd_list_count = self->api_parsed_list_count;
    self->erd_index                = 0;
    self->request_id++;
    if (self->appliance_erd_list_count > 0) {
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    } else {
      // No ERDs to probe; transition directly to polling.
      tiny_hsm_transition(hsm, self->next_discovery_state);
    }
    return tiny_hsm_result_signal_consumed;
  }

  // Phase 2 verification: any failure — whether explicitly rejected
  // (not_supported) or timed out after all retries (retries_exhausted) —
  // permanently excludes the ERD from the Phase 3 polling list.
  if (signal == signal_read_failed) {
    auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);
    erd_set(self).insert(args->read_failed.erd);
    if (!send_next_read_request(self)) {
      tiny_hsm_transition(hsm, self->next_discovery_state);
    }
    return tiny_hsm_result_signal_consumed;
  }
  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_custom_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    self->current_state_name      = "add_custom_erds";
    self->next_discovery_state    = state_polling;
    // Rebuild erd_set from the actual polling list so that custom ERDs are
    // evaluated independently of failures in earlier discovery phases.  A
    // failed ERD from probe or standard discovery should not block the same
    // ERD from being independently probed as a custom ERD.
    erd_set(self).clear();
    for (uint16_t i = 0; i < self->polling_list_count; i++) {
      erd_set(self).insert(self->erd_polling_list[i]);
    }
    self->appliance_erd_list       = self->custom_erd_list;
    self->appliance_erd_list_count = self->custom_erd_list_count;
    self->erd_index                = 0;
    self->request_id++;
    if (self->appliance_erd_list_count > 0) {
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    } else {
      tiny_hsm_transition(hsm, self->next_discovery_state);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_add_appliance_erds(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);

  if (signal == tiny_hsm_signal_entry) {
    // Skip appliance-specific ERD discovery if the type is out of range.
    // Type 0 is a real appliance type (water heater), so clamping to 0
    // would incorrectly poll water heater ERDs for an unknown appliance.
    if (self->appliance_type >= maximumApplianceType) {
      ESP_LOGW(TAG, "Invalid appliance type 0x%02x; skipping appliance-specific ERD discovery",
               self->appliance_type);
      self->appliance_erd_list       = nullptr;
      self->appliance_erd_list_count = 0;
    } else {
      self->appliance_erd_list       = applianceTypeToErdGroupTranslation[self->appliance_type].erdList;
      self->appliance_erd_list_count = applianceTypeToErdGroupTranslation[self->appliance_type].erdCount;
    }
    self->current_state_name      = "add_appliance_erds";
    // If custom ERDs are configured, discover them after appliance ERDs.
    self->next_discovery_state    = (self->custom_erd_list != nullptr && self->custom_erd_list_count > 0)
      ? state_add_custom_erds
      : state_polling;
    self->erd_index                = 0;
    self->request_id++;
    if (self->appliance_erd_list_count > 0) {
      tiny_gea3_erd_client_read(self->erd_client, &self->request_id, self->erd_host_address, self->appliance_erd_list[self->erd_index]);
    } else {
      // No appliance-specific ERDs to discover; transition directly.
      tiny_hsm_transition(hsm, self->next_discovery_state);
    }
    return tiny_hsm_result_signal_consumed;
  }

  return handle_discovery_list_signals(hsm, signal, data);
}

static tiny_hsm_result_t state_polling(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data)
{
  erd_bridge_poll_t* self = container_of(erd_bridge_poll_t, hsm, hsm);
  auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(data);

  switch (signal) {
    case tiny_hsm_signal_entry:
      // api_parsed_list ERDs that were successfully probed in
      // state_probe_api_parsed_erds are already in erd_set and
      // erd_polling_list — the dedup check below silently skips them.
      // ERDs that failed during probe are in erd_set as exclusions, so
      // they are also skipped (correctly excluded from polling).
      // This path is primarily useful on re-entry after appliance lost,
      // where erd_set has been cleared and all api_parsed_list ERDs are
      // re-added to the polling list.
      if (self->api_parsed_list != nullptr) {
        for (uint16_t i = 0; i < self->api_parsed_list_count; i++) {
          add_erd_to_polling_list(self, self->api_parsed_list[i]);
        }
      }
      self->erd_index = 0;
      self->cycle_completed_count = 0;
      self->restart_pending = false;
      arm_polling_timer(self, self->polling_interval_ms);
      self->polling_list_complete = true;
      self->current_state_name    = "polling";
      // Notify startup HSM that discovery is complete.  Safe to call
      // synchronously from inside the polling HSM's state entry because:
      // 1. The callback sends a signal to the *startup* HSM (a different
      //    instance), not the polling HSM.
      // 2. startup_state_subscription_watch entry gates custom ERD polling
      //    on the subscription quiet window, which has not elapsed yet, so
      //    maybe_start_custom_erd_polling() returns early without touching
      //    the polling bridge.
      // 3. startup_state_ha_discovery entry only logs.
      // Invariant: the callback must never trigger a path that sends a
      // signal back to the polling HSM while this entry handler runs.
      if (self->on_discovery_complete != nullptr) {
        self->on_discovery_complete(self->on_discovery_complete_context);
      }
      break;
    case signal_polling_timer_expired: {
      // Timer fired: mark as no longer armed.
      self->polling_timer_armed = false;
      if (self->erd_index >= self->polling_list_count && self->cycle_completed_count < self->polling_list_count) {
        // Reads are in flight (erd_index reached the end of the list) but
        // not all responses have arrived yet.  Per Phase 3 spec, let the
        // current cycle finish naturally before restarting.  If erd_index
        // is 0 the cycle has not started; the timer correctly starts it below.
        // Mark restart as pending so the cycle-completion handler kicks off
        // the next cycle as soon as the last ERD responds.
        self->restart_pending = true;
        break;
      }
      // If we were in the middle of sending reads for a cycle, resume.
      bool all_sent;
      if (self->cycle_sending_in_progress) {
        all_sent = send_cycle_reads(self);
        if (all_sent) {
          arm_polling_timer(self, self->polling_interval_ms);
        } else {
          arm_polling_timer(self, POLL_CYCLE_RESUME_MS);
        }
      } else {
        /* Cycle was already complete when the timer fired — start next cycle. */
        self->erd_index = 0;
        self->cycle_completed_count = 0;
        uint32_t now = esphome::millis();
        self->cycle_start_ms = now;
        all_sent = send_cycle_reads(self);
        if (all_sent) {
          arm_polling_timer(self, self->polling_interval_ms);
        } else {
          arm_polling_timer(self, POLL_CYCLE_RESUME_MS);
        }
      }
      break;
    }

    case signal_read_completed: {
      reset_lost_appliance_timer(self);
      tiny_erd_t      erd       = args->read_completed.erd;
      const uint8_t*  erd_data  = reinterpret_cast<const uint8_t*>(args->read_completed.data);
      uint8_t         data_size = args->read_completed.data_size;

      if (erd_set(self).find(erd) == erd_set(self).end()) {
        add_erd_to_polling_list(self, erd);
      }

      erd_cache_update(self->erd_cache, erd, erd_data, data_size, false);
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self, self->restart_pending || !self->polling_timer_armed);
      }
      break;
    }

    case signal_read_failed: {
      reset_lost_appliance_timer(self);
      ESP_LOGD(TAG, "Read failed for ERD 0x%04x", args->read_failed.erd);
      self->cycle_completed_count++;
      if (self->cycle_completed_count >= self->polling_list_count) {
        on_polling_cycle_complete(self, self->restart_pending || !self->polling_timer_armed);
      }
      break;
    }

    case tiny_hsm_signal_exit:
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
  { .state = state_probe_api_parsed_erds,         .parent = poll_state_top  },
  { .state = state_add_custom_erds,               .parent = poll_state_top  },
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
static void erd_bridge_poll_init_impl(
  erd_bridge_poll_t*    self,
  tiny_timer_group_t*       timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint32_t                  polling_interval_ms,
  uint8_t                   initial_host_address,
  uint8_t                   initial_appliance_type,
  const tiny_erd_t*         api_parsed_list,
  uint16_t                  api_parsed_list_count,
  erd_cache_t*              cache)
{
  self->timer_group            = timer_group;
  self->erd_client             = erd_client;
  self->polling_interval_ms    = polling_interval_ms;
  // Must be set before tiny_hsm_init() so state_identify_appliance entry
  // can decide whether to broadcast or skip straight to discovery/polling.
  self->erd_host_address       = initial_host_address;
  self->appliance_type         = initial_appliance_type;
  // Store the pre-known address so that signal_appliance_lost can restore it
  // after a transient read failure instead of falling back to 0xFF broadcast.
  // Zero means "unknown — use broadcast" (set by erd_bridge_poll_init()).
  self->known_host_address     = (initial_host_address != tiny_gea_broadcast_address)
    ? initial_host_address : 0;
  self->api_parsed_list        = api_parsed_list;
  self->api_parsed_list_count  = api_parsed_list_count;
  self->custom_erd_list        = nullptr;
  self->erd_polling_list       = nullptr;
  self->polling_list_count     = 0;
  self->polling_list_capacity  = 0;
  self->restart_pending             = false;
  self->cycle_sending_in_progress   = false;
  self->polling_timer_armed         = false;
  self->current_state_name          = nullptr;
  self->polling_list_complete       = false;
  self->cycle_start_ms              = 0;
  self->last_cycle_time_ms          = 0;
  self->cycle_count                 = 0;
  // Initialize to nullptr so that if the new below throws,
  // erd_bridge_poll_destroy() will safely skip the delete.
  self->erd_set = nullptr;
  self->erd_set = reinterpret_cast<void*>(new set<tiny_erd_t>());
  self->erd_cache = cache;
  self->on_discovery_complete        = nullptr;
  self->on_discovery_complete_context = nullptr;

  tiny_event_subscription_init(
    &self->erd_client_activity_subscription, self, +[](void* context, const void* _args) {
      auto self = reinterpret_cast<erd_bridge_poll_t*>(context);
      auto args = reinterpret_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);

      switch (args->type) {
        case tiny_gea3_erd_client_activity_type_read_completed:
          tiny_hsm_send_signal(&self->hsm, signal_read_completed, args);
          break;
        case tiny_gea3_erd_client_activity_type_read_failed:
          tiny_hsm_send_signal(&self->hsm, signal_read_failed, args);
          break;
      }
    });
  tiny_event_subscribe(tiny_gea3_erd_client_on_activity(erd_client), &self->erd_client_activity_subscription);

  tiny_hsm_init(&self->hsm, &poll_hsm_configuration, state_identify_appliance);
}

void erd_bridge_poll_init(
  erd_bridge_poll_t*    self,
  tiny_timer_group_t*       timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint32_t                  polling_interval_ms,
  uint8_t                   host_address,
  uint8_t                   appliance_type,
  const tiny_erd_t*         api_list,
  uint16_t                  api_list_count,
  erd_cache_t*              cache)
{
  erd_bridge_poll_init_impl(
    self, timer_group, erd_client, polling_interval_ms,
    host_address, appliance_type, api_list, api_list_count, cache);
}

void erd_bridge_poll_destroy(erd_bridge_poll_t* self)
{
  // Guard against destroy() being called on a never-initialized struct (e.g.
  // in test teardowns that always call both bridge and polling destroy).
  if (!self->timer_group) {
    return;
  }

  // Stop all active timers so they cannot fire after the bridge is torn down.
  // tiny_timer_stop() is idempotent: safe to call even if a timer is not active.
  tiny_timer_stop(self->timer_group, &self->appliance_lost_timer);
  tiny_timer_stop(self->timer_group, &self->polling_timer);

  // Remove event subscription before freeing heap state.
  // erd_bridge_poll_init() subscribes one event callback that references
  // this struct: erd_client_activity_subscription.  If this remains
  // registered after destroy(), any subsequent event fires the HSM which
  // dereferences self->erd_set (freed below) — a use-after-free that
  // corrupts the heap.
  tiny_event_unsubscribe(
    tiny_gea3_erd_client_on_activity(self->erd_client),
    &self->erd_client_activity_subscription);

  // Guard against partial init (e.g., if the first new set<tiny_erd_t>()
  // failed and init returned early).  delete nullptr is safe in C++, but
  // the reinterpret_cast from a non-null garbage pointer is not.
  if (self->erd_set) {
    delete reinterpret_cast<set<tiny_erd_t>*>(self->erd_set);
    self->erd_set = nullptr;
  }

  if (self->erd_polling_list != nullptr) {
    delete[] self->erd_polling_list;
    self->erd_polling_list = nullptr;
  }
  self->polling_list_count = 0;
  self->polling_list_capacity = 0;
}

