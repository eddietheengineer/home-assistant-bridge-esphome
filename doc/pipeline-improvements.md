# Pipeline Improvements

Architectural recommendations from PR reviews of the `july4th` branch.
Prioritized by impact on correctness and maintainability.

## 1. Consistent Stale-Clearing Across Auto-Detection Scripts (Medium)

**Problem:** Only `auto_detect_device_class` and `auto_detect_scaling` clear stale values when their detector returns `None`. The other scripts preserve stale values indefinitely:

- `auto_detect_ha_domain`: `if review.get('ha_domain'): continue` — stale values persist if keyword logic changes.
- `auto_detect_state_class`: `if review.get('state_class'): continue` — same issue.
- `auto_detect_pairings`: Does a blanket clear at the start of `apply_pairings()` — the most robust approach.

**Recommendation:** Adopt the same stale-clearing pattern in `auto_detect_ha_domain` and `auto_detect_state_class`:

```python
# In apply_detection, when infer_* returns None:
if dc is None:
    if review.get('device_class') is not None:
        review['device_class'] = None
    continue
# Always write when detected, overwriting stale values.
```

Alternatively, adopt the `auto_detect_pairings` approach: clear all review fields at the start of `apply_*` for a nuclear reset. This is simpler and makes the pipeline truly idempotent.

**Affected files:** `scripts/ha_discovery/pipeline/auto_detect_ha_domain.py`, `scripts/ha_discovery/pipeline/auto_detect_state_class.py`

## 2. Pipeline Idempotency Contract (Medium)

**Problem:** The pipeline relies on a specific order: auto-detection scripts may clear stale values, then `post_process` re-applies overrides. This works but is undocumented.

**Recommendation:** Add a comment in `run_pipeline.py` documenting the contract:

```
# Pipeline contract:
# - Auto-detection scripts (steps 1-5) may clear stale review values when
#   the detector no longer finds a match. This ensures changes to detection
#   logic take effect on the next run.
# - Post-process (step 6) re-applies all overrides after detection, so
#   intentional override values survive stale clearing.
# - The pipeline is idempotent: running it twice produces identical output.
```

**Affected file:** `scripts/ha_discovery/run_pipeline.py`

## 3. Replace 'allowed' Filter with Allowlist (Minor)

**Problem:** The current filter in `generate_ha_discovery.py` uses a blacklist-with-exceptions pattern:

```python
if ('allowed' in combined and 'setpoint' not in combined and
    'minimum allowed' not in combined and 'maximum allowed' not in combined) or
    'available' in combined:
    return
```

Each new legitimate "allowed" pattern requires another `and '...' not in combined` clause. This is O(n) in exception count and unreadable.

**Recommendation:** Invert the logic — only skip fields matching known metadata patterns:

```python
# Skip known allowability/capability metadata patterns.
# Real data fields (setpoints, ranges, limits) are not skipped.
metadata_patterns = [
    'allowables',           # EcoDryOptionAllowables
    'allowed selections',   # Allowed Selections.Cyclic Supported
    'available',            # Bake Cycle Availability
]
if any(p in combined for p in metadata_patterns):
    return
```

**Affected file:** `scripts/ha_discovery/generators/generate_ha_discovery.py`

## 4. Document Override Keys in Docstring (Minor)

**Problem:** The `apply_overrides` docstring describes the key format (`erd_id` or `erd_id:offset`) but doesn't list valid override value keys. New contributors add keys by copying existing entries, which works but is fragile.

**Recommendation:** Add a section to the docstring listing valid override keys:

```
Valid override value keys:
    - ha_domain: Override the Home Assistant domain (sensor, switch, select, number, binary_sensor).
    - device_class: Override the device class (temperature, problem, occupancy, enum, etc.).
    - unit_of_measurement: Override the unit of measurement.
    - scaling_factor: Override the scaling factor (int or None to clear).
    - state_class: Override the state class (measurement, total, total_increasing).
    - paired_erd: Set the paired ERD ID for request/status relationships.
    - pair_role: Set the pair role ('request' or 'status').
    - field_name: Override the field name for display purposes.
    - force_classification: Force a classification ('single' to combine multi-field ERDs).
    - value_template: Custom Jinja2 value template.
```

**Affected file:** `scripts/ha_discovery/pipeline/post_process.py`

## 5. Add 'failure' to Problem Keywords (Low)

**Problem:** 17 `Failure_*` binary_sensor fields in `0x4058` (Heating Issues) are not detected as `device_class=problem` because `failure` is not in the keyword list `['fault', 'issue', 'error', 'alarm', 'limited']`.

**Recommendation:** Add `'failure'` to the problem keyword list in `auto_detect_device_class.py`:

```python
if any(_word_bound(combined, kw) for kw in ['fault', 'failure', 'issue', 'error', 'alarm', 'limited']):
    return 'problem', 0.8
```

**Affected file:** `scripts/ha_discovery/pipeline/auto_detect_device_class.py`

## 6. Handle 'Availability' in Filter (Low)

**Problem:** 52 `Availability` ERD entities across 11 ERDs are capability metadata, not actionable sensors. The `'available'` substring filter doesn't match `Availability` (different casing after `.lower()` — actually `'availability'` contains `'available'` as a prefix, so it should match). Verify and fix if needed.

**Note:** `'availability'.lower()` contains `'available'` — the filter should already catch this. Re-verify if these entities are still appearing in the output.

**Affected file:** `scripts/ha_discovery/generators/generate_ha_discovery.py`

## 7. Add state_class Guard to Mixed Handler (Info)

**Problem:** The `mixed` classification handler's primary field uses `primary.get('state_class') or state_class` without a numeric-type guard. If the primary field is an enum and the ERD-level `state_class` is set (from a non-primary field), the enum inherits it.

**Current risk:** Low — for all 104 mixed ERDs, the first field is the primary field, so this path isn't triggered.

**Recommendation:** Add the same guard as the `byte_offset` handler:

```python
p_state_cls = primary.get('state_class') or (state_class if p_type in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32') else '')
```

**Affected file:** `scripts/ha_discovery/generators/generate_ha_discovery.py`

## 8. Add Unit Tests for New Code Paths (Low)

**Missing tests:**
- `field_name` override application in `_build_erds_from_flat_list`
- `_word_bound` edge cases (camelCase, hyphens, underscores)
- Stale `device_class` clearing in `apply_detection`
- Per-field pairing overrides (`paired_erd`, `pair_role` in OVERRIDES)
- Bitfield device_class wiring in both `bitfield` and `mixed` handlers

**Affected file:** `scripts/test_pipeline.py`