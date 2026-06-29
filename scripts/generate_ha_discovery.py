#!/usr/bin/env python3
"""
Script to generate Home Assistant MQTT Discovery JSONL files from the
public-appliance-api-documentation ERD definitions.

Reads appliance_api_erd_definitions.json and produces category-specific
JSONL files in ha_discovery/, each line being a compact JSON object
defining one HA entity.

Uses ERD data type information to generate proper value/command templates:
  - Signed integer types (i8, i16, i32) get two's-complement conversion
  - Scaling factors are applied with proper decimal places
  - Enum types get proper hex-to-label mapping
  - Multi-field ERDs are classified (single/byte_offset/bitfield/mixed)
  - Bit-field sub-values are extracted with proper masking

Each JSONL line has these keys:
  i  - ERD ID (lowercase hex, zero-padded to 4 chars)
  n  - Entity name (human-readable)
  d  - HA domain: sensor, binary_sensor, switch, select, number, button
  ds - ERD data size in bytes (total ERD payload size)
  vt - Jinja2 value_template for decoding the hex payload
  ct - Jinja2 command_template for encoding commands (writable ERDs)
  u  - unit_of_measurement
  dc - device_class
  sc - state_class (e.g. "total", "measurement")
  fi - Field ID for sub-fields within a multi-byte ERD
  p  - Paired ERD ID (hex string)
  r  - Role: "request" or "status"
  o  - JSON array of options (for select domain)
  dt - Data type for number domain
  sf - Scale factor for number domain
"""

import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


# Valid HA device_class values per domain. Invalid combos are silently dropped.
VALID_DEVICE_CLASSES = {
    'button': {'restart'},
    'switch': {'outlet', 'switch'},
    'binary_sensor': {
        'battery', 'battery_charging', 'carbon_monoxide', 'cold',
        'connectivity', 'door', 'garage_door', 'gas', 'heat',
        'light', 'lock', 'moisture', 'motion', 'moving',
        'occupancy', 'opening', 'plug', 'power', 'presence',
        'problem', 'running', 'safety', 'smoke', 'sound',
        'tamper', 'update', 'vibration', 'window',
    },
    'sensor': {
        'date', 'enum', 'timestamp', 'uptime',
        'absolute_humidity', 'apparent_power', 'aqi', 'area',
        'atmospheric_pressure', 'battery', 'blood_glucose_concentration',
        'carbon_monoxide', 'carbon_dioxide', 'conductivity', 'current',
        'data_rate', 'data_size', 'distance', 'duration', 'energy',
        'energy_distance', 'energy_storage', 'frequency', 'gas',
        'humidity', 'illuminance', 'irradiance', 'moisture', 'monetary',
        'nitrogen_dioxide', 'nitrogen_monoxide', 'nitrous_oxide', 'ozone',
        'ph', 'pm1', 'pm10', 'pm25', 'pm4', 'power_factor', 'power',
        'precipitation', 'precipitation_intensity', 'pressure',
        'reactive_energy', 'reactive_power', 'signal_strength',
        'sound_pressure', 'speed', 'sulphur_dioxide', 'temperature',
        'temperature_delta', 'volatile_organic_compounds',
        'volatile_organic_compounds_parts', 'voltage', 'volume',
        'volume_storage', 'volume_flow_rate', 'water', 'weight',
        'wind_direction', 'wind_speed',
    },
}


def _decimal_places(scaling_factor: int) -> int:
    """Return the number of decimal places needed to represent 1/scaling_factor exactly.

    For powers of 10 this is simply the number of digits (e.g. 10 -> 1, 100 -> 2).
    For other factors (e.g. 32) it finds the smallest dp where round(1/sf, dp) == 1/sf.
    """
    if scaling_factor <= 0:
        return 0
    for dp in range(1, 10):
        if round(1.0 / scaling_factor, dp) == 1.0 / scaling_factor:
            return dp
    return 3  # fallback

def _is_valid_device_class(domain: str, device_class: str) -> bool:
    """Check if device_class is valid for the given HA domain."""
    if not device_class:
        return True
    valid = VALID_DEVICE_CLASSES.get(domain)
    if valid is None:
        return True  # no restrictions for this domain
    return device_class in valid

# Category ranges matching the plan
CATEGORIES = {
    "common": (0x0000, 0x0FFF),
    "refrigeration": (0x1000, 0x1FFF),
    "laundry": (0x2000, 0x2FFF),
    "dishwasher": (0x3000, 0x3FFF),
    "waterheater": (0x4000, 0x4FFF),
    "range": (0x5000, 0x5FFF),
    "airconditioning": (0x7000, 0x7FFF),
    "waterfilter": (0x8000, 0x8FFF),
    "smallappliance": (0x9000, 0x9FFF),
    "energy": (0xD000, 0xDFFF),
}


# ---------------------------------------------------------------------------
# Basic helpers
# ---------------------------------------------------------------------------

def parse_erd_id(erd_id_str: str) -> int:
    """Convert ERD ID string (e.g., '0x0001') to integer."""
    return int(erd_id_str, 16)


def erd_id_to_hex(erd_id_str: str) -> str:
    """Convert ERD ID string to lowercase hex, zero-padded to 4 chars."""
    return format(parse_erd_id(erd_id_str), '04x')


def get_category(erd_id: int) -> Optional[str]:
    """Return the category name for an ERD ID, or None if unclassified."""
    for name, (lo, hi) in CATEGORIES.items():
        if lo <= erd_id <= hi:
            return name
    return None


def get_erd_byte_size(erd_data: List[Dict]) -> int:
    """Compute the actual byte size of an ERD from its data field definitions.

    Each data field has an 'offset' (byte offset) and 'size' (byte count).
    Fields may overlap (bit-fields share the same bytes), so the true ERD byte
    size is the highest (offset + size) value across all fields.
    """
    if not erd_data:
        return 0
    return max((d.get('offset', 0) + d.get('size', 0)) for d in erd_data)


def get_first_enum_values(erd_data: List[Dict]) -> Dict[str, str]:
    """Return the values dict from the first enum-typed data field, or {}."""
    for d in erd_data:
        if d.get('type') == 'enum':
            return d.get('values', {})
    return {}


def _get_first_enum_field_info(erd_data: List[Dict]) -> Tuple[Dict[str, str], int]:
    """Return (values_dict, field_size) for the first enum-typed data field.

    field_size is the byte width of the enum field (used to build the hex
    extraction slice in value templates).  Falls back to ({}, 1) when no
    enum field is present.
    """
    for d in erd_data:
        if d.get('type') == 'enum':
            return d.get('values', {}), max(1, d.get('size', 1))
    return {}, 1


# ---------------------------------------------------------------------------
# Multi-field ERD helpers
# ---------------------------------------------------------------------------

def _is_signed_type(type_str: str) -> bool:
    """Return True if the type string represents a signed integer (e.g. 'i8', 'i16', 'i32')."""
    return bool(re.match(r'^i\d+$', type_str))

def _compute_number_range(data_type: str, scaling_factor: int) -> Tuple[float, float, float]:
    """Return (min, max, step) for a number entity based on data type and scale factor.

    Returns user-facing values (after scaling), not raw byte values.
    E.g. u16 with sf=10 -> (0, 6553.5, 0.1)
    """
    bounds = {
        'u8': (0, 255),
        'i8': (-128, 127),
        'u16': (0, 65535),
        'i16': (-32768, 32767),
        'u32': (0, 4294967295),
        'i32': (-2147483648, 2147483647),
    }
    raw_min, raw_max = bounds.get(data_type, (0, 255))
    step = 1.0 / scaling_factor
    return (raw_min / scaling_factor, raw_max / scaling_factor, step)


