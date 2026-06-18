# ERD Bridge Poll — Specification

## 1. Overview

### 1.1 Purpose

The ERD polling bridge discovers the connected GE appliance, determines which ERDs (Entity-Relationship Data points) it supports, and periodically reads their values — writing them to the shared ERD cache. It has **zero** direct interaction with the MQTT client.

### 1.2 Responsibilities

- Discover the appliance's host address via broadcast (or accept a pre-known address)
- Determine which ERDs the appliance supports through sequential verification reads
- Maintain a dynamic polling list of verified ERDs
- Execute steady-state polling cycles at a configured interval
- Write ERD values to the shared ERD cache (change detection via `erd_cache_update`)
- Recover from appliance loss

### 1.3 Not Responsible For

- Subscription-mode operation (see `erd_bridge_subscribe`)
- Any MQTT behavior (publishing, write requests, disconnect handling)
- Deciding which ERDs are valid (filtered upstream by the MQTT client adapter)
- Bridge startup phase management (see `geappliances_bridge_startup_hsm`)

---

## 2. Initialization

### 2.1 Primary Init

```c
void erd_bridge_poll_init(
    erd_bridge_poll_t* self,
    tiny_timer_group_t* timer_group,
    i_tiny_gea3_erd_client_t* erd_client,
    uint32_t polling_interval_ms,
    uint8_t host_address,
    uint8_t appliance_type,
    const tiny_erd_t* api_list,
    uint16_t api_list_count,
    erd_cache_t* cache);
```

| Parameter | Description |
|-----------|-------------|
| `host_address` | The appliance's GEA bus address. If `tiny_gea_broadcast_address` (0xFF), the bridge performs broadcast discovery. Otherwise, it skips broadcast and proceeds directly to discovery/polling. |
| `appliance_type` | The appliance type byte from ERD 0x0008. Used to select appliance-specific ERD lists. |
| `api_list` / `api_list_count` | Optional pre-populated ERD list from appliance API feature bit parsing. When non-NULL, the bridge probes each ERD before polling (verifying support). When NULL, the bridge runs the full discovery chain. |
| `cache` | Shared `erd_cache_t` for storing ERD values. |

### 2.2 Destroy

```c
void erd_bridge_poll_destroy(erd_bridge_poll_t* self);
```

Stops timers, unsubscribes all event handlers, and frees heap-allocated state (`erd_set`, `erd_polling_list`). Guards against being called on a never-initialized struct or a partially-initialized one (e.g., if a `new` allocation failed during init).

### 2.3 Custom ERD List

After init, the caller may set `self->custom_erd_list` and `self->custom_erd_list_count` to configure user-defined ERDs. These are discovered after the standard ERD lists and appended to the polling list.

### 2.4 Discovery-Complete Callback

After init, the caller may set `self->on_discovery_complete` and `self->on_discovery_complete_context`. This callback fires once when the HSM enters `state_polling` (discovery complete). The callback must not send signals back to the polling HSM. It should iterate the cache via `erd_cache_get_next_entry()` to build the ERD set for HA discovery.

## 3. State Machine

The polling bridge uses a hierarchical state machine (`tiny_hsm`) with a parent state (`poll_state_top`) and eight child states.

### 3.1 Parent State: `poll_state_top`

Handles signals that apply regardless of the current child state:

| Signal | Behavior |
|--------|----------|
| `signal_appliance_lost` | Fires after `appliance_lost_timeout` (60 s) with no successful reads. Restores `erd_host_address` to `known_host_address` if set, or to broadcast. Transitions to `state_identify_appliance`. |

### 3.2 Child States

#### `state_identify_appliance`

Determines the appliance's host address.

**On entry:**
- Sets `polling_list_complete = false`.
- If `erd_host_address != tiny_gea_broadcast_address` (pre-known address):
  - If this is a re-entry (`polling_list_count > 0` and `api_parsed_list != NULL`): clears `erd_set`, `erd_cache`, and `polling_list_count` via `clear_discovery_state()`.
  - If `api_parsed_list != NULL`: transitions to `state_probe_api_parsed_erds` (first entry) or `state_polling` (re-entry).
  - Otherwise: transitions to `state_add_common_erds`.
- If `erd_host_address == tiny_gea_broadcast_address`: sends a broadcast read for ERD 0x0008 (appliance type).

**On `signal_read_completed`:**
- Ignores responses for ERDs other than 0x0008 (spurious reads from concurrent bus activity).
- Extracts `erd_host_address` from the responding device's address and `appliance_type` from the data.
- If `api_parsed_list != NULL`: transitions to `state_add_appliance_api_feature_erds`.
- Otherwise: transitions to `state_add_common_erds`.

