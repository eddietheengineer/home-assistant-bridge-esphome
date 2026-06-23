# Feature Bit Manager Specification

This document defines the requirements for the `FeatureBitManager`, which reads and parses appliance API feature bit ERDs (0x0092–0x010D) and produces the set of ERDs that the appliance supports.

## 1. ERD Read Sequence

### Requirement 1.1: Sequential Reads

The manager MUST read the 11 feature bit ERDs in a fixed sequence, one at a time:

| Order | ERD | Name |
|-------|-----|------|
| 1 | 0x0092 | Common Feature API |
| 2 | 0x0093 | Appliance Feature API 0 |
| 3 | 0x0094 | Appliance Feature API 1 |
| 4 | 0x0095 | Appliance Feature API 2 |
| 5 | 0x0096 | Appliance Feature API 3 |
| 6 | 0x0097 | Appliance Feature API 4 |
| 7 | 0x0109 | Appliance Feature API 5 |
| 8 | 0x010A | Appliance Feature API 6 |
| 9 | 0x010B | Appliance Feature API 7 |
| 10 | 0x010C | Appliance Feature API 8 |
| 11 | 0x010D | Appliance Feature API 9 |

Only ONE ERD read shall be outstanding at any time. The next ERD read is issued only after the previous read has received a definitive response (success or failure).

### Requirement 1.2: Self-Driving Read Sequence

The manager MUST drive the read sequence autonomously via event subscription to the ERD client activity event (`tiny_gea3_erd_client_on_activity`). The bridge MUST NOT poll `run()` or any equivalent method to advance the sequence.

The sequence is initiated by calling `start()`, which queues the first ERD read (0x0092). Each subsequent read is queued by the manager itself in response to the previous read's completion or failure event.

### Requirement 1.3: Idempotent Start

Calling `start()` multiple times MUST NOT queue multiple reads. The manager MUST use a `read_queued_` guard: if the manager is in its initial state (`FEATURE_BIT_STATE_READING_0092`) and a read has already been queued, subsequent calls to `start()` are no-ops.

### Requirement 1.4: Successful Read

When an ERD read completes successfully:
- The ERD data MUST be stored (capped at 8 bytes).
- The state MUST advance to the next ERD in the sequence.
- The next ERD read MUST be queued immediately.

### Requirement 1.5: Failed Read

When an ERD read fails (not supported, timeout, or null data):
- The ERD data is NOT stored (the buffer remains zeroed).
- The state MUST advance to the next ERD in the sequence.
- The next ERD read MUST be queued immediately.
- The failed ERD is NOT retried.

### Requirement 1.6: Queue Full Handling

If `tiny_gea3_erd_client_read()` returns false (queue full):
- The manager MUST remain in the current state (not advance).
- The manager MUST arm a one-shot retry timer (50 ms).
- If an ERD activity event arrives before the timer fires, the manager retries the queue immediately.
- When the timer fires, the manager retries the queue.
- If the retry also fails (queue still full), the timer is re-armed.

### Requirement 1.7: Event Filtering

The `on_erd_activity_()` handler MUST filter events by:
1. **Address**: Only process events where `address == host_address_`.
2. **State**: Ignore all events when in `FEATURE_BIT_STATE_PARSING`, `FEATURE_BIT_STATE_COMPLETE`, or `FEATURE_BIT_STATE_FAILED`.
3. **Expected ERD**: Only process `read_completed` or `read_failed` events for the ERD that the current READING state is waiting for (as returned by `get_expected_erd_()`).

This prevents unrelated reads (e.g., from the polling bridge) from corrupting the read sequence.

### Requirement 1.8: Common Feature API Failure

If ERD 0x0092 (Common Feature API) fails or returns null data, the manager MUST transition to `FEATURE_BIT_STATE_FAILED` instead of continuing the read sequence. The Common Feature API is the foundation for all feature filtering — without it, there is no way to determine which ERDs are supported. When the manager enters `FEATURE_BIT_STATE_FAILED`, the bridge falls back to full polling (no feature filtering).

### Implementation