def _get_primary_data_type(erd_data: List[Dict]) -> str:
    """Return the type of the primary (first non-reserved, non-bitfield) data field."""
    for d in erd_data:
        if not _is_reserved_field(d.get('name', '')) and not _has_bits(d):
            return d.get('type', 'u8')
    return 'u8'

def _jinja2_escape(s: str) -> str:
    """Escape a string for safe use inside a single-quoted Jinja2 dict literal.

    Replaces single quotes with backslash-escaped single quotes so that
    values like "Don't Care" don't break the dict literal syntax.
    """
    return s.replace("'", "\\'")

def _is_reserved_field(name: str) -> bool:
    """Return True if a field name indicates it is a reserved/padding field."""
    return 'reserved' in name.lower()


def _leaf_field_name(name: str) -> str:
    """Return the leaf portion of a potentially dot-qualified field name.

    E.g. "Allowed Selections.Cyclic Supported" -> "Cyclic Supported".
    Handles edge cases like "Air Purifier.PM2.5" where the dot is part of
    the value (decimal/unicode) rather than a namespace separator.
    """
    parts = name.split('.')
    if len(parts) == 1:
        return name.strip()
    leaf = parts[-1].strip()
    # If the last part is purely numeric and the second-to-last part ends
    # with an alphanumeric char (letter or digit), the dot is likely part
    # of a decimal or version number (e.g., "PM2.5" -> "PM2" + ".5").
    if len(parts) >= 2 and leaf.isdigit() and parts[-2][-1:].isalnum():
        return (parts[-2] + '.' + leaf).strip()
    return leaf


def _field_slug(name: str) -> str:
    """Convert a field name to a compact ASCII slug suitable for unique_ids.

    Examples:
        "Critical Major"          -> "critical_major"
        "GH (Fan Hi)"             -> "gh_fan_hi"
        "Cyclic Supported"        -> "cyclic_supported"
    """
    slug = re.sub(r'[^a-z0-9]+', '_', name.lower())
    slug = slug.strip('_')
    # Cap at 64 chars to keep MQTT topic segments reasonable without losing
    # enough uniqueness to cause collisions within a single ERD's sub-fields.
    return slug[:64]


def _has_bits(field: Dict) -> bool:
    """Return True if a field carries a 'bits' sub-object (i.e. it is a bit-field)."""
    return 'bits' in field and isinstance(field['bits'], dict)


def _get_non_reserved_fields(erd_data: List[Dict]) -> List[Dict]:
    """Return data fields whose names do not indicate reserved/padding content."""
    return [d for d in erd_data if not _is_reserved_field(d.get('name', ''))]


def _is_version_field(name: str) -> bool:
    """Return True if a field name matches a version component pattern.

    Matches: Critical Major, Critical Minor, Non-Critical Major, Non-Critical Minor
    (with optional prefix like "UI ", "MC ", "Inverter ", etc. and optional " Version" suffix).
    """
    n = name.lower()
    for pattern in ('critical major', 'critical minor', 'non-critical major', 'non-critical minor'):
        if pattern in n:
            return True
    return False


def _version_field_role(name: str) -> Optional[str]:
    """Return the version role of a field, or None if not a version field.

    Returns one of: 'crit_major', 'crit_minor', 'noncrit_major', 'noncrit_minor'.
    """
    n = name.lower()
    # Check non-critical before critical to avoid substring match
    # ("critical major" is a substring of "non-critical major")
    if 'non-critical major' in n:
        return 'noncrit_major'
    if 'non-critical minor' in n:
        return 'noncrit_minor'
    if 'critical major' in n:
        return 'crit_major'
    if 'critical minor' in n:
        return 'crit_minor'
    return None


def _is_parametric_field(name: str) -> bool:
    """Return True if a field name is a parametric version component."""
    n = name.lower()
    return ('parametric major' in n) or ('parametric minor' in n)


def _extract_board_prefix(name: str) -> str:
    """Extract the board prefix from a field name like 'UI Critical Major Version'.

    Returns the prefix before the version role keyword, stripped.
    E.g. 'UI Critical Major Version' -> 'UI', 'Critical Major' -> ''.
    """
    n = name.strip()
    # Check longer keywords first to avoid substring matches
    for keyword in ('Non-Critical Major', 'Non-Critical Minor',
                    'Critical Major', 'Critical Minor',
                    'Parametric Major', 'Parametric Minor'):
        idx = n.find(keyword)
        if idx >= 0:
            return n[:idx].rstrip()
    return n


def _is_simple_version_erd(erd_data: List[Dict]) -> bool:
    """Return True if the ERD is a simple 4-byte version ERD.

    Must have exactly 4 non-reserved u8 fields at offsets 0-3 with the pattern:
    Critical Major, Critical Minor, Non-Critical Major, Non-Critical Minor.
    """
    nr = _get_non_reserved_fields(erd_data)
    if len(nr) != 4:
        return False
    expected_roles = ['crit_major', 'crit_minor', 'noncrit_major', 'noncrit_minor']
    for i, field in enumerate(nr):
        if field.get('type') != 'u8':
            return False
        if field.get('offset', 0) != i:
            return False
        role = _version_field_role(field.get('name', ''))
        if role != expected_roles[i]:
            return False
    return True


def _group_multi_board_version_fields(erd_data: List[Dict]) -> List[Dict]:
    """Group fields of a multi-board version ERD by board prefix.

    Returns a list of groups, each with:
      - 'prefix': board name (e.g. 'UI', 'MC')
      - 'version_fields': list of 4 version fields in order (crit_major, crit_minor, noncrit_major, noncrit_minor)
      - 'parametric_fields': list of 2 parametric fields (major, minor) or empty
    """
    nr = _get_non_reserved_fields(erd_data)
    if not nr:
        return []

    # Group fields by board prefix
    boards: Dict[str, Dict[str, Dict]] = {}
    for field in nr:
        name = field.get('name', '')
        prefix = _extract_board_prefix(name)
        if prefix not in boards:
            boards[prefix] = {}
        role = _version_field_role(name)
        if role:
            boards[prefix][role] = field
        elif _is_parametric_field(name):
            n = name.lower()
            p_role = 'parametric_major' if 'parametric major' in n else 'parametric_minor'
            boards[prefix][p_role] = field

    # Filter to boards that have all 4 version components
    required = {'crit_major', 'crit_minor', 'noncrit_major', 'noncrit_minor'}
    groups = []
    for prefix, roles in boards.items():
        if required.issubset(roles.keys()):
            groups.append({
                'prefix': prefix,
                'version_fields': [
                    roles['crit_major'],
                    roles['crit_minor'],
                    roles['noncrit_major'],
                    roles['noncrit_minor'],
                ],
                'parametric_fields': [
                    roles.get('parametric_major'),
                    roles.get('parametric_minor'),
                ],
            })
    return groups