**On `signal_read_failed`:**
- If the failed ERD is 0x0008: retries the broadcast read indefinitely.

#### `state_add_common_erds`

Probes the common ERD list (`commonErds` from `erd_lists.h`).

**On entry:**
- Calls `clear_discovery_state()` to reset `erd_set`, `erd_cache`, and `polling_list_count`.
- Sets `appliance_erd_list` to `commonErds`, `erd_index` to 0, and `next_discovery_state` to `state_add_energy_erds`.
- Sends the first read.

**On read signals:** delegates to `handle_discovery_list_signals` (§4.1).

#### `state_add_energy_erds`

Probes the energy ERD list (`energyErds` from `erd_lists.h`).

**On entry:** sets `appliance_erd_list` to `energyErds`, `erd_index` to 0, `next_discovery_state` to `state_add_appliance_api_feature_erds`, and sends the first read.

**On read signals:** delegates to `handle_discovery_list_signals` (§4.1).

#### `state_add_appliance_api_feature_erds`

Probes the appliance API feature ERD list (`applianceApiFeatureErds` from `erd_lists.h`).

**On entry:**
- Sets `next_discovery_state` to `state_probe_api_parsed_erds` if `api_parsed_list != NULL`, otherwise `state_add_appliance_erds`.
- Sets `appliance_erd_list` to `applianceApiFeatureErds`, `erd_index` to 0, and sends the first read.

**On read signals:** delegates to `handle_discovery_list_signals` (§4.1).

#### `state_add_appliance_erds`

Probes the appliance-type-specific ERD list.

**On entry:**
- If `appliance_type >= maximumApplianceType`: logs a warning, sets `appliance_erd_list` to NULL and `appliance_erd_list_count` to 0 (skips appliance-specific discovery).
- Otherwise: looks up the ERD list from `applianceTypeToErdGroupTranslation[appliance_type]`.
- Sets `next_discovery_state` to `state_add_custom_erds` if custom ERDs are configured, otherwise `state_polling`.
- If `appliance_erd_list_count > 0`: sends the first read. Otherwise: transitions to `next_discovery_state`.

**On read signals:** delegates to `handle_discovery_list_signals` (§4.1).

#### `state_probe_api_parsed_erds`

Verifies each ERD in `api_parsed_list` with a read before adding it to the polling list.

**On entry:**
- Sets `next_discovery_state` to `state_add_custom_erds` if custom ERDs are configured, otherwise `state_polling`.
- Sets `appliance_erd_list` to `api_parsed_list`, `erd_index` to 0.
- If `api_parsed_list_count > 0`: sends the first read. Otherwise: transitions to `next_discovery_state`.

**On `signal_read_failed`:** inserts the ERD into `erd_set` (as an exclusion), then advances to the next ERD or transitions to `next_discovery_state`.

**On `signal_read_completed`:** delegates to `handle_discovery_list_signals` (§4.1).

#### `state_add_custom_erds`

Probes user-configured custom ERDs.

**On entry:**
- Rebuilds `erd_set` from the actual `erd_polling_list` (so custom ERDs are evaluated independently of failures in earlier discovery phases).
- Sets `appliance_erd_list` to `custom_erd_list`, `erd_index` to 0, `next_discovery_state` to `state_polling`.
- If `custom_erd_list_count > 0`: sends the first read. Otherwise: transitions to `state_polling`.

**On read signals:** delegates to `handle_discovery_list_signals` (§4.1).

#### `state_polling`

Steady-state polling. See §5.

### 3.3 Discovery Path Diagrams

**Full discovery (no `api_parsed_list`):**
```
state_identify_appliance
  → state_add_common_erds
    → state_add_energy_erds
      → state_add_appliance_api_feature_erds
        → state_add_appliance_erds
          → state_add_custom_erds (if custom ERDs configured)
          → state_polling
```

**API-parsed list (broadcast discovery):**
```
state_identify_appliance
  → state_add_appliance_api_feature_erds
    → state_probe_api_parsed_erds
      → state_add_custom_erds (if custom ERDs configured)
      → state_polling
```

**API-parsed list (pre-known address, first entry):**
```
state_identify_appliance
  → state_probe_api_parsed_erds
    → state_add_custom_erds (if custom ERDs configured)
    → state_polling
```

**API-parsed list (pre-known address, re-entry after appliance lost):**
```
state_identify_appliance
  → state_polling
```