The state machine has one state per ERD in the sequence (`FEATURE_BIT_STATE_READING_0092` through `FEATURE_BIT_STATE_READING_010D`), plus `FEATURE_BIT_STATE_PARSING`, `FEATURE_BIT_STATE_COMPLETE`, and `FEATURE_BIT_STATE_FAILED`. The `queue_erd_read_()` and `get_expected_erd_()` methods map each state to its corresponding ERD. The `skip_to_next_erd_()` method advances state on failure, or transitions to `FEATURE_BIT_STATE_FAILED` if the failed ERD is 0x0092.

### Prohibited

- Reading multiple ERDs concurrently.
- Advancing the read sequence via timers (only event-driven).
- Retrying failed ERD reads.
- Processing events for ERDs other than the one the current state expects.
- Processing events in PARSING, COMPLETE, or FAILED state.

### Verification

Tests must verify that the read sequence progresses only through event callbacks, that failed reads advance to the next ERD without retry, that unrelated events are ignored, and that ERD 0x0092 failure transitions to FAILED state.

---

## 2. Incremental Parsing

### Requirement 2.1: Timer-Driven Parsing

After all 11 ERD reads are complete (regardless of individual success or failure), the manager MUST transition to `FEATURE_BIT_STATE_PARSING` and start a periodic timer (5 ms interval) for incremental parsing. This spreads heap allocations across multiple timer ticks to avoid triggering the ESP32 Task Watchdog Timer.

### Requirement 2.2: Parsing Phases

Parsing proceeds in three phases, each on separate timer ticks:

1. **Initialization** (first tick): Clear `valid_erds_` and `valid_erds_vec_`.
2. **Common features** (ERD 0x0092): Process 17 common feature descriptors, 4 per tick (~5 ticks). Each descriptor has a bit mask and a list of ERDs; if the bit is set in the common feature bitmap, all ERDs in the descriptor are added to `valid_erds_`.
3. **Appliance features** (ERDs 0x0093–0x010D): Process one appliance ERD per tick (10 ticks). Each ERD contains a type, version, and feature bitmap. The bitmap is matched against the `appliance_feature_api_descriptors` table by type and version; matching descriptors contribute their ERDs to `valid_erds_`.
4. **Finalization** (last tick): Add the mandatory ERDs (see Requirement 2.4), build `valid_erds_vec_` from `valid_erds_`, set `valid_list_ready_` to true, stop the parse timer, and transition to `FEATURE_BIT_STATE_COMPLETE`.

### Requirement 2.3: Parsing with Incomplete Data

If an ERD read failed or returned less than the expected size:
- **Common features (0x0092)**: If `erd_0092_size == 0`, the common feature parsing phase is skipped entirely (no ERDs added from common features).
- **Appliance features**: If the ERD data is less than 8 bytes, that ERD is skipped (no ERDs added from it). If the type and version fields are all zeros and the feature bitmap is zero, the ERD is also skipped.

### Requirement 2.4: Mandatory ERDs

The following ERDs MUST always be included in the valid ERD set, regardless of feature bit values:

| ERD | Name |
|-----|------|
| 0x0001 | Model Number |
| 0x0002 | Serial Number |
| 0x0008 | Appliance Type |
| 0x0092 | Common Feature API |
| 0x0093 | Appliance Feature API 0 |
| 0x0094 | Appliance Feature API 1 |
| 0x0095 | Appliance Feature API 2 |
| 0x0096 | Appliance Feature API 3 |
| 0x0097 | Appliance Feature API 4 |
| 0x0109 | Appliance Feature API 5 |
| 0x010A | Appliance Feature API 6 |
| 0x010B | Appliance Feature API 7 |
| 0x010C | Appliance Feature API 8 |
| 0x010D | Appliance Feature API 9 |

These 14 ERDs are added during the finalization tick, after all feature bit parsing is complete.

### Requirement 2.5: Total Parsing Duration

The total parsing time is approximately 80 ms (16 ticks × 5 ms):
- 1 tick for initialization
- ~5 ticks for common features (17 descriptors / 4 per tick, rounded up)
- 10 ticks for appliance features (1 per tick)
- 1 tick for finalization

### Implementation

