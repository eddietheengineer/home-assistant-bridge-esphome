# ERD Bridge Poll — Specification

## 1. Overview

### 1.1 Purpose

The ERD polling bridge discovers the connected GE appliance, determines which ERDs (Entity-Relationship Data points) it supports, and periodically reads their values — writing them to the shared ERD cache. It has **zero** direct interaction with the MQTT client.

### 1.2 Responsibilities

- Discover the appliance's host address via broadcast (or accept a pre-known address)
- Probe a pre-built list of ERDs to determine which the appliance supports
- Maintain a dynamic polling list of verified ERDs
- Execute steady-state polling cycles at a configured interval
- Write ERD values to the shared ERD cache (change detection via `erd_cache_update`)
- Recover from appliance loss

### 1.3 Not Responsible For

- Subscription-mode operation (see `erd_bridge_subscribe`)
- Any MQTT behavior (publishing, write requests, disconnect handling)
- Building the probe list (see `erd_poll_list_builder`)
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
    const tiny_erd_t* probe_list,
    uint16_t probe_list_count,
    erd_cache_t* cache);
```

| Parameter | Description |
|-----------|-------------|
| `timer_group` | Shared timer group for polling and appliance-lost timers. |
| `erd_client` | GEA3 ERD client interface. |
| `polling_interval_ms` | Interval between polling cycles (default 10000 ms). |
| `host_address` | The appliance's GEA bus address. If `tiny_gea_broadcast_address` (0xFF), the bridge performs broadcast discovery. Otherwise, it skips broadcast and proceeds directly to probing. |
| `appliance_type` | The appliance type byte from ERD 0x0008. Used for logging and health metrics. |
| `probe_list` / `probe_list_count` | A pre-built array of ERDs to verify during the probe phase. Built by `erd_poll_list_builder` based on bridge mode and configuration. Each ERD is read once; successful reads are added to the polling list, failed reads are excluded. |
| `cache` | Shared `erd_cache_t` for storing ERD values. |

The probe list pointer must remain valid for the duration of the probe phase (typically the lifetime of the bridge instance). The caller is responsible for ensuring this — the bridge does not copy the probe list.

### 2.2 Destroy

```c
void erd_bridge_poll_destroy(erd_bridge_poll_t* self);
```

Stops timers, unsubscribes all event handlers, and frees heap-allocated state (`erd_set`, `erd_polling_list`). Guards against being called on a never-initialized struct or a partially-initialized one (e.g., if a `new` allocation failed during init).

### 2.3 Discovery-Complete Callback

After init, the caller may set `self->on_discovery_complete` and `self->on_discovery_complete_context`. This callback fires once when the HSM enters `state_polling` (discovery complete). The callback must not send signals back to the polling HSM. It should iterate the cache via `erd_cache_get_next_entry()` to build the ERD set for HA discovery.

---

## 3. State Machine

The polling bridge uses a hierarchical state machine (`tiny_hsm`) with a parent state (`poll_state_top`) and three child states.

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
  - If this is a re-entry (`polling_list_count > 0`): clears `erd_set` and `polling_list_count` via `clear_discovery_state()` (does NOT clear the ERD cache — it may be shared with the subscription bridge).
  - Transitions to `state_probe_list`.
- If `erd_host_address == tiny_gea_broadcast_address`: sends a broadcast read for ERD 0x0008 (appliance type).

**On `signal_read_completed`:**
- Ignores responses for ERDs other than 0x0008 (spurious reads from concurrent bus activity).
- Extracts `erd_host_address` from the responding device's address and `appliance_type` from the data.
- Transitions to `state_probe_list`.

**On `signal_read_failed`:**
- If the failed ERD is 0x0008: retries the broadcast read indefinitely.

#### `state_probe_list`

Probes each ERD in the pre-built probe list to verify support before adding to the polling list.

**On entry:**
- Sets `appliance_erd_list` to `probe_list` and `appliance_erd_list_count` to `probe_list_count`.
- Sets `erd_index = (uint16_t)-1` (sentinel — `send_next_read_request` increments to 0 on first call).
- If re-entry (`polling_list_count > 0`): clears `erd_set` and `polling_list_count` via `clear_discovery_state()` (does NOT clear the ERD cache).
- If `probe_list_count > 0`: sends the first read via `send_next_read_request`.
- If `probe_list_count == 0`: transitions directly to `state_polling`.

**On `signal_read_completed`:**
- Adds the ERD to the polling list via `add_erd_to_polling_list()` (deduped via `erd_set`).
- Updates the ERD cache via `erd_cache_update()`.
- Advances to the next ERD or transitions to `state_polling`.

**On `signal_read_failed`:**
- Inserts the ERD into `erd_set` as an exclusion (prevents lazy registration in `state_polling`).
- Advances to the next ERD or transitions to `state_polling`.

#### `state_polling`

Steady-state polling. See §5.

### 3.3 State Diagram

```
poll_state_top (parent — handles appliance_lost globally)
  ├─ state_identify_appliance (initial)
  │    ├─ entry: if pre-known address → state_probe_list
  │    │          else → broadcast read for ERD 0x0008
  │    ├─ read_completed (0x0008): extract host address → state_probe_list
  │    └─ read_failed (0x0008): retry broadcast
  │
  ├─ state_probe_list
  │    ├─ entry: if probe_list empty → state_polling
  │    │         else → read first ERD
  │    ├─ read_completed: add to polling list + cache → next ERD or state_polling
  │    └─ read_failed: exclude from polling list → next ERD or state_polling
  │
  └─ state_polling (steady state)
       ├─ entry: arm polling timer, set polling_list_complete, fire callback
       ├─ polling_timer_expired: start cycle (or set restart_pending)
       ├─ read_completed: update cache, count completion, maybe restart cycle
       ├─ read_failed: count completion, maybe restart cycle
       └─ appliance_lost → state_identify_appliance
