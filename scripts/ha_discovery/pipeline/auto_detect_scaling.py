#!/usr/bin/env python3
"""Auto-detect unit_of_measurement and scaling_factor from field names.

Parses parenthetical suffixes in field names (e.g. "(volts)", "(fahrenheit x 100)")
and inline scaling hints (e.g. "MilliliterX10", "Volts x 100") to populate
the review.unit_of_measurement and review.scaling_factor fields.

Only acts on fields with clear, unambiguous data in the field name itself —
never on the ERD description.

Usage:
    python3 scripts/auto_detect_scaling.py [--input ERD_FILE] [--output OUTPUT]
    python3 scripts/auto_detect_scaling.py --input erd_flattened.json --output erd_flattened.json
"""

import argparse
import json
import os
import re

import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pipeline_utils import SCRIPT_DIR, REPO_ROOT, load_json

# Mapping from normalized parenthetical suffix -> (unit_of_measurement, scaling_factor).
# Keys are lowercase. Values are (unit, scale) where None means "don't set".
UNIT_MAP = {
    # Temperature
    'f': ('°F', 1),
    'fahrenheit': ('°F', 1),
    'fahrenheit x 100': ('°F', 100),
    'f x 10': ('°F', 10),
    'degrees f': ('°F', 1),
    'degreesfx10': ('°F', 10),
    'signed fahrenheit': ('°F', 1),
    '0 f - 255 f': ('°F', 1),
    'c * 10': ('°C', 10),
    'celsius': ('°C', 1),
    'kelvin x 32': ('K', 32),

    # Electrical
    'volts': ('V', 1),
    'volts x 100': ('V', 100),
    'tenth of a volt': ('V', 10),
    'amps': ('A', 1),
    'amps x 100': ('A', 100),
    'tenth of an amp': ('A', 10),
    'watts': ('W', 1),
    'hz': ('Hz', 1),

    # Time
    'minutes': ('min', 1),
    'in minutes': ('min', 1),
    'seconds': ('s', 1),
    'seconds * 10': ('s', 10),
    'hours': ('h', 1),
    '0 - 60 minutes': ('min', 1),

    # Percentage
    'percentage': ('%', 1),
    'percent': ('%', 1),
    'percent x 100': ('%', 100),
    '%': ('%', 1),

    # Volume / Weight
    'gallons': ('gal', 1),
    '1/100 gallons': ('gal', 100),
    'liters': ('L', 1),
    'liters x 10': ('L', 10),
    'milliliters': ('mL', 1),
    'ounces': ('oz', 1),
    'ounces x 100': ('oz', 100),
    'oz': ('oz', 1),
    'grams': ('g', 1),
    'lbs x 10': ('lb', 10),
    'cubic feet x10': ('ft³', 10),

    # Pressure
    'in pa': ('Pa', 1),
    'inches of water x 1000': ('inH₂O', 1000),
    'inches': ('in', 1),

    # Speed
    'rpm': ('rpm', 1),
    'speed * 10': (None, 10),

    # Other
    'megabytes': ('MB', 1),
    'cups': ('cups', 1),
    'slices': ('slices', 1),
    'ug/m^3': ('μg/m³', 1),
    'mn-m': ('mN·m', 1),
    'x10': (None, 10),
    'x100': (None, 100),

    # Non-unit parentheticals — metadata, not units (set nothing)
    'actual': (None, None),
    'desired': (None, None),
    'compressor': (None, None),
    'compressor stage 2': (None, None),
    'fan hi': (None, None),
    'fan lo': (None, None),
    'heater': (None, None),
    'reverse valve': (None, None),
    'tank 2': (None, None),
    '24 - hour format': (None, None),
    '24-hour format': (None, None),
    '0-10': (None, None),
    '0-255': (None, None),
    '0xff if not begun': (None, None),
    '0 < x <= 4': (None, None),
    '0 < x <= 6': (None, None),
    'integer ounces': ('oz', 1),
    'max count': (None, None),
    'min count': (None, None),
    'max ounces': ('oz', 1),
    'min ounces': ('oz', 1),
    'max lbs x 10': ('lb', 10),
    'min lbs x 10': ('lb', 10),
    'load average x 100': (None, 100),
    'each': (None, None),
    'oz or count': ('oz', 1),
    'number of coins': (None, None),
    'extrapolated from writes during period': (None, None),
    'from the configuration': (None, None),
    'seconds since 1/1/1970': (None, None),
    'least significant 32 bits': (None, None),
    'most significant 32 bits': (None, None),

    # Special cases
    'pounds (x10)': ('lb', 10),
    'x100, 2sec avg': ('°F', 100),
    'load average x 100': (None, 100),
    'fahrenheit x 10': ('°F', 10),
    'celsius x 10': ('°C', 10),

}
PAREN_GROUPS = re.compile(r'\(([^()]+(?:\([^()]*\)[^()]*)*)\)')