def _classify_erd_data(erd_data: List[Dict]) -> str:
    """Classify how sub-fields of an ERD should be expanded for HA discovery.

    Returns one of:
        'single'              - one entity covers the whole ERD value
        'byte_offset'         - multiple fields at distinct byte positions (no bits)
        'bitfield'            - all non-reserved fields are bit-flags at same byte range
        'mixed'               - one primary byte-offset field + additional bit-flag fields
        'version'             - 4-part version ERD (crit.major.noncrit.major.noncrit.minor)
        'multi_board_version' - multi-board version ERD (e.g. dishwasher system software)
    """
    nr = _get_non_reserved_fields(erd_data)
    if len(nr) <= 1:
        return 'single'

    # Check for simple 4-byte version ERD before other classifications
    if _is_simple_version_erd(erd_data):
        return 'version'

    # Check for multi-board version ERD
    groups = _group_multi_board_version_fields(erd_data)
    if groups:
        # Only classify as multi-board if ALL non-reserved fields are accounted for
        total_accounted = 0
        for g in groups:
            total_accounted += len(g['version_fields'])
            total_accounted += len([f for f in g['parametric_fields'] if f is not None])
        if total_accounted == len(nr):
            return 'multi_board_version'

    with_bits = [d for d in nr if _has_bits(d)]
    no_bits = [d for d in nr if not _has_bits(d)]

    if not with_bits:
        # All byte-offset: split only if they occupy different (offset, size) ranges
        offsets = {(d.get('offset', 0), d.get('size', 1)) for d in no_bits}
        return 'byte_offset' if len(offsets) > 1 else 'single'
    elif not no_bits:
        # Pure bit-field ERD
        return 'bitfield'
    else:
        # Mixed: primary value field + bit-flag fields
        return 'mixed'


# ---------------------------------------------------------------------------
# Value template generators
# ---------------------------------------------------------------------------

def _byte_subfield_value_template(field: Dict, erd_scaling: int) -> str:
    """Generate a Jinja2 value_template that extracts one byte-offset sub-field.

    The template slices the right hex-char range from the full ERD hex payload,
    then converts to a number (applying scaling if needed) or an enum label.
    """
    offset = field.get('offset', 0)
    size = field.get('size', 1)
    hex_start = offset * 2
    hex_end = (offset + size) * 2
    field_type = field.get('type', 'u8')

    if field_type == 'enum':
        enum_values = field.get('values', {})
        valid_pairs = sorted(
            [(int(k), v) for k, v in enum_values.items() if v != 'Request Consumed'],
            key=lambda x: x[0]
        )
        if not valid_pairs:
            return f"{{{{ value[{hex_start}:{hex_end}] }}}}"
        hex_chars = size * 2
        mapping = ', '.join(f"'{k:0{hex_chars}x}': '{_jinja2_escape(v)}'" for k, v in valid_pairs)
        return f"{{{{ {{{mapping}}}.get(value[{hex_start}:{hex_end}], 'Unknown') }}}}"
    elif field_type == 'bool':
        return f"{{{{ '01' if value[{hex_start}:{hex_end}] != '00' else '00' }}}}"
    else:
        # Numeric types: u8, u16, u32, i8, i16, i32, etc.
        if _is_signed_type(field_type):
            max_val = 2 ** (size * 8)
            half_val = max_val // 2
            if erd_scaling and erd_scaling > 1:
                dp = _decimal_places(erd_scaling)
                return (f"{{{{ ((value[{hex_start}:{hex_end}] | int(base=16)) - {max_val}"
                        f" if (value[{hex_start}:{hex_end}] | int(base=16)) >= {half_val}"
                        f" else (value[{hex_start}:{hex_end}] | int(base=16)))"
                        f" / {erd_scaling} | round({dp}) }}}}")
            else:
                return (f"{{{{ (value[{hex_start}:{hex_end}] | int(base=16)) - {max_val}"
                        f" if (value[{hex_start}:{hex_end}] | int(base=16)) >= {half_val}"
                        f" else (value[{hex_start}:{hex_end}] | int(base=16)) }}}}")
        elif erd_scaling and erd_scaling > 1:
            dp = _decimal_places(erd_scaling)
            return (f"{{{{ (value[{hex_start}:{hex_end}] | int(base=16))"
                    f" / {erd_scaling} | round({dp}) }}}}")
        else:
            return f"{{{{ value[{hex_start}:{hex_end}] | int(base=16) }}}}"


def _bitfield_sub_value_template(field: Dict) -> str:
    """Generate a Jinja2 value_template that extracts one bit-field sub-field.

    For 1-bit fields the template outputs '01' (on) or '00' (off) so that it
    works with the binary_sensor payload_on/payload_off defaults already
    hardcoded in publish_next_ha_discovery_entity_().
    For multi-bit fields the template outputs the extracted integer.
    """
    byte_offset = field.get('offset', 0)
    byte_size = field.get('size', 1)
    bits = field.get('bits', {})
    bit_offset = bits.get('offset', 0)
    bit_size = bits.get('size', 1)
    hex_start = byte_offset * 2
    hex_end = (byte_offset + byte_size) * 2

    if bit_size == 1:
        divisor = 2 ** bit_offset
        return (f"{{{{ '01' if ((value[{hex_start}:{hex_end}] | int(base=16))"
                f" // {divisor}) % 2 else '00' }}}}")
    else:
        divisor = 2 ** bit_offset
        modulus = 1 << bit_size
        return (f"{{{{ ((value[{hex_start}:{hex_end}] | int(base=16))"
                f" // {divisor}) % {modulus} }}}}")

def _version_value_template(fields: List[Dict]) -> str:
    """Generate a Jinja2 value_template for a 4-part version ERD.

    Produces a dotted decimal string like '1.0.2.3' from 4 u8 fields
    at consecutive byte offsets.
    """
    parts = []
    for field in fields:
        offset = field.get('offset', 0)
        hex_start = offset * 2
        hex_end = (offset + 1) * 2
        parts.append(f"(value[{hex_start}:{hex_end}] | int(base=16) | string)")
    return '{{ ' + ' + \".\" + '.join(parts) + ' }}'


def _parametric_version_value_template(fields: List[Dict]) -> str:
    """Generate a Jinja2 value_template for a 2-part parametric version.

    Produces a dotted decimal string like '1.2' from 2 u8 fields.
    """
    parts = []
    for field in fields:
        if field is None:
            return ''
        offset = field.get('offset', 0)
        hex_start = offset * 2
        hex_end = (offset + 1) * 2
        parts.append(f"(value[{hex_start}:{hex_end}] | int(base=16) | string)")
    if len(parts) != 2:
        return ''
    return '{{ ' + ' + \".\" + '.join(parts) + ' }}'


def _unit_to_ha(unit: str) -> str:
    """Convert an API unit string to the Home Assistant display unit."""
    return {'degF': '\u00b0F', 'degC': '\u00b0C'}.get(unit, unit)


