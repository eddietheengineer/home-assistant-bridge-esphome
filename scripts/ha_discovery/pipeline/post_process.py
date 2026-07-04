#!/usr/bin/env python3
"""Post-processing: fix cross-field consistency issues after auto-detection.

Applies these rules:
1. Clear unit_of_measurement for binary_sensor/switch (they shouldn't have units).
2. Clear device_class for number domain (except temperature).
3. Add state_class=measurement for sensor with device_class but no state_class.

Run this after all auto-detection scripts for a clean state.
"""

import argparse
import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def apply_overrides(entries):
    """Reapply documented overrides that auto-detection scripts may have reset.

    Each override is a dict keyed by lowercase hex ERD ID, with the review
    fields to force-set.  Applied after all auto-detection so manual fixes
    survive the next pipeline run.
    """
    OVERRIDES = {
        # --- Read-only: force ha_domain=sensor ---
        "0x7100": {"ha_domain": "sensor"},
        "0x7101": {"ha_domain": "sensor"},
        "0x7102": {"ha_domain": "sensor"},
        "0x7103": {"ha_domain": "sensor"},
        "0x7108": {"ha_domain": "sensor"},
        "0x710a": {"ha_domain": "sensor"},
        "0x7130": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
        "0x7131": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
        "0x7132": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
        "0x7133": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
        "0x7601": {"ha_domain": "sensor"},
        "0x7512": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
        "0x7513": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
        "0x7514": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
        "0x7515": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
        "0x7104": {"ha_domain": "sensor"},
        "0x7114": {"ha_domain": "sensor"},
        "0x7115": {"ha_domain": "sensor"},
        "0x4026": {"ha_domain": "sensor"},
        "0x7907": {"ha_domain": "sensor", "unit_of_measurement": "steps"},
        "0x7938": {"ha_domain": "sensor"},
        # --- Fan speed: add rpm unit ---
        "0x7136": {"unit_of_measurement": "rpm"},
        "0x7137": {"unit_of_measurement": "rpm"},
        "0x5b13": {"unit_of_measurement": "rpm"},
        # --- Fan PWM: add % unit and scaling ---
        "0x7134": {"unit_of_measurement": "%", "scaling_factor": 100},
        "0x7135": {"unit_of_measurement": "%", "scaling_factor": 100},
        # --- EEV positions: add steps unit ---
        "0x7518": {"unit_of_measurement": "steps"},
        # --- WAC Ambient: remove incorrect scaling ---
        "0x7a02": {"scaling_factor": None},
        # --- Appliance Cumulative Energy: add scaling ---
        "0xd030": {"scaling_factor": 1000},
    }

    applied = 0
    for entry in entries:
        erd_id = entry.get("erd_id", "").lower()
        if erd_id in OVERRIDES:
            review = entry.get("review", {})
            for key, val in OVERRIDES[erd_id].items():
                if review.get(key) != val:
                    review[key] = val
                    applied += 1

    return applied


def apply_post_processing(entries):
    """Apply post-processing rules to all entries."""
    cleared_unit = 0
    cleared_dc = 0
    added_sc = 0

    for entry in entries:
        review = entry.get('review', {})
        ha_domain = review.get('ha_domain')
        device_class = review.get('device_class')
        state_class = review.get('state_class')

        # Rule 1: binary_sensor/switch should not have units
        if ha_domain in ('binary_sensor', 'switch') and review.get('unit_of_measurement'):
            review['unit_of_measurement'] = None
            cleared_unit += 1

        # Rule 2: number domain should not have device_class (except temperature)
        if ha_domain == 'number' and device_class and device_class != 'temperature':
            review['device_class'] = None
            cleared_dc += 1

        # Rule 3: sensor with device_class should have state_class, but only
        # for numeric device classes. Non-numeric (enum, timestamp, date, uptime)
        # output text strings — state_class on these causes HA LTS errors.
        NON_NUMERIC_DEVICE_CLASSES = {'enum', 'timestamp', 'date', 'uptime'}
        if (ha_domain == 'sensor' and device_class
                and not state_class
                and device_class not in NON_NUMERIC_DEVICE_CLASSES):
            review['state_class'] = 'measurement'
            added_sc += 1

    return cleared_unit, cleared_dc, added_sc


def load_json(path):
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def main():
    parser = argparse.ArgumentParser(
        description='Post-process review fields for cross-field consistency.'
    )
    parser.add_argument(
        '--input',
        default=os.path.join(SCRIPT_DIR, '..', 'appliance_api_erd_definitions_processed.json'),
        help='Input processed JSON file',
    )
    parser.add_argument(
        '--output',
        default=None,
        help='Output processed JSON file (default: overwrite input)',
    )
    args = parser.parse_args()

    entries = load_json(args.input)
    cleared_unit, cleared_dc, added_sc = apply_post_processing(entries)
    n_overrides = apply_overrides(entries)

    print(f"Cleared {cleared_unit} unit_of_measurement values for binary_sensor/switch")
    print(f"Cleared {cleared_dc} device_class values for non-temperature number")
    print(f"Added {added_sc} state_class=measurement for sensors with device_class")
    print(f"Reapplied {n_overrides} override fields")

    if args.output:
        with open(args.output, 'w', encoding='utf-8') as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)
        print(f"Output: {args.output}")
    else:
        with open(args.input, 'w', encoding='utf-8') as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)
        print(f"Output: {args.input}")


if __name__ == '__main__':
    main()