```

---

## 4. Probe Phase Behavior

### 4.1 Sequential Probing

During the probe phase, only one ERD read is outstanding at any time. The next read is issued only after receiving a definitive response (`signal_read_completed` or `signal_read_failed`). No timers are used to advance the probe index.

### 4.2 Successful Probe

When a probe read completes successfully:
- The ERD is added to `erd_polling_list` (deduped via `erd_set`).
- The ERD data is written to the shared ERD cache via `erd_cache_update()`.
- The next ERD in the probe list is read.

### 4.3 Failed Probe

When a probe read fails (either `not_supported` or `retries_exhausted`):
- The ERD is **not** added to the polling list.
- The ERD is inserted into `erd_set` as an exclusion — this prevents the ERD from being lazily registered in `state_polling` if a late response arrives.
- The next ERD in the probe list is read.

### 4.4 Shared Discovery Handler

The `handle_discovery_list_signals()` function handles `signal_read_completed` and `signal_read_failed` for `state_probe_list`. It adds successful ERDs to the polling list and cache, then advances to the next ERD or transitions to `state_polling`.

**Note:** `state_probe_list` handles `signal_read_failed` explicitly (inserting into `erd_set` for exclusion) before delegating to `handle_discovery_list_signals` for `signal_read_completed`. This ensures failed probe ERDs are excluded from lazy registration in the polling state.

### 4.5 Late Response Handling

If a probe read response arrives while `state_polling` is active (e.g., the appliance responded slower than expected), the polling state handles it via `signal_read_completed`: the ERD is added to the polling list (if not already present) and the cache is updated.

---

## 5. Steady-State Polling

### 5.1 Entry

On entering `state_polling`:
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
- If the ERD is not in `erd_set`: adds it to the polling list via `add_erd_to_polling_list()` (handles late probe responses that arrive during polling).
- Updates the ERD cache via `erd_cache_update()` (respects the cache's `only_publish_onchange` setting).
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

### 6.3 Probe List

- `probe_list`: pointer to the pre-built array of ERDs to probe. Set by the caller before `erd_bridge_poll_init()`. The bridge reads from this array during the probe phase but does not own or modify it.
- `probe_list_count`: number of ERDs in the probe list.

### 6.4 Cache Publish Behavior

The polling bridge does not own the publish-on-change setting — it is controlled by the shared ERD cache. The caller sets `erd_cache_set_only_publish_onchange()` after init to configure whether polled ERD reads should only publish on data change (`true`) or always publish (`false`, the default).

### 6.5 Timers

- `polling_timer`: armed for `polling_interval_ms` after each cycle starts. Fires `signal_polling_timer_expired`.
- `appliance_lost_timer`: armed for `appliance_lost_timeout` (60 s) on each successful read. Fires `signal_appliance_lost`.

### 6.6 Health Metrics

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
2. **One read at a time during probe:** The next probe read is issued only after the previous one has a definitive response.
3. **No polling list growth on re-entry:** `erd_set` and `polling_list_count` are cleared on re-entry to `state_identify_appliance` or `state_probe_list` after appliance loss, preventing duplicate ERD additions.
4. **Failed probe ERDs are excluded:** In `state_probe_list`, failed ERDs are inserted into `erd_set` as exclusions, preventing them from being lazily registered in `state_polling`.
5. **Cache NOT cleared on probe re-entry:** The ERD cache is not reset during `state_probe_list` entry or on appliance-loss re-discovery. The cache may be shared with the subscription bridge; stale entries are overwritten when new data arrives.
6. **Known host address preserved:** When initialized with a non-broadcast `host_address`, the bridge stores it in `known_host_address`. On appliance loss, this address is restored instead of falling back to broadcast.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| `i_tiny_gea3_erd_client` | GEA3 ERD client interface (read, activity events) |
| `tiny_hsm` | Hierarchical state machine |
| `tiny_timer` | Timer group and timer instances |
| `erd_bridge_common.h` | Shared signals, timing constants, utility templates (`erd_set`, `arm_timer`) |
| `erd_lists.h` | Static ERD lists (used by `erd_poll_list_builder`) |
| `erd_cache.h` | ERD cache for change detection (`erd_cache_init`, `erd_cache_update`, `erd_cache_get_next_entry`) |

---

## 10. Known Limitations

1. **Single appliance assumption:** Broadcast identification reads the appliance type from the first response to ERD 0x0008. In multi-appliance environments, this may not be the intended target.
2. **Failed ERDs never evicted:** An ERD that permanently fails during steady-state polling (e.g., removed by a firmware update) remains in the polling list indefinitely, consuming bus bandwidth and inflating cycle time.
3. **Probe list pointer lifetime:** The bridge does not copy the probe list — the caller must ensure the pointer remains valid for the duration of the probe phase (typically the bridge's lifetime).