def _compute_sensor_value_template(scaling_factor: int, data_size: int, signed: bool = False) -> str:
    """Return the Jinja2 value_template for a numeric sensor ERD.

    When ``signed`` is True the template applies two's-complement sign extension
    so that negative values (e.g. an int16 encoded as 0xFFFF) are reported as
    negative numbers rather than large positive values.
    """
    if signed:
        max_val = 2 ** (data_size * 8)
        half_val = max_val // 2
        if scaling_factor > 1:
            dp = _decimal_places(scaling_factor)
            return (f'{{{{ ((value | int(base=16)) - {max_val}'
                    f' if (value | int(base=16)) >= {half_val}'
                    f' else (value | int(base=16))) / {scaling_factor} | round({dp}) }}}}')
        return (f'{{{{ (value | int(base=16)) - {max_val}'
                f' if (value | int(base=16)) >= {half_val}'
                f' else (value | int(base=16)) }}}}')
    if scaling_factor > 1:
        dp = _decimal_places(scaling_factor)
        return f'{{{{ (value | int(base=16)) / {scaling_factor} | round({dp}) }}}}'
    return '{{ value | int(base=16) }}'


def _string_value_template(data_size: int) -> str:
    """Return a Jinja2 value_template for string-type ERDs.

    GE API model/serial are plain ASCII. The MQTT payload is hex,
    so the template converts each hex byte pair to ASCII, skipping
    null bytes and stripping trailing '_' padding.
    """
    # Build a lookup string for ASCII 0x20-0x7E (printable range).
    # Index 0 maps to 0x20 (' '), index 0x5E maps to 0x7E ('~').
    chars = ''.join(chr(i) for i in range(0x20, 0x7F))
    chars_escaped = chars.replace("'", "\\'")
    return (
        "{% set chars = '" + chars_escaped + "' %}"
        "{% set ns = namespace(value='') %}"
        "{% for i in range(0, value | length, 2) %}"
        "{% set b = value[i:i+2] | int(base=16) %}"
        "{% if b >= 0x20 and b <= 0x7E %}"
        "{% set ns.value = ns.value ~ chars[b - 0x20] %}"
        "{% endif %}"
        "{% endfor %}"
        "{{ ns.value.rstrip('_') }}"
    )


def _infer_unit_from_field_name(field_name: str, parent_unit: str) -> str:
    """Override the parent ERD unit when the field name explicitly names a unit.

    This handles ERDs like 0x7705 which has one field in degF and one in degC but
    the parent unit_of_measurement is degF.  If the field name contains the
    word 'Celsius' the unit is overridden to degC, and vice versa for 'Fahrenheit'.
    Otherwise the parent unit is returned unchanged.
    """
    name_lower = field_name.lower()
    if 'celsius' in name_lower:
        return '\u00b0C'
    if 'fahrenheit' in name_lower:
        return '\u00b0F'
    return parent_unit


def _compute_binary_sensor_value_template(data_size: int) -> str:
    """Return value_template for a binary_sensor ERD.

    The MQTT payload is raw hex (e.g. '00', '01'). HA's default payload_on/off
    are 'ON'/'OFF', so we always need a template to convert.
    For multi-byte ERDs we slice the first two hex chars to get the first byte.
    """
    hex_chars = data_size * 2
    tmpl = f"value[:{hex_chars}]" if hex_chars > 2 else "value"
    return f"{{{{ 'ON' if {tmpl} | int(base=16) != 0 else 'OFF' }}}}"
def _compute_switch_value_template(data_size: int) -> str:
    """Return empty value_template for switch.

    Switches use state_on/state_off and payload_on/payload_off instead of
    value_template, since HA ignores state_on/off when value_template is set.
    """
    return ''


def _paired_primary_field_template(erd_by_id: Dict[str, Dict], paired_erd_str: str,
                                    erd_scaling: int, for_switch: bool = False) -> Optional[str]:
    """Return a Jinja2 value_template that extracts the primary field from a
    paired ERD's full hex payload, or None if the paired ERD isn't found or
    has no non-bitfield primary field.

    When for_switch=True, returns a raw hex extraction template (e.g. value[0:2])
    so the output matches state_on/state_off. Otherwise uses the full field
    template (enum labels, scaling, etc).
    """
    pf = _get_primary_field(erd_by_id, paired_erd_str)
    if pf is None:
        return None
    if for_switch:
        # Extract just the hex bytes for the primary field.
        offset = pf.get('offset', 0)
        size = pf.get('size', 1)
        hex_start = offset * 2
        hex_end = (offset + size) * 2
        return f"{{{{ value[{hex_start}:{hex_end}] }}}}"
    return _byte_subfield_value_template(pf, erd_scaling)

def _select_options_and_templates(enum_values: Dict[str, str], data_size: int):
    """Build options_json, value_template and command_template for a select entity.

    'Request Consumed' (value 255) is excluded from selectable options because
    it is a write-only protocol marker (not a valid user-visible state).
    Returns (options_json_str, value_template_str, command_template_str).
    """
    # Filter out 'Request Consumed' (255) and sort by numeric key
    valid_pairs = sorted(
        [(int(k), v) for k, v in enum_values.items() if v != 'Request Consumed'],
        key=lambda x: x[0]
    )
    if not valid_pairs:
        return ('[]', '', '')

    hex_chars = data_size * 2

    # Build value_template: map hex string -> option name
    hex_to_name = ', '.join(
        f"'{k:0{hex_chars}x}': '{_jinja2_escape(v)}'" for k, v in valid_pairs
    )
    value_template = f"{{{{ {{{hex_to_name}}}.get(value[:{hex_chars}], 'Unknown') }}}}"

    # Build command_template: map option name -> hex string
    name_to_hex = ', '.join(
        f"'{_jinja2_escape(v)}': '{k:0{hex_chars}x}'" for k, v in valid_pairs
    )
    command_template = f"{{{{ {{{name_to_hex}}}[value] }}}}"

    # Build options JSON array
    option_names = [v for _, v in valid_pairs]
    options_json = '[' + ', '.join(f'"{name}"' for name in option_names) + ']'

    return (options_json, value_template, command_template)


def _enum_sensor_value_template(enum_values: Dict[str, str], field_size: int) -> str:
    """Build value_template for a read-only enum sensor.

    Maps hex byte values to their human-readable label.  Works the same as the
    select value_template but without options or command_template.
    'Request Consumed' (255) is excluded.
    Falls back to showing the raw first-byte hex string when no valid mappings
    exist.
    """
    valid_pairs = sorted(
        [(int(k), v) for k, v in enum_values.items() if v != 'Request Consumed'],
        key=lambda x: x[0]
    )
    if not valid_pairs:
        return '{{ value[:2] }}'

    hex_chars = field_size * 2
    hex_to_name = ', '.join(
        f"'{k:0{hex_chars}x}': '{_jinja2_escape(v)}'" for k, v in valid_pairs
    )
    return f"{{{{ {{{hex_to_name}}}.get(value[:{hex_chars}], 'Unknown') }}}}"


def _strip_pair_role_word(name: str) -> str:
    """Remove trailing or standalone 'Status'/'Request' words from a paired-ERD name.

    Examples:
        'Fan Configuration in Cooling Status'  -> 'Fan Configuration in Cooling'
        'Freeze Sentinel Request'               -> 'Freeze Sentinel'
    """
    # Strip the word wherever it appears as a complete word (word boundaries)
    result = re.sub(r'\b(?:Status|Request)\b', '', name, flags=re.IGNORECASE)
    # Collapse multiple spaces and strip surrounding whitespace
    result = re.sub(r'\s+', ' ', result).strip()
    return result