The `parse_next_step_()` method uses `parse_erd_idx_` (0–9) and `common_parse_idx_` (0–17) to track progress across ticks. The `parse_common_done_` flag gates the transition from common to appliance parsing. The `appliance_feature_api_descriptors` table is generated at build time by `scripts/generate_erd_lists.py`.

### Prohibited

- Parsing all feature bits in a single function call (must be spread across timer ticks).
- Adding non-mandatory ERDs to the valid set without a matching feature bit.
- Leaving the parse timer running after transitioning to COMPLETE.

### Verification

Tests must verify that parsing completes within the expected number of timer ticks, that mandatory ERDs are always present in the final set, and that failed or incomplete ERD reads do not cause parsing to stall or crash.

---

## 3. Public API and State Reporting

### Requirement 3.1: State Reporting

The `get_state()` method MUST return the current `FeatureBitState`. The bridge startup HSM polls this to determine when to transition past the feature_bits phase. `is_feature_bits_complete()` returns true for both `FEATURE_BIT_STATE_COMPLETE` and `FEATURE_BIT_STATE_FAILED` — the former means feature filtering succeeded, the latter means the bridge falls back to full polling.

### Requirement 3.2: Valid ERD Set

The `get_valid_erds()` method returns a `const std::set<tiny_erd_t>&` of all valid ERDs. Before parsing completes, the set is empty. After parsing completes, it contains the mandatory ERDs plus any ERDs indicated by the feature bits. When in `FEATURE_BIT_STATE_FAILED`, the valid ERD set is empty (no feature filtering is applied).

### Requirement 3.3: Valid ERD Vector

The `get_valid_erds_vec()` method returns a `const std::vector<tiny_erd_t>&` that is a sorted copy of `valid_erds_`. This is provided for C API compatibility where `std::set` is not accessible.

### Requirement 3.4: List Readiness

The `valid_list_ready_` flag MUST be false until parsing completes and the mandatory ERDs have been added. Callers MUST NOT read `valid_erds_` or `valid_erds_vec_` until `valid_list_ready_` is true. When in `FEATURE_BIT_STATE_FAILED`, `valid_list_ready_` is false.

### Prohibited

- Returning a non-empty `valid_erds_` before parsing completes.
- Modifying `valid_erds_` or `valid_erds_vec_` after transitioning to COMPLETE.

---

## 4. Lifecycle

### Requirement 4.1: Initialization

`init()` MUST:
- Unsubscribe from any previous ERD client activity event (safe for re-init).
- Stop any active timers (parse timer and queue retry timer).
- Reset all state to initial values.
- Subscribe to the new ERD client's activity event.

### Requirement 4.2: Cleanup

`cleanup()` MUST:
- Unsubscribe from the ERD client activity event.
- Stop all timers.
- Reset the ERD client pointer and host address to null/zero.

### Requirement 4.3: No Timeout

The feature bits phase has no timeout. If the appliance is lost during the read sequence, the manager remains in the current READING state indefinitely. The startup HSM handles appliance loss via its own timeout mechanism.

### Requirement 4.4: Failed State Exit

When the manager enters `FEATURE_BIT_STATE_FAILED` (due to ERD 0x0092 failure):
- The manager MUST NOT continue the read sequence.
- The manager MUST ignore subsequent ERD activity events (same as PARSING/COMPLETE).
- The retry timer callback MUST be a no-op when in FAILED state.
- The bridge treats FAILED as a terminal state equivalent to COMPLETE for HSM transition purposes.

### Prohibited

- Using timers to advance the read sequence (only event-driven).
- Timing out individual ERD reads (the ERD client handles retries and timeouts).

---

## 5. Dependencies

| Dependency | Purpose |
|------------|---------|
| `i_tiny_gea3_erd_client` | ERD read interface |
| `tiny_event` / `tiny_event_subscription` | Event subscription for ERD client activity |
| `tiny_timer` | Periodic parse timer and one-shot retry timer |
| `appliance_api_feature_lists.h` | Generated descriptor tables for feature bit parsing |
| `geappliances_bridge_constants.h` | ERD constant definitions |