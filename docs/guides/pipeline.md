# Pipeline Guide

End-to-end guide for the Home Assistant discovery pipeline.

## Overview

The pipeline converts GE Appliances ERD definitions into Home Assistant MQTT discovery entities, then compresses them into a C header embedded in the firmware.

```
appliance_api_erd_definitions.json (submodule)
    ↓
generate_flattened_review.py  →  appliance_api_erd_definitions_processed.json
    ↓
auto_detect_ha_domain.py      →  assign ha_domain
auto_detect_device_class.py   →  assign device_class
auto_detect_state_class.py    →  assign state_class
auto_detect_scaling.py        →  infer scaling_factor
auto_detect_pairings.py       →  detect Request/Status pairs
post_process.py               →  apply overrides, fix consistency
    ↓
generate_ha_discovery.py      →  ha_discovery/*.jsonl
    ↓
compress_ha_discovery.py      →  components/geappliances_bridge/ha_discovery_data.h
```

## Running the Pipeline

```bash
python3 scripts/ha_discovery/run_pipeline.py
```

**Always run this before committing changes to ERD definitions, overrides, or pipeline scripts.** It regenerates all derived artifacts.

## Adding Overrides

If an ERD needs manual correction (wrong domain, missing unit, incorrect scaling), add an override to `scripts/ha_discovery/pipeline/post_process.py`:

```python
OVERRIDES = {
    # Bare erd_id — applies to all fields in the ERD.
    "0x7130": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},

    # erd_id:offset — applies only to the field at the given byte offset.
    "0x3015:0": {"unit_of_measurement": "gal/min", "scaling_factor": 10000},
}
```

### Valid Override Keys

| Key | Description | Example |
|---|---|---|
| `ha_domain` | Home Assistant domain | `"sensor"`, `"number"`, `"switch"` |
| `device_class` | Device class | `"temperature"`, `"current"`, `"weight"` |
| `unit_of_measurement` | Unit string | `"rpm"`, `"gal/min"`, `"CFM"` |
| `scaling_factor` | Scaling factor | `100`, `10000`, or `None` to remove |
| `state_class` | State class | `"measurement"`, `"total"`, `"total_increasing"` |
| `field_name` | Display name | `"Inlet Flow Rate"` |
| `paired_erd` | Manual request/status pair | `"0x7708"` |
| `pair_role` | Role in a pair | `"request"` or `"status"` |
| `force_classification` | Force classification | `"single"` |
| `value_template` | Custom Jinja2 template | `"..."` |

Overrides are reapplied after auto-detection, so they survive subsequent pipeline runs.

## Pipeline Stages

### 1. Flatten Review

`generate_flattened_review.py` takes the raw ERD definitions and creates one entry per sub-field. Each ERD can contain multiple fields at different byte offsets.

### 2. Auto-Detection

Five scripts run sequentially to infer Home Assistant metadata:

- **`auto_detect_ha_domain.py`** — Assigns `sensor`, `number`, `switch`, `binary_sensor`, or `button` based on field type and name keywords.
- **`auto_detect_device_class.py`** — Assigns device class (`temperature`, `power`, `energy`, etc.) based on field name patterns.
- **`auto_detect_state_class.py`** — Assigns `measurement`, `total`, or `total_increasing` for sensor entities.
- **`auto_detect_scaling.py`** — Infers `scaling_factor` from field name patterns (e.g., "percentage" → divide by 100).
- **`auto_detect_pairings.py`** — Detects Request/Status ERD pairs (e.g., a setpoint request and its corresponding status).

### 3. Post-Process

`post_process.py` applies manual overrides and fixes cross-field consistency issues (e.g., ensuring paired ERDs have matching domains).

### 4. Generate Discovery

`generate_ha_discovery.py` produces one JSONL file per appliance category (`common.jsonl`, `dishwasher.jsonl`, `laundry.jsonl`, etc.). Each line is a complete Home Assistant discovery payload.

### 5. Compress

`compress_ha_discovery.py` zlib-compresses each JSONL file and embeds the compressed bytes in a C header (`ha_discovery_data.h`). The firmware decompresses these at runtime.

## Debugging Pipeline Issues

### Check Intermediate Output

The processed definitions are saved at `scripts/ha_discovery/appliance_api_erd_definitions_processed.json`. Inspect this file to verify auto-detection results before they're compiled into discovery payloads.

### Run Individual Stages

Each pipeline script can be run independently:

```bash
python3 scripts/ha_discovery/pipeline/auto_detect_ha_domain.py
python3 scripts/ha_discovery/pipeline/auto_detect_device_class.py
```

### Verify Compression Roundtrip

```bash
make pytest
```

The `test_compress_ha_discovery.py` test verifies that compressed data decompresses to the original JSONL.

### Check Output Size

The compressed header size impacts firmware flash usage. Check the size after a pipeline run:

```bash
ls -la components/geappliances_bridge/ha_discovery_data.h
```

## When to Run the Pipeline

Run the pipeline whenever you:

1. Add or modify an override in `post_process.py`.
2. Change auto-detection logic in any `auto_detect_*.py` script.
3. Update the upstream `public-appliance-api-documentation` submodule.
4. Change the discovery payload format in `generate_ha_discovery.py`.