def _number_command_template(data_size: int, scaling_factor: int, signed: bool = False) -> str:
    """Return command_template for a number entity.

    When ``signed`` is True a modulo operation is applied so that negative
    values are converted to their two's-complement unsigned hex representation
    (e.g. -1 for an int16 becomes 'ffff').  Modulo is used instead of a
    bitwise-AND mask because Jinja2 does not support the ``&`` operator.
    """
    hex_chars = data_size * 2
    if signed:
        max_val = 1 << (data_size * 8)
        if scaling_factor > 1:
            return f"{{{{ '%0{hex_chars}x' % ((((value | float) * {scaling_factor}) | round | int) % {max_val}) }}}}"
        return f"{{{{ '%0{hex_chars}x' % ((value | int) % {max_val}) }}}}"
    if scaling_factor > 1:
        return f"{{{{ '%0{hex_chars}x' % (((value | float) * {scaling_factor}) | round | int) }}}}"
    return f"{{{{ '%0{hex_chars}x' % (value | int) }}}}"


# ---------------------------------------------------------------------------
# Shared helpers for HA-discovery data collection
# ---------------------------------------------------------------------------

def _get_primary_field(erd_by_id: Dict[str, Dict], paired_erd_str: str):
    """Return the first non-reserved, non-bitfield data field of the paired ERD.

    Returns None if the paired ERD is not found or has no suitable field.
    """
    if not paired_erd_str or paired_erd_str not in erd_by_id:
        return None
    erd_data = erd_by_id[paired_erd_str].get('data', [])
    for d in erd_data:
        if not _is_reserved_field(d.get('name', '')) and not _has_bits(d):
            return d
    return None


def _deduplicate_field_ids(entries: List[Dict]) -> None:
    """Ensure field_id is unique within each ERD by appending byte offset on collision.

    When _leaf_field_name strips a disambiguating prefix (e.g. 'Index 0' vs 'Index 1')
    or when field_id_buf truncation causes collisions, this pass appends the byte
    offset to the field_id to make it unique within its ERD.
    """
    by_erd: Dict[int, List[Dict]] = {}
    for entry in entries:
        by_erd.setdefault(entry['erd_id'], []).append(entry)

    for erd_id, group in by_erd.items():
        # Track all field_ids already claimed (original or renamed)
        claimed: Dict[str, Dict] = {}
        for entry in group:
            fid = entry.get('field_id', '')
            if not fid:
                continue
            if fid in claimed:
                # Collision — extract byte offset from value_template for disambiguation
                vt = entry.get('value_template', '')
                m = re.search(r'value\[(\d+):(\d+)\]', vt)
                if m:
                    offset = int(m.group(1))
                else:
                    # For entries without value_template (e.g. buttons, enum options),
                    # use a collision counter to guarantee uniqueness within the ERD.
                    counter_key = f'__dedup_counter__{fid}'
                    counter = claimed.get(counter_key, 0) + 1
                    claimed[counter_key] = counter
                    offset = counter
                new_fid = f'{fid}_{offset}'
                # Guard against the new id also colliding with another claimed id
                suffix = 0
                while new_fid in claimed:
                    suffix += 1
                    new_fid = f'{fid}_{offset}_{suffix}'
                entry['field_id'] = new_fid
                claimed[new_fid] = entry
            else:
                claimed[fid] = entry


