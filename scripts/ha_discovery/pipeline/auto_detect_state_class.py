#!/usr/bin/env python3
"""Auto-detect state_class from field names.

state_class is only meaningful for sensor domain entities.
- 'measurement': instantaneous values (temperature, voltage, current, etc.)
- 'total': cumulative counters that can go up and down (energy, water, gas)
- 'total_increasing': counters that only increase (cycle counts, runtime)

This script only assigns state_class for fields that already have a device_class.
"""

import argparse
import json
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _word_bound(name_lower, kw):
    """Check if kw appears as a whole word (or at word boundary) in name_lower.

    Treats underscores as word separators so that 'my_cycle' matches 'cycle'.
    """
    normalized = name_lower.replace('_', ' ')
    return bool(re.search(r'\b' + re.escape(kw) + r'\b', normalized))


def infer_state_class(entry):
    """Infer state_class from field name, device_class, and ha_domain.

    state_class is ONLY valid for sensor domain entities.
    Returns (state_class, confidence) or (None, None).
    """
    field_name = entry.get('field_name', '')
    device_class = entry.get('review', {}).get('device_class')
    ha_domain = entry.get('review', {}).get('ha_domain')
    name_lower = field_name.lower()

    # Only assign state_class for sensor domain
    if ha_domain != 'sensor':
        return None, None

    # HA only supports long-term statistics for numeric sensors.
    # Non-numeric device classes (enum, timestamp, date, uptime) output text
    # strings — state_class on these causes HA to reject the sensor for LTS.
    NON_NUMERIC_DEVICE_CLASSES = {'enum', 'timestamp', 'date', 'uptime'}
    if device_class in NON_NUMERIC_DEVICE_CLASSES:
        return None, None

    # Only assign state_class for fields with a device_class

    # --- Total: cumulative counters that can go up and down ---
    total_keywords = [
        'cumulative', 'total', 'consumption', 'usage', 'since clear',
    ]
    if any(kw in name_lower for kw in total_keywords):
        # Exclude averages (instantaneous derived values)
        if 'average' in name_lower:
            return None, None
        if device_class in ('energy', 'gas', 'water', 'volume'):
            return 'total', 0.9

    # --- Total_increasing: counters that only increase ---
    # Use word-boundary matching to avoid 'cycle' matching 'MyCycle'
    total_increasing_keywords = [
        'count', 'number of', 'runtime', 'uptime',
    ]
    if any(kw in name_lower for kw in total_increasing_keywords):
        return 'total_increasing', 0.85

    # 'cycle' requires word-boundary match (not substring)
    if _word_bound(name_lower, 'cycle'):
        # Only for non-temperature device classes (MyCycle is a product feature)
        if device_class not in ('temperature',):
            return 'total_increasing', 0.8

    # --- Measurement: instantaneous values ---
    # Per HA SENSOR_DEVICE_CLASS_STATE_CLASSES:
    # - volume only allows total/total_increasing (not measurement)
    # - power only allows measurement/total (not total_increasing)
    # - energy only allows total/total_increasing (not measurement)
    # - gas only allows total/total_increasing (not measurement)
    # - water only allows total/total_increasing (not measurement)
    measurement_classes = [
        'temperature', 'humidity', 'pressure', 'voltage', 'current',
        'frequency', 'signal_strength', 'battery',
        'illuminance', 'pm25', 'weight',
    ]
    if device_class in measurement_classes:
        # Exclude cumulative/total fields from measurement
        if not any(kw in name_lower for kw in total_keywords):
            return 'measurement', 0.9

    return None, None


def apply_detection(entries):
    """Walk all entries, detect state_class, and overwrite review field.

    Only overwrites state_class when a new value is detected, preserving
    manually assigned state_class not inferred by the detector.
    """
    total_checked = 0
    total_matched = 0
    total_applied = 0

    for entry in entries:
        device_class = entry.get('review', {}).get('device_class')
        if not device_class:
            continue

        total_checked += 1
        review = entry.get('review', {})

        sc, confidence = infer_state_class(entry)
        if sc is None:
            continue

        total_matched += 1
        review['state_class'] = sc
        review['_sc_confidence'] = confidence
        total_applied += 1

    return total_checked, total_matched, total_applied


def load_json(path):
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def main():
    parser = argparse.ArgumentParser(
        description='Auto-detect state_class from field names and device_class.'
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
    checked, matched, applied = apply_detection(entries)

    print(f"Checked {checked} fields with device_class")
    print(f"Matched {matched} fields with detectable state_class")
    print(f"Applied {applied} fields")

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