# Inline unit+scaling patterns: extract both unit and scale from field name context.
# Each entry is (regex, resolver). Regex captures the scaling factor.
# Order matters — more specific patterns first.
# Uses .*? (non-greedy) to allow text between unit keyword and scaling,
# and (100|10) so 100 is tried before 10.
INLINE_UNIT_SCALING = [
    # Temperature with inline scaling: "°Fx10", "°Cx10"
    (re.compile(r'°(F|C).*?x\s*(100|10)', re.IGNORECASE), lambda m: ('°F' if m.group(1).upper() == 'F' else '°C', int(m.group(2)))),
    # "in F (X100)", "in F (X10)", "in degrees F (X10)"
    (re.compile(r'(?:in\s+degrees?\s+)?F\s*(?:\s*\([^)]*x\s*(100|10)\)|X(100|10))', re.IGNORECASE), lambda m: ('°F', int(m.group(1) or m.group(2)))),
    # "Fahrenheit x 10", "Celsius x 10"
    (re.compile(r'fahrenheit.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('°F', int(m.group(1)))),
    (re.compile(r'celsius.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('°C', int(m.group(1)))),
    # "in degrees C (X10)"
    (re.compile(r'in\s+degrees?\s+C\s*(?:\s*\([^)]*x\s*(100|10)\)|X(100|10))', re.IGNORECASE), lambda m: ('°C', int(m.group(1) or m.group(2)))),
    # "SecondsX10", "Seconds x 10", "Seconds ... x 10"
    # Use negative lookahead to avoid matching 'Secondary'
    (re.compile(r'\bseconds?(?!ary).*?x\s*(100|10)', re.IGNORECASE), lambda m: ('s', int(m.group(1)))),
    # "MilliliterX10", "Milliliter x 10"
    (re.compile(r'\bmilliliter.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('mL', int(m.group(1)))),
    # "GallonsX100", "Gallons x 100"
    (re.compile(r'\bgallon.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('gal', int(m.group(1)))),
    # "LiterX10", "Liter x 10" — exclude 'milliliter' with negative lookbehind
    (re.compile(r'(?<!milli)liter.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('L', int(m.group(1)))),
    # "Lbs x 10", "Lb x 10", "Lbs of selected item x 10"
    (re.compile(r'lbs?.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('lb', int(m.group(1)))),
    # "Ounce x 10", "Oz x 10"
    (re.compile(r'(?:ounce|oz).*?x\s*(100|10)', re.IGNORECASE), lambda m: ('oz', int(m.group(1)))),
    # "Percent x 10", "Percent x 100", "Percent LFL x 10"
    (re.compile(r'percent.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('%', int(m.group(1)))),
    # "Volts x 100", "VoltsX100"
    (re.compile(r'volt.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('V', int(m.group(1)))),
    # "Amps x 100", "AmpsX100"
    (re.compile(r'amp.*?x\s*(100|10)', re.IGNORECASE), lambda m: ('A', int(m.group(1)))),
    # "Inches of water x 1000"
    (re.compile(r'inches\s+of\s+water.*?x\s*(1000|100|10)', re.IGNORECASE), lambda m: ('inH₂O', int(m.group(1)))),
    # "x 10 (ounces)", "x 100 (ounces)" — scaling before parenthetical unit
    (re.compile(r'x\s*(100|10)\s*\(.*?(?:ounce|oz)', re.IGNORECASE), lambda m: ('oz', int(m.group(1)))),
    (re.compile(r'x\s*(100|10)\s*\(.*?volt', re.IGNORECASE), lambda m: ('V', int(m.group(1)))),
    (re.compile(r'x\s*(100|10)\s*\(.*?amp', re.IGNORECASE), lambda m: ('A', int(m.group(1)))),
]


def infer_unit_from_name(field_name):
    """Infer unit_of_measurement from keywords in the field name.

    Used when the parenthetical suffix has scaling info but no unit keyword,
    e.g. "Blower Minimum Runtime in minutes (X10)" -> unit='min' from 'minutes'.
    Returns (unit, scale) or (None, None).
    """
    name_lower = field_name.lower()

    # Skip compound units (e.g. "liter/minute", "inH2O/minute") — these are
    # flow rates that don't map to a single HA unit_of_measurement.
    if '/' in name_lower:
        return None, None

    # Temperature
    if '°f' in name_lower or 'fahrenheit' in name_lower or ' in f ' in name_lower or 'degrees f' in name_lower:
        return '°F', None
    if '°c' in name_lower or 'celsius' in name_lower or ' in c ' in name_lower or 'degrees c' in name_lower:
        return '°C', None
    if 'kelvin' in name_lower:
        return 'K', None

    # Time — exclude compound units, load average, and time of day
    if 'second' in name_lower and 'watt second' not in name_lower and '/' not in name_lower:
        return 's', None
    if 'minute' in name_lower and 'load average' not in name_lower and 'time of day' not in name_lower and '/' not in name_lower:
        return 'min', None

    # Volume / Weight — use word boundaries to avoid substring matches
    if 'milliliter' in name_lower:
        return 'mL', None
    if 'gallon' in name_lower:
        return 'gal', None
    if 'liter' in name_lower:
        return 'L', None
    if 'lbs' in name_lower or ' lb ' in name_lower:
        return 'lb', None
    if 'ounce' in name_lower or ' oz ' in name_lower:
        return 'oz', None
    if re.search(r'\bgrams?\b', name_lower) and 'ground gram' not in name_lower:
        return 'g', None
    # Other
    if 'voltage' in name_lower or 'volt' in name_lower:
        return 'V', None
    if 'current' in name_lower and ('amp' in name_lower or 'ampere' in name_lower):
        return 'A', None
    # Watt — exclude "watt seconds" (energy, not power)
    if 'watt' in name_lower and 'watt second' not in name_lower:
        return 'W', None
    if 'pressure' in name_lower and 'pa' in name_lower:
        return 'Pa', None
    if 'rpm' in name_lower:
        return 'rpm', None
    if 'humidity' in name_lower and 'update period' not in name_lower:
        return '%', None
    if 'percentage' in name_lower or 'percent' in name_lower:
        return '%', None
    if 'battery' in name_lower and ('level' in name_lower or 'health' in name_lower):
        return '%', None
    if 'rssi' in name_lower or 'ble' in name_lower and 'scan result' in name_lower:
        return 'dBm', None
    if 'energy' in name_lower and ('cumulative' in name_lower or 'consumption' in name_lower or 'watt second' in name_lower):
        return 'kWh', None
    if 'power' in name_lower and 'maximum' in name_lower:
        return '%', None
    if 'temperature' in name_lower or re.search(r'\btemp\b', name_lower):
        return '°F', None

    return None, None


def detect_unit_and_scaling(field_name, field_bits=None):
    """Detect unit_of_measurement and scaling_factor from a field name.

    Checks parenthetical suffix for unit, then inline patterns for scaling.
    Both sources are consulted so that e.g. "(volts)" gives unit=V while
    "x 100" in the name gives scale=100.
    Returns (unit_of_measurement, scaling_factor) or (None, None).
    """
    # Skip 1-bit boolean flags — they are on/off indicators, not measurements.
    # Multi-bit fields (e.g., 3-bit temperature sensor) can carry real values.
    # Skip compound units (e.g. "GallonsX100/Minute") — inline patterns
    # would match the unit keyword but miss the compound context.
    # Check only the non-parenthetical portion to allow "(1/100 Gallons)" etc.
    # Allow menu separators like "Biscuit/Dinner Roll" (/[A-Z]word followed by space).
    name_without_parens = re.sub(r'\s*\(.*?\)\s*$', '', field_name)
    has_compound = '/' in name_without_parens and not re.search(r'/[A-Z][a-z]+ ', name_without_parens)
    if has_compound and 'min/max' not in field_name.lower():
        return None, None

    unit = None
    scale = None
    paren_matched = False

    # Check parenthetical suffix first (most reliable unit signal).
    groups = PAREN_GROUPS.findall(field_name)
    if groups:
        suffix = groups[-1].strip().lower()
        if suffix in UNIT_MAP:
            paren_matched = True
            p_unit, p_scale = UNIT_MAP[suffix]
            if p_unit is not None:
                unit = p_unit
            if p_scale is not None and p_scale != 1:
                scale = p_scale
            # If parenthetical gave us scaling but no unit, infer unit from name.
            if p_unit is None and p_scale is not None:
                name_unit, _ = infer_unit_from_name(field_name)
                if name_unit:
                    unit = name_unit

    # Check inline unit+scaling patterns (may add scaling even if paren matched).
    # Inline patterns are more specific (they match the full field name context),
    # so they override parenthetical results when they provide both unit and scale.
    for pattern, resolver in INLINE_UNIT_SCALING:
        m = pattern.search(field_name)
        if m:
            i_unit, i_scale = resolver(m)
            if i_unit is not None and i_scale is not None and i_scale != 1:
                # Inline pattern provides both unit and scaling — trust it over parenthetical.
                unit = i_unit
                scale = i_scale
            elif i_unit is not None and unit is None:
                unit = i_unit
            if i_scale is not None and i_scale != 1 and scale is None:
                scale = i_scale

    # Fallback: if neither parenthetical nor inline patterns gave us a unit,
    # infer from field name keywords (e.g. "temperature in degrees F").
    if unit is None and not paren_matched:
        name_unit, _ = infer_unit_from_name(field_name)
        if name_unit:
            unit = name_unit

    return unit, scale


def apply_detection(entries):
    """Walk all entries, detect unit/scaling, and overwrite review fields.

    Only resets unit_of_measurement when a new value is detected, preserving
    manually set units not recognized by the auto-detector.
    Returns (total_checked, total_matched, total_applied) counts.
    """
    total_checked = 0
    total_matched = 0
    total_applied = 0

    for entry in entries:
        field_name = entry.get('field_name', '')
        review = entry.setdefault('review', {})

        # Skip non-numeric types — unit/scaling only applies to numeric fields.
        field_type = entry.get('field_type', '')
        if field_type not in ('u8', 'u16', 'u32', 'i8', 'i16', 'i32'):
            # Clear stale units/scaling from previous runs on non-numeric fields
            # (e.g., enum fields that inherited units from a sibling field override).
            if review.get('unit_of_measurement'):
                review['unit_of_measurement'] = None
                total_applied += 1
            if review.get('scaling_factor'):
                review['scaling_factor'] = None
                total_applied += 1
            continue

        total_checked += 1

        unit, scale = detect_unit_and_scaling(field_name, entry.get('field_bits'))
        if scale is None:
            # Try field name as fallback: "CLC Temperature x 100"
            m = re.search(r'\bx\s+(\d+)', field_name, re.IGNORECASE)
            if m:
                scale = int(m.group(1))
        if scale is None:
            # Try ERD description as fallback: "Kelvin x 32", "degrees F x 10"
            desc = entry.get('erd_description', '')
            m = re.search(r'\bx\s+(\d+)', desc, re.IGNORECASE)
            if m:
                scale = int(m.group(1))
        if unit is None and scale is None:
            # Clear stale units from previous runs when the detector
            # no longer finds any signal for this field.
            if review.get('unit_of_measurement'):
                review['unit_of_measurement'] = None
                total_applied += 1
            continue

        total_matched += 1
        applied = False

        # Always write when detected, overwriting stale or incorrect values.
        if unit is not None:
            review['unit_of_measurement'] = unit
            applied = True

        if scale is not None and scale != 1 and 'scaling_factor' not in review:
            review['scaling_factor'] = scale
            applied = True

        if applied:
            total_applied += 1

    return total_checked, total_matched, total_applied



def main():
    parser = argparse.ArgumentParser(
        description='Auto-detect unit_of_measurement and scaling_factor from field names.'
    )
    parser.add_argument(
        '--input',
        default=os.path.join(SCRIPT_DIR, '..', 'appliance_api_erd_definitions_processed.json'),
        help='Input flattened review file (default: appliance_api_erd_definitions_processed.json)',
    )
    parser.add_argument(
        '--output',
        default=os.path.join(SCRIPT_DIR, '..', 'appliance_api_erd_definitions_processed.json'),
        help='Output path (default: overwrites input)',
    )
    args = parser.parse_args()

    entries = load_json(args.input)

    checked, matched, applied = apply_detection(entries)

    if args.output != args.input:
        with open(args.output, 'w', encoding='utf-8') as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)
    else:
        with open(args.output, 'w', encoding='utf-8') as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)

    print(f"Checked {checked} numeric fields")
    print(f"Matched {matched} fields with detectable unit/scaling")
    print(f"Applied {applied} fields (skipped already-set values)")
    print(f"Output: {args.output}")


if __name__ == '__main__':
    main()