def _collect_ha_discovery_entries(erds: List[Dict]) -> List[Dict]:
    """Process all ERDs with ha_domain metadata and return a list of entry dicts.

    Each dict has the keys: erd_id, name, domain, unit, device_class,
    state_class, scaling_factor, data_size, paired_erd_id, pair_role,
    value_template, command_template, options_json, field_id.

    This is the single source of truth for ha-discovery data; both the C header
    generator and the JSONL generator call this function.
    """
    erd_by_id: Dict[str, Dict] = {e['id']: e for e in erds}
    ha_erds = [e for e in erds if 'ha_domain' in e]
    entries: List[Dict] = []

    def collect(erd_id_int: int, name: str, domain: str, unit: str,
                dev_cls: str, state_cls: str, scaling: int, d_size: int,
                paired_id: int, role: str, val_tmpl: str, cmd_tmpl: str,
                opts: str, field_id: str, mode: str = '',
                payload_on: str = '', payload_off: str = '',
                state_on: str = '', state_off: str = '',
                min_val: float = 0.0, max_val: float = 0.0, step_val: float = 1.0) -> None:
        # Skip availability/allowability metadata — not actionable in HA.
        combined = (name + ' ' + field_id).lower()
        if 'allowed' in combined or 'available' in combined:
            return
        entries.append({
            'erd_id': erd_id_int,
            'name': name,
            'domain': domain,
            'unit': unit,
            'device_class': dev_cls,
            'state_class': state_cls,
            'scaling_factor': scaling,
            'data_size': d_size,
            'paired_erd_id': paired_id,
            'pair_role': role,
            'value_template': val_tmpl,
            'command_template': cmd_tmpl,
            'options_json': opts,
            'field_id': field_id,
            'mode': mode,
            'payload_on': payload_on,
            'payload_off': payload_off,
            'state_on': state_on,
            'state_off': state_off,
            'min_val': min_val,
            'max_val': max_val,
            'step_val': step_val,
        })

    processed_status = set()

    for erd in ha_erds:
        erd_id_int = parse_erd_id(erd['id'])
        name = erd.get('name', '')
        ha_domain = erd.get('ha_domain', '')
        unit = _unit_to_ha(erd.get('unit_of_measurement') or '')
        device_class = erd.get('device_class') or ''
        state_class = erd.get('state_class') or ''
        scaling_factor = int(erd.get('scaling_factor') or 1)
        pair_role = erd.get('pair_role') or ''
        paired_erd_str = erd.get('paired_erd') or ''
        paired_erd_id = parse_erd_id(paired_erd_str) if paired_erd_str else 0
        display_name = _strip_pair_role_word(name) if pair_role else name
        erd_data = erd.get('data', [])
        data_size = get_erd_byte_size(erd_data) or 1

        # Skip "Request" ERDs that lack a proper request/status pair.
        # Only skip when the ERD is actually part of a request/status pair
        # (pair_role == 'request') but has no valid paired_erd.
        # Standalone ERDs with "Request" in their name (e.g., button commands)
        # should NOT be skipped — they are legitimate write-only entities.
        if pair_role == 'request' and 'Request' in name:
            if not (paired_erd_str and paired_erd_str in erd_by_id):
                continue

        # Skip status ERD if its paired request ERD is a controllable domain
        # (switch/select/number) — the request ERD will handle both state+command.
        # Verify bidirectional pairing: the request ERD's paired_erd must point
        # back to this status ERD, otherwise the pairing is asymmetric and the
        # status ERD carries independent information (e.g., "Not Equipped").
        if pair_role == 'status' and paired_erd_str and paired_erd_str in erd_by_id:
            paired = erd_by_id[paired_erd_str]
            paired_role = paired.get('pair_role') or ''
            paired_domain = paired.get('ha_domain') or ''
            paired_back = paired.get('paired_erd') or ''
            if (paired_role == 'request'
                    and paired_domain in ('switch', 'select', 'number')
                    and paired_back == erd['id']):
                processed_status.add(erd_id_int)
                continue

        classification = (
            'single'
            if ha_domain in ('select', 'number', 'button', 'switch')
            else _classify_erd_data(erd_data)
        )

        if classification == 'single':
            vt, ct, opts = '', '', ''
            min_val, max_val, step_val = 0.0, 0.0, 1.0

            if ha_domain == 'sensor':
                # Detect enum from either device_class or data field type.
                primary_type = _get_primary_data_type(erd_data)
                if device_class == 'enum' or primary_type == 'enum':
                    ev, fs = _get_first_enum_field_info(erd_data)
                    vt = _enum_sensor_value_template(ev, fs)
                elif primary_type == 'string':
                    # String-type ERDs: MQTT payload is hex. Convert each byte
                    # pair to plain ASCII, stripping trailing '_' padding.
                    vt = _string_value_template(data_size)
                elif data_size <= 4:
                    signed = _is_signed_type(_get_primary_data_type(erd_data))
                    vt = _compute_sensor_value_template(scaling_factor, data_size, signed)
            elif ha_domain == 'binary_sensor':
                vt = _compute_binary_sensor_value_template(data_size)
            elif ha_domain == 'switch':
                # For paired switches, read state from the status ERD's primary field.
                vt = _paired_primary_field_template(erd_by_id, paired_erd_str, 1, True) or ''
            elif ha_domain == 'select':
                ev, fs = _get_first_enum_field_info(erd_data)
                if ev:
                    opts, vt, ct = _select_options_and_templates(ev, fs)
                else:
                    # No enum values to populate options; skip rather than emit
                    # a broken select entity.
                    continue
            elif ha_domain == 'number':
                pf = _get_primary_field(erd_by_id, paired_erd_str)
                if pf:
                    p_scale = int(erd_by_id[paired_erd_str].get('scaling_factor') or 1)
                    vt = _byte_subfield_value_template(pf, p_scale)
                    signed = _is_signed_type(pf.get('type', 'u8'))
                elif paired_erd_str and paired_erd_str in erd_by_id:
                    p_scale = int(erd_by_id[paired_erd_str].get('scaling_factor') or 1)
                    paired_type = _get_primary_data_type(erd_by_id[paired_erd_str].get('data', []))
                    signed = _is_signed_type(paired_type)
                    vt = _compute_sensor_value_template(p_scale, data_size, signed)
                else:
                    signed = _is_signed_type(_get_primary_data_type(erd_data))
                    vt = _compute_sensor_value_template(scaling_factor, data_size, signed)
                ct = _number_command_template(data_size, scaling_factor, signed)
                # Compute min/max/step for number entities
                if pf:
                    n_type = pf.get('type', 'u8')
                    n_scale = int(erd_by_id[paired_erd_str].get('scaling_factor') or 1)
                elif paired_erd_str and paired_erd_str in erd_by_id:
                    n_type = _get_primary_data_type(erd_by_id[paired_erd_str].get('data', []))
                    n_scale = int(erd_by_id[paired_erd_str].get('scaling_factor') or 1)
                else:
                    n_type = _get_primary_data_type(erd_data)
                    n_scale = scaling_factor
                min_val, max_val, step_val = _compute_number_range(n_type, n_scale)
            # button: no templates

            # For switch/binary_sensor, set payload_on/off and state_on/off to hex
            p_on = ''
            p_off = ''
            s_on = ''
            s_off = ''
            if ha_domain in ('switch', 'binary_sensor'):
                p_on = '01'
                p_off = '00'
                s_on = '01'
                s_off = '00'
            collect(erd_id_int, display_name, ha_domain, unit, device_class,
                    state_class, scaling_factor, data_size, paired_erd_id,
                    pair_role, vt, ct, opts, '',
                    'box' if ha_domain == 'number' else '',
                    p_on, p_off, s_on, s_off,
                    min_val, max_val, step_val)

        elif classification == 'byte_offset':
            nr_fields = _get_non_reserved_fields(erd_data)
            for idx, field in enumerate(nr_fields):
                leaf = _leaf_field_name(field.get('name', ''))
                entity_name = (leaf if leaf.lower().startswith(display_name.lower())
                               else f'{display_name} - {leaf}')
                fid = '' if idx == 0 else _field_slug(leaf)
                f_type = field.get('type', '')
                f_dev_cls = 'enum' if f_type == 'enum' else (device_class if idx == 0 else '')
                f_state_cls = state_class if idx == 0 else ''
                f_unit = _infer_unit_from_field_name(leaf, unit)
                if ha_domain == 'binary_sensor' and f_type == 'enum':
                    f_dev_cls = ''
                    vt = _compute_binary_sensor_value_template(data_size)
                else:
                    vt = _byte_subfield_value_template(field, scaling_factor)
                collect(erd_id_int, entity_name, ha_domain, f_unit, f_dev_cls,
                        f_state_cls, scaling_factor, data_size, paired_erd_id,
                        pair_role, vt, '', '', fid, '', '', '', '', '')

        elif classification == 'bitfield':
            for field in _get_non_reserved_fields(erd_data):
                leaf = _leaf_field_name(field.get('name', ''))
                fid = _field_slug(leaf)
                bits_size = field.get('bits', {}).get('size', 1)
                sub_domain = 'binary_sensor' if bits_size == 1 else 'sensor'
                vt = _bitfield_sub_value_template(field)
                b_p_on = '01' if sub_domain == 'binary_sensor' else ''
                b_p_off = '00' if sub_domain == 'binary_sensor' else ''
                b_s_on = '01' if sub_domain == 'binary_sensor' else ''
                b_s_off = '00' if sub_domain == 'binary_sensor' else ''
                collect(erd_id_int, f'{display_name} - {leaf}', sub_domain, '', '',
                        '', scaling_factor, data_size, paired_erd_id, pair_role,
                        vt, '', '', fid, '', b_p_on, b_p_off, b_s_on, b_s_off)

        elif classification == 'mixed':
            primary = next(
                (d for d in erd_data
                 if not _has_bits(d) and not _is_reserved_field(d.get('name', ''))),
                None
            )
            if primary:
                p_type = primary.get('type', '')
                p_dev_cls = 'enum' if p_type == 'enum' else device_class
                if ha_domain == 'binary_sensor' and p_type == 'enum':
                    # binary_sensor can't display enum labels; use ON/OFF
                    p_dev_cls = ''
                    p_vt = _compute_binary_sensor_value_template(data_size)
                else:
                    p_vt = _byte_subfield_value_template(primary, scaling_factor)
                collect(erd_id_int, display_name, ha_domain, unit, p_dev_cls,
                        state_class, scaling_factor, data_size, paired_erd_id,
                        pair_role, p_vt, '', '', '', '', '', '', '', '')

            for field in [d for d in erd_data
                          if _has_bits(d) and not _is_reserved_field(d.get('name', ''))]:
                leaf = _leaf_field_name(field.get('name', ''))
                fid = _field_slug(leaf)
                bits_size = field.get('bits', {}).get('size', 1)
                sub_domain = 'binary_sensor' if bits_size == 1 else 'sensor'
                vt = _bitfield_sub_value_template(field)
                b_p_on = '01' if sub_domain == 'binary_sensor' else ''
                b_p_off = '00' if sub_domain == 'binary_sensor' else ''
                b_s_on = '01' if sub_domain == 'binary_sensor' else ''
                b_s_off = '00' if sub_domain == 'binary_sensor' else ''
                collect(erd_id_int, f'{display_name} - {leaf}', sub_domain, '', '',
                        '', scaling_factor, data_size, paired_erd_id, pair_role,
                        vt, '', '', fid, '', b_p_on, b_p_off, b_s_on, b_s_off)

        elif classification == 'version':
            nr_fields = _get_non_reserved_fields(erd_data)
            vt = _version_value_template(nr_fields)
            collect(erd_id_int, display_name, ha_domain, unit, device_class,
                    state_class, scaling_factor, data_size, paired_erd_id,
                    pair_role, vt, '', '', '', '', '', '', '', '')

        elif classification == 'multi_board_version':
            groups = _group_multi_board_version_fields(erd_data)
            for group in groups:
                board = group['prefix']
                entity_name = f'{display_name} - {board} Version' if board else display_name
                vt = _version_value_template(group['version_fields'])
                fid = _field_slug(board) if board else ''
                collect(erd_id_int, entity_name, ha_domain, unit, device_class,
                        state_class, scaling_factor, data_size, paired_erd_id,
                        pair_role, vt, '', '', fid, '', '', '', '', '')

                # Add parametric version if both fields present
                param_fields = group['parametric_fields']
                if param_fields[0] is not None and param_fields[1] is not None:
                    param_name = f'{display_name} - {board} Parametric Version' if board else f'{display_name} - Parametric Version'
                    p_vt = _parametric_version_value_template(param_fields)
                    if p_vt:
                        param_fid = _field_slug(board + '_parametric') if board else 'parametric'
                        collect(erd_id_int, param_name, ha_domain, unit, device_class,
                                state_class, scaling_factor, data_size, paired_erd_id,
                                pair_role, p_vt, '', '', param_fid, '', '', '', '', '')
    _deduplicate_field_ids(entries)
    return entries