---

## 4. Discovery Behavior

### 4.1 Shared Discovery Handler (`handle_discovery_list_signals`)

All discovery states (except `state_probe_api_parsed_erds` for failures) delegate to this handler for `signal_read_completed` and `signal_read_failed`.

**On `signal_read_completed`:**
Calls `add_erd_to_polling_list()` — adds the ERD to `erd_polling_list` (deduped via `erd_set`).
Calls `erd_cache_update()` with `force_publish = true` (discovery phase always publishes).
Advances to the next ERD in the current list or transitions to `next_discovery_state`.

**On `signal_read_failed`:**
- Does NOT add the ERD to the polling list.
- Does NOT insert into `erd_set` (failed ERDs are not excluded — they may be re-probed in a later discovery phase, e.g., as a custom ERD).
- Advances to the next ERD in the current list or transitions to `next_discovery_state`.

### 4.2 `state_probe_api_parsed_erds` Failure Handling

Unlike the shared handler, `state_probe_api_parsed_erds` inserts failed ERDs into `erd_set` as exclusions. This permanently excludes them from the polling list — the API-parsed list is authoritative, and a failed probe means the appliance does not support that ERD.

### 4.3 ERD Set Semantics

`erd_set` is a `std::set<tiny_erd_t>` used for deduplication:
- `add_erd_to_polling_list()` checks `erd_set` before adding — skips if already present.
- `clear_discovery_state()` clears `erd_set` at the start of a new discovery phase.
- `state_add_custom_erds` rebuilds `erd_set` from `erd_polling_list` so custom ERDs are evaluated independently of earlier failures.

### 4.4 One ERD at a Time

During discovery, only one ERD read is outstanding at any time. The next read is issued only after receiving a definitive response (`signal_read_completed` or `signal_read_failed`). No timers are used to advance the discovery index.

---

## 5. Steady-State Polling

### 5.1 Entry

On entering `state_polling`:
- If `api_parsed_list != NULL`: iterates all entries through `add_erd_to_polling_list()` (deduped via `erd_set`).
- Resets `erd_index = 0`, `cycle_completed_count = 0`, `restart_pending = false`.
- Arms the polling timer for `polling_interval_ms`.
- Sets `polling_list_complete = true`.
- Calls `on_discovery_complete` callback if set.

### 5.2 Cycle Semantics

A polling cycle consists of sending reads for all ERDs in `erd_polling_list` and waiting for all responses.

**Cycle start:**
- Triggered by `signal_polling_timer_expired`.
- All reads are sent via `send_cycle_reads()`, which uses `send_poll_read_requests_bounded()` to stay within `POLL_CYCLE_SEND_BUDGET_MS` (500 ms). If the budget is exceeded, `cycle_sending_in_progress` is set and the function returns `false`; a resume timer (`POLL_CYCLE_RESUME_MS` = 100 ms) is armed to continue sending.
- `cycle_start_ms` is set to `millis()`.

**Cycle completion:**
- `cycle_completed_count` increments on each `signal_read_completed` and `signal_read_failed`.
- When `cycle_completed_count >= polling_list_count`, the cycle is complete.
- `on_polling_cycle_complete()` is called with `immediate = restart_pending || !polling_timer_armed`:
  - If `immediate`: starts the next cycle immediately, arms the polling timer.
  - If not `immediate` and `polling_timer_armed` is false: arms the polling timer (next cycle starts on timer expiry).
  - If `polling_timer_armed` is still true: waits for the timer to fire.

**Timer mid-cycle:**
- If the polling timer fires while reads are still in-flight (`erd_index >= polling_list_count` but `cycle_completed_count < polling_list_count`): sets `restart_pending = true` and lets the cycle finish. The cycle-completion handler then starts the next cycle immediately.

**Timer resume:**
- If `cycle_sending_in_progress` is true when the timer fires: resumes sending reads via `send_cycle_reads()`. If all reads are sent, arms the polling timer; otherwise, arms the resume timer.

### 5.3 Read Completion