# ---------------------------------------------------------------------------
# Config topic filtering
# ---------------------------------------------------------------------------

# Compiled regex patterns for filtering out entities that are internal
# metadata, diagnostics, or commissioning state — not useful to end users.
# Each tuple is (category_name, compiled_regex).
_FILTER_PATTERNS = [
    # OS/board-level diagnostics (RAM, disk, packet stats, uptime).
    # Never useful to end users.
    ("diagnostics", re.compile(
        r"(?i)(linux diagnostics|GEA.*interface diagnostic|non-volatile usage warning|reset reason|seconds since last reset|program counter.*failed assertion|fault code)"
    )),
    # Internal firmware metadata: config hashes, SHA-256 schedule hashes,
    # boot loader versions, supported image types.
    ("firmware", re.compile(
        r"(?i)(configuration hash|schedule hash|SHA-256|boot loader version|supported image types|ready to enter boot|engineering revision setup)"
    )),
    # CSM (Control State Machine) fault data. Internal diagnostics.
    ("csm_fault", re.compile(
        r"(?i)csm fault data"
    )),
    # Matter/Alexa one-time commissioning state. Not useful after setup.
    ("commissioning", re.compile(
        r"(?i)(Alexa.*registration|Alexa.*status|Matter.*device|Matter.*commissioning|Matter.*onboarding|Matter.*product ID|Matter.*temperature display|Matter.*keypad lockout|voice module)"
    )),
    # Mobile app push notification flags. Irrelevant in HA context.
    ("push_notifications", re.compile(
        r"(?i)(push notification)"
    )),
    # Min/max bounds for settings. Used internally; redundant in HA where
    # number/slider controls handle bounds.
    ("limits", re.compile(
        r"(?i)(limit|min.*max|allowable.*range|range data|expiration limit|target temperature range)"
    )),
    # Metadata about which settings can be changed. Not actionable.
    ("availability", re.compile(
        r"(?i)(modification available|action available|editable|available.*mode|action availability|available.*setting|availability)"
    )),
    # Feature capability flags. Static metadata.
    ("supported_features", re.compile(
        r"(?i)(supported.*feature|supported.*state|supported.*equipment|supported.*sound theme|supported.*enhanced|supported.*notification|supported.*setting|supported.*device)"
    )),
    # Request-side mirrors of status ERDs. The status ERD handles both
    # read+write via pairing.
    ("request_parameters", re.compile(
        r"(?i)(requested.*parameter|request.*setting|request.*mask|request.*configuration)"
    )),
    # Appliance clock. Redundant with system time.
    ("clock", re.compile(
        r"(?i)(clock time|NTP|time zone|daylight saving|calendar)"
    )),
    # Network diagnostics. Redundant with router info.
    ("network", re.compile(
        r"(?i)(WiFi.*status|network.*status|signal.*strength|BLE.*master|Bluetooth.*master)"
    )),
    # Utility pricing schedule internals. Rarely useful to end users.
    ("energy_pricing", re.compile(
        r"(?i)(electrical.*pricing|demand response|time of use.*pricing|pricing.*structure)"
    )),
    # Camera/image capture. Specialized, not for most users.
    ("camera", re.compile(
        r"(?i)(still frame|image upload|camera.*configuration|camera.*stream|inference ID|cook cam.*upload)"
    )),
    # Sound/beep configuration. Niche preference.
    ("sound", re.compile(
        r"(?i)(sound level|sound theme|available sound|number of sound level)"
    )),
    # GE's proprietary cloud feature deployment. Irrelevant for local HA.
    ("enhanced_cloud", re.compile(
        r"(?i)(enhanced feature|CEC|core-enhanced-cloud|request enabled enhanced|current enabled enhanced)"
    )),
    # Usage profile data. Internal telemetry, not actionable.
    ("usage_profile", re.compile(
        r"(?i)usage profile"
    )),
    # Current report data (AC, inverter). Internal diagnostics.
    ("current_report", re.compile(
        r"(?i)current report"
    )),
    # Feature configuration. Internal metadata.
    ("feature_configuration", re.compile(
        r"(?i)feature configuration"
    )),
    # Cycle definitions. Internal program metadata.
    ("cycle_definition", re.compile(
        r"(?i)cycle definition"
    )),
    # Latched key status. Internal keypad state.
    ("latched_key", re.compile(
        r"(?i)latched key status"
    )),
    # DIP switch. Hardware configuration, not user-facing.
    ("dip_switch", re.compile(
        r"(?i)dip switch"
    )),
    # Most recent cycle status. Historical data, not actionable.
    ("most_recent_cycle", re.compile(
        r"(?i)most recent cycle status"
    )),
    # Unused/reserved fields. Placeholder data, never meaningful.
    ("unused_reserved", re.compile(
        r"(?i)(unused|reserved)(\s*\[.*\])?"
    )),
    # Service mode. Internal technician state, not useful to end users.
    ("service_mode", re.compile(
        r"(?i)service mode"
    )),
    # Issue/fault/diagnostic/failure indicators. Operational error state,
    # not actionable in HA (appliance handles these internally).
    ("operational_errors", re.compile(
        r"(?i)(issue|\bfault\b|\bfaulted\b|diagnostic|failure)"
    )),

]


def _should_filter_entity(name: str) -> bool:
    """Return True if the entity should be filtered out when
    filter_config_topics is enabled.

    Checks the entity name against the compiled filter patterns.
    """
    for _, pattern in _FILTER_PATTERNS:
        if pattern.search(name):
            return True
    return False
# ---------------------------------------------------------------------------
# JSONL generation
# ---------------------------------------------------------------------------

def generate_ha_discovery_jsonl_by_category(erds: List[Dict], filter_config_topics: bool = True) -> Dict[str, str]:
    """Generate compact JSONL content grouped by appliance category.

    Returns a dict mapping category name -> JSONL string content.
    Each JSONL line is a compact JSON object with the pre-computed ha-discovery
    fields for one entity.  Fields that equal their default value are omitted to
    reduce file size.

    When filter_config_topics is True, entities matching internal metadata,
    diagnostics, commissioning, and other non-user-facing patterns are excluded.
    """
    entries = _collect_ha_discovery_entries(erds)

    if filter_config_topics:
        filtered = []
        filtered_count = 0
        for entry in entries:
            if _should_filter_entity(entry['name']):
                filtered_count += 1
            else:
                filtered.append(entry)
        entries = filtered
        if filtered_count:
            print(f"  Filtered out {filtered_count} entities (filter_config_topics=True)", file=sys.stderr)

    categorized: Dict[str, list] = {cat: [] for cat in CATEGORIES}
    for entry in entries:
        eid = entry['erd_id']
        for cat, (lo, hi) in CATEGORIES.items():
            if lo <= eid <= hi:
                categorized[cat].append(entry)
                break

    result: Dict[str, str] = {}
    for cat in CATEGORIES:
        cat_entries = categorized[cat]
        if not cat_entries:
            continue
        lines = []
        for e in cat_entries:
            obj: Dict[str, Any] = {
                'i': f'{e["erd_id"]:04x}',
                'n': e['name'],
                'd': e['domain'],
                'ds': e['data_size'],
            }
            # Omit fields that equal their defaults to save space
            if e['unit']:                         obj['u']  = e['unit']
            if e['device_class'] and _is_valid_device_class(e['domain'], e['device_class']):
                obj['dc'] = e['device_class']
            if e['state_class']:                  obj['sc'] = e['state_class']
            if e['scaling_factor'] != 1:          obj['sf'] = e['scaling_factor']
            if e['domain'] == 'number' and e.get('min_val') is not None:      obj['mn'] = e['min_val']
            if e['domain'] == 'number' and e.get('max_val') is not None:      obj['mx'] = e['max_val']
            if e['domain'] == 'number' and e.get('step_val') is not None:     obj['st'] = e['step_val']
            if e['paired_erd_id']:                obj['p']  = f'{e["paired_erd_id"]:04x}'
            if e['pair_role']:                    obj['r']  = e['pair_role']
            if e['value_template']:               obj['vt'] = e['value_template']
            if e['command_template']:             obj['ct'] = e['command_template']
            if e['options_json']:                 obj['o']  = e['options_json']
            if e['field_id']:                     obj['fi'] = e['field_id']
            if e['mode']:                         obj['m']  = e['mode']
            if e['payload_on']:                   obj['pon'] = e['payload_on']
            if e['payload_off']:                  obj['poff'] = e['payload_off']
            if e['state_on']:                     obj['son'] = e['state_on']
            if e['state_off']:                    obj['soff'] = e['state_off']
            lines.append(json.dumps(obj, ensure_ascii=False, separators=(',', ':')))
        result[cat] = '\n'.join(lines) + '\n'

    return result


# ---------------------------------------------------------------------------
# File discovery and main
# ---------------------------------------------------------------------------

def find_erd_definitions_json() -> Optional[Path]:
    """Find the ERD definitions JSON using multiple search paths.

    Mirrors the search strategy in __init__.py::load_appliance_types().
    Returns the path if found, or None.
    """
    json_filename = "appliance_api_erd_definitions.json"
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    seen_paths = set()

    search_paths = [
        # Local submodule (for development with checked out repo)
        repo_root / 'lib' / 'public-appliance-api-documentation' / json_filename,
        # ESPHome library cache in user's home directory
        Path.home() / '.esphome' / 'external_files' / 'libraries' / 'public-appliance-api-documentation' / json_filename,
        # ESPHome library cache in /config (Home Assistant add-on)
        Path('/config/.esphome/external_files/libraries/public-appliance-api-documentation/' + json_filename),
        # ESPHome library cache relative to component (build directory)
        repo_root / '.esphome' / 'external_files' / 'libraries' / 'public-appliance-api-documentation' / json_filename,
        # Parent library path (external_components layout)
        repo_root / 'lib' / 'public-appliance-api-documentation' / json_filename,
    ]

    for p in search_paths:
        norm = str(p.resolve())
        if norm in seen_paths:
            continue
        seen_paths.add(norm)
        if p.exists():
            return p

    return None


def fetch_erd_definitions_from_github() -> Optional[dict]:
    """Fetch ERD definitions from GitHub as fallback."""
    import urllib.request as urllib
    url = "https://raw.githubusercontent.com/eddietheengineer/public-appliance-api-documentation/main/appliance_api_erd_definitions.json"
    print(f"Fetching ERD definitions from GitHub: {url}", file=sys.stderr)
    try:
        with urllib.urlopen(url, timeout=10) as response:
            return json.loads(response.read().decode('utf-8'))
    except Exception as e:
        print(f"Failed to fetch from GitHub: {e}", file=sys.stderr)
        return None


def main():
    """Main entry point for the script."""
    import argparse
    parser = argparse.ArgumentParser(description="Generate HA discovery JSONL files.")
    parser.add_argument("--filter-config-topics", action="store_true", default=True,
                        help="Filter out internal metadata, diagnostics, and commissioning entities (default: true).")
    parser.add_argument("--no-filter-config-topics", action="store_false", dest="filter_config_topics",
                        help="Disable filtering of internal/diagnostic entities.")
    parser.add_argument("--erd-definitions", default=None,
                        help="Path to appliance_api_erd_definitions.json (bypasses auto-search).")
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    output_dir = repo_root / 'ha_discovery'

    # Try to find the JSON file locally
    json_file = None
    if args.erd_definitions and os.path.exists(args.erd_definitions):
        json_file = Path(args.erd_definitions)
    else:
        json_file = find_erd_definitions_json()
    data = None

    if json_file is not None:
        print(f"Reading ERD definitions from {json_file}", file=sys.stderr)
        try:
            with open(json_file, 'r') as f:
                data = json.load(f)
        except Exception as e:
            print(f"Failed to read {json_file}: {e}", file=sys.stderr)
    else:
        print("Local ERD definitions not found, trying GitHub fallback...", file=sys.stderr)

    # Fallback to GitHub
    if data is None:
        data = fetch_erd_definitions_from_github()
        if data is None:
            print("Error: Could not find appliance_api_erd_definitions.json", file=sys.stderr)
            print("Tried local paths and GitHub. Check your network and git submodules.", file=sys.stderr)
            sys.exit(1)

    erds = data.get('erds', [])
    print(f"Found {len(erds)} ERD definitions", file=sys.stderr)

    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    # Generate JSONL by category
    jsonl_by_cat = generate_ha_discovery_jsonl_by_category(erds, args.filter_config_topics)
    total_entries = 0
    for cat, content in jsonl_by_cat.items():
        outfile = output_dir / f'{cat}.jsonl'
        with open(outfile, 'w', encoding='utf-8') as f:
            f.write(content)
        n = content.count('\n')
        total_entries += n
        print(f"  {cat}: {n} entities -> {cat}.jsonl ({len(content):,} bytes)", file=sys.stderr)

    print(f"\nTotal entities generated: {total_entries}", file=sys.stderr)
    print(f"Output directory: {output_dir}", file=sys.stderr)
    print("Done!", file=sys.stderr)
if __name__ == '__main__':
    main()