**On `signal_read_completed`:**
- Resets the appliance-lost timer.
- If the ERD is not in `erd_set`: adds it to the polling list via `add_erd_to_polling_list()` (handles late discovery responses that arrive during polling).
- Updates the ERD cache via `erd_cache_update()` with `force_publish = false` (respects the cache's `only_publish_onchange` setting).
- Increments `cycle_completed_count`; if cycle is complete, calls `on_polling_cycle_complete()`.

**On `signal_read_failed`:**
- Resets the appliance-lost timer.
- Logs the failed ERD at debug level.
- Increments `cycle_completed_count`; if cycle is complete, calls `on_polling_cycle_complete()`.
- **Does not remove the ERD from the polling list.** Failed ERDs remain in the list and are retried each cycle.

---

## 6. Data Structures

### 6.1 Polling List

- `erd_polling_list`: heap-allocated array of `tiny_erd_t`, dynamically grown in increments of `POLLING_LIST_GROWTH_INCREMENT` (32), capped at `POLLING_LIST_MAX_SIZE` (from `erd_lists.h`).
- `polling_list_count`: number of valid entries.
- `polling_list_capacity`: allocated capacity.

### 6.2 ERD Set

- `erd_set`: `std::set<tiny_erd_t>` stored as `void*` in the struct. Used for deduplication during discovery and polling list management.

### 6.3 Cache Publish Behavior

The polling bridge does not own the publish-on-change setting — it is controlled by the shared ERD cache. The caller sets `erd_cache_set_only_publish_onchange()` after init to configure whether polled ERD reads should only publish on data change (`true`) or always publish (`false`, the default).

### 6.4 Timers

- `polling_timer`: armed for `polling_interval_ms` after each cycle starts. Fires `signal_polling_timer_expired`.
- `appliance_lost_timer`: armed for `appliance_lost_timeout` (60 s) on each successful read. Fires `signal_appliance_lost`.

### 6.5 Health Metrics

- `cycle_start_ms`: `millis()` when the current cycle's first read was sent.
- `last_cycle_time_ms`: duration of the last completed cycle in milliseconds.
- `cycle_count`: total number of completed cycles since init.

---

## 7. Timing Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `POLL_YIELD_MS` | 50 ms | Per-batch time budget in `send_poll_read_requests_bounded()`. |
| `POLL_CYCLE_SEND_BUDGET_MS` | 500 ms | Maximum time for `send_cycle_reads()` before yielding. |
| `POLL_CYCLE_RESUME_MS` | 100 ms | Timer interval when send budget is exceeded. |
| `appliance_lost_timeout` | 60000 ms | Time without successful reads before triggering appliance loss. |
| `POLLING_LIST_GROWTH_INCREMENT` | 32 | Number of ERDs to allocate per growth step. |
| `POLLING_LIST_MAX_SIZE` | (from `erd_lists.h`) | Hard cap on polling list capacity. |

---

## 8. Invariants

1. **No overlapping cycles:** A new cycle does not start until the previous one has completed (all ERDs have responded) and the timer has expired (or `restart_pending` is set).
2. **One read at a time during discovery:** The next discovery read is issued only after the previous one has a definitive response.
3. **No polling list growth on MQTT reconnect:** `erd_set` is not cleared on MQTT disconnect, preventing duplicate ERD additions on re-entry to `state_polling`.
4. **Failed discovery ERDs are not excluded from later phases:** In `handle_discovery_list_signals`, failed ERDs are not inserted into `erd_set`, allowing them to be independently re-probed in later discovery phases (e.g., as custom ERDs).
5. **Failed probe ERDs are excluded:** In `state_probe_api_parsed_erds`, failed ERDs are inserted into `erd_set` as exclusions, permanently preventing them from being added to the polling list.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| `i_tiny_gea3_erd_client` | GEA3 ERD client interface (read, write, activity events) |
| `tiny_hsm` | Hierarchical state machine |
| `tiny_timer` | Timer group and timer instances |
| `erd_bridge_common.h` | Shared signals, timing constants, utility templates (`erd_set`, `arm_timer`) |
| `erd_lists.h` | Static ERD lists (`commonErds`, `energyErds`, `applianceApiFeatureErds`, `applianceTypeToErdGroupTranslation`, `maximumApplianceType`, `POLLING_LIST_MAX_SIZE`) |
| `erd_cache.h` | ERD cache for change detection (`erd_cache_init`, `erd_cache_update`, `erd_cache_get_next_entry`) |

---

## 10. Known Limitations

1. **Single appliance assumption:** Broadcast identification reads the appliance type from the first response to ERD 0x0008. In multi-appliance environments, this may not be the intended target.
2. **Failed ERDs never evicted:** An ERD that permanently fails during steady-state polling (e.g., removed by a firmware update) remains in the polling list indefinitely, consuming bus bandwidth and inflating cycle time.
3. **Invalid appliance type skips discovery:** When `appliance_type >= maximumApplianceType`, appliance-specific ERD discovery is skipped entirely rather than falling back to a default set.
