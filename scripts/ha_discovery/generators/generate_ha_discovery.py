#!/usr/bin/env python3
"""
Script to generate Home Assistant MQTT Discovery JSONL files from the
public-appliance-api-documentation ERD definitions.

Reads appliance_api_erd_definitions_processed.json (flattened, one entry per
sub-field) and produces category-specific JSONL files in ha_discovery/, each
line being a compact JSON object defining one HA entity.

Uses field type information from the processed data to generate proper
value/command templates:
  - Signed integer types (i8, i16, i32) get two's-complement conversion
  - Scaling factors are applied with proper decimal places
  - Enum types get proper hex-to-label mapping
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


def _unit_to_ha(unit: str) -> str:
    """Convert an API unit string to the Home Assistant display unit."""
    return {'degF': '\u00b0F', 'degC': '\u00b0C'}.get(unit, unit)


# ---------------------------------------------------------------------------
# Value template generators
# ---------------------------------------------------------------------------

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
        f"'{k:0{hex_chars}x}': '{v.replace(chr(39), chr(92)+chr(39))}'" for k, v in valid_pairs
    )
    return f"{{{{ {{{hex_to_name}}}.get(value[:{hex_chars}], 'Unknown') }}}}"


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
        f"'{k:0{hex_chars}x}': '{v.replace(chr(39), chr(92)+chr(39))}'" for k, v in valid_pairs
    )
    value_template = f"{{{{ {{{hex_to_name}}}.get(value[:{hex_chars}], 'Unknown') }}}}"

    # Build command_template: map option name -> hex string
    name_to_hex = ', '.join(
        f"'{v.replace(chr(39), chr(92)+chr(39))}': '{k:0{hex_chars}x}'" for k, v in valid_pairs
    )
    command_template = f"{{{{ {{{name_to_hex}}}[value] }}}}"

    # Build options JSON array
    option_names = [v for _, v in valid_pairs]
    options_json = '[' + ', '.join(f'"{name}"' for name in option_names) + ']'

    return (options_json, value_template, command_template)


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


# ---------------------------------------------------------------------------
# Shared helpers for HA-discovery data collection
# ---------------------------------------------------------------------------

def _deduplicate_field_ids(entries: List[Dict]) -> None:
    """Ensure field_id is unique within each ERD by appending byte offset on collision.

    When _field_slug truncation causes collisions, this pass appends the byte
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


def _collect_ha_discovery_entries(entries: List[Dict]) -> List[Dict]:
    """Process all flattened entries with ha_domain metadata and return a list of HA entry dicts.

    Each dict has the keys: erd_id, name, domain, unit, device_class,
    state_class, scaling_factor, data_size, paired_erd_id, pair_role,
    value_template, command_template, options_json, field_id.

    Input entries are from appliance_api_erd_definitions_processed.json,
    one entry per sub-field with review{} containing HA metadata.
    """
    # Build lookup by erd_id for pairing lookups
    by_erd_id: Dict[str, List[Dict]] = {}
    for entry in entries:
        by_erd_id.setdefault(entry['erd_id'], []).append(entry)

    results: List[Dict] = []

    def collect(erd_id_int: int, name: str, domain: str, unit: str,
                dev_cls: str, state_cls: str, scaling: int, d_size: int,
                paired_id: int, role: str, val_tmpl: str, cmd_tmpl: str,
                opts: str, field_id: str, mode: str = '',
                payload_on: str = '', payload_off: str = '',
                state_on: str = '', state_off: str = '',
                min_val: float = 0.0, max_val: float = 0.0, step_val: float = 1.0) -> None:
        results.append({
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

    for entry in entries:
        review = entry['review']

        # Skip if no ha_domain assigned
        ha_domain = review.get('ha_domain')
        if ha_domain is None:
            continue

        # Skip if explicitly filtered in review
        if review.get('filtered'):
            continue

        # Skip availability/allowability metadata — not actionable in HA.
        field_name = entry.get('field_name', '')
        combined = (field_name).lower()
        if 'allowed' in combined or 'available' in combined:
            continue

        erd_id_str = entry['erd_id']
        erd_id_int = parse_erd_id(erd_id_str)
        erd_name = entry.get('erd_name', '')
        field_type = entry.get('field_type', 'u8')
        field_offset = entry.get('field_offset', 0)
        field_size = entry.get('field_size', 1)
        field_values = entry.get('field_values')  # enum values dict or None
        field_bits = entry.get('field_bits')  # bitfield info dict or None

        # Review metadata
        device_class = review.get('device_class') or ''
        unit_of_measurement = review.get('unit_of_measurement') or ''
        scaling_factor = int(review.get('scaling_factor') or 1)
        state_class = review.get('state_class') or ''
        pair_role = review.get('pair_role') or ''
        paired_erd_str = review.get('paired_erd') or ''
        paired_erd_id = parse_erd_id(paired_erd_str) if paired_erd_str else 0

        # Compute data_size: for a flattened entry, use the ERD's total size.
        # We need to find the max (offset + size) across all entries with the same erd_id.
        # For simplicity, use the field's own offset + size as the data_size (the MQTT
        # payload covers the whole ERD). We'll compute the actual ERD size from siblings.
        erd_entries = by_erd_id.get(erd_id_str, [entry])
        data_size = max(e.get('field_offset', 0) + e.get('field_size', 1) for e in erd_entries)
        if data_size == 0:
            data_size = 1

        unit = _unit_to_ha(unit_of_measurement) if unit_of_measurement else ''

        # Skip unpaired Request ERDs (request role with no valid paired ERD)
        if pair_role == 'request':
            if not (paired_erd_str and paired_erd_str in by_erd_id):
                continue

        # Skip status ERD if its paired request ERD is a controllable domain
        # (switch/select/number) — the request ERD will handle both state+command.
        if pair_role == 'status' and paired_erd_str and paired_erd_str in by_erd_id:
            paired_entries = by_erd_id[paired_erd_str]
            for paired_entry in paired_entries:
                paired_role = paired_entry['review'].get('pair_role') or ''
                paired_domain = paired_entry['review'].get('ha_domain') or ''
                paired_back = paired_entry['review'].get('paired_erd') or ''
                if (paired_role == 'request'
                        and paired_domain in ('switch', 'select', 'number')
                        and paired_back == erd_id_str):
                    continue  # skip this status entry

        # Build display name: use field_name for the entity name
        # For paired entries, strip "Status"/"Request" words from the name
        if pair_role:
            display_name = _strip_pair_role_word(field_name)
        else:
            display_name = field_name

        # Build field_id for sub-fields within a multi-field ERD
        # When there are multiple entries for the same ERD, each gets a field_id
        # except the primary (offset 0) which has no field_id.
        is_primary = (field_offset == 0 and len(erd_entries) == 1)
        field_id = '' if is_primary else _field_slug(field_name)

        # Generate templates based on domain and field type
        vt, ct, opts = '', '', ''
        min_val, max_val, step_val = 0.0, 0.0, 1.0
        p_on, p_off, s_on, s_off = '', '', '', ''

        if ha_domain == 'sensor':
            if field_type == 'enum' and field_values:
                vt = _enum_sensor_value_template(field_values, field_size)
                device_class = 'enum'
            elif field_type == 'string':
                vt = _string_value_template(field_size)
            elif field_bits:
                # Bitfield sub-field: generate inline template
                bits = field_bits
                bit_offset = bits.get('offset', 0)
                bit_size = bits.get('size', 1)
                hex_start = field_offset * 2
                hex_end = (field_offset + field_size) * 2
                if bit_size == 1:
                    divisor = 2 ** bit_offset
                    vt = (f"{{{{ '01' if ((value[{hex_start}:{hex_end}] | int(base=16))"
                            f" // {divisor}) % 2 else '00' }}}}")
                    ha_domain = 'binary_sensor'
                    p_on, p_off = '01', '00'
                    s_on, s_off = '01', '00'
                else:
                    divisor = 2 ** bit_offset
                    modulus = 1 << bit_size
                    vt = (f"{{{{ ((value[{hex_start}:{hex_end}] | int(base=16))"
                            f" // {divisor}) % {modulus} }}}}")
            else:
                signed = bool(re.match(r'^i\d+$', field_type))
                vt = _compute_sensor_value_template(scaling_factor, field_size, signed)

        elif ha_domain == 'binary_sensor':
            if field_bits:
                bits = field_bits
                bit_offset = bits.get('offset', 0)
                bit_size = bits.get('size', 1)
                hex_start = field_offset * 2
                hex_end = (field_offset + field_size) * 2
                if bit_size == 1:
                    divisor = 2 ** bit_offset
                    vt = (f"{{{{ '01' if ((value[{hex_start}:{hex_end}] | int(base=16))"
                            f" // {divisor}) % 2 else '00' }}}}")
                else:
                    divisor = 2 ** bit_offset
                    modulus = 1 << bit_size
                    vt = (f"{{{{ ((value[{hex_start}:{hex_end}] | int(base=16))"
                            f" // {divisor}) % {modulus} }}}}")
            else:
                vt = _compute_binary_sensor_value_template(field_size)
            p_on, p_off = '01', '00'
            s_on, s_off = '01', '00'

        elif ha_domain == 'switch':
            ct = "{{ '01' if value else '00' }}"
            if pair_role == 'request' and paired_erd_str and paired_erd_str in by_erd_id:
                paired_entries_list = by_erd_id[paired_erd_str]
                for pe in paired_entries_list:
                    pe_role = pe['review'].get('pair_role') or ''
                    if pe_role == 'status':
                        pe_offset = pe.get('field_offset', 0)
                        pe_size = pe.get('field_size', 1)
                        pe_type = pe.get('field_type', 'u8')
                        pe_values = pe.get('field_values')
                        pe_bits = pe.get('field_bits')
                        hex_s = pe_offset * 2
                        hex_e = (pe_offset + pe_size) * 2
                        if pe_bits:
                            b_off = pe_bits.get('offset', 0)
                            b_sz = pe_bits.get('size', 1)
                            if b_sz == 1:
                                div = 2 ** b_off
                                vt = (f"{{{{ '01' if ((value[{hex_s}:{hex_e}] | int(base=16))"
                                        f" // {div}) % 2 else '00' }}}}")
                            else:
                                div = 2 ** b_off
                                mod = 1 << b_sz
                                vt = (f"{{{{ ((value[{hex_s}:{hex_e}] | int(base=16))"
                                        f" // {div}) % {mod} }}}}")
                        elif pe_type == 'enum' and pe_values:
                            valid_pairs = sorted(
                                [(int(k), v) for k, v in pe_values.items() if v != 'Request Consumed'],
                                key=lambda x: x[0]
                            )
                            if valid_pairs:
                                hc = pe_size * 2
                                mapping = ', '.join(f"'{k:0{hc}x}': '{v.replace(chr(39), chr(92)+chr(39))}'" for k, v in valid_pairs)
                                vt = f"{{{{ {{{mapping}}}.get(value[{hex_s}:{hex_e}], 'Unknown') }}}}"
                            else:
                                vt = f"{{{{ value[{hex_s}:{hex_e}] }}}}"
                        elif pe_type == 'bool':
                            vt = f"{{{{ '01' if value[{hex_s}:{hex_e}] != '00' else '00' }}}}"
                        else:
                            vt = f"{{{{ value[{hex_s}:{hex_e}] | int(base=16) }}}}"
                        break
            else:
                vt = ''
            p_on, p_off = '01', '00'
            s_on, s_off = '01', '00'

        elif ha_domain == 'select':
            if field_values:
                opts, vt, ct = _select_options_and_templates(field_values, field_size)
            else:
                # No enum values — skip
                continue

        elif ha_domain == 'number':
            signed = bool(re.match(r'^i\d+$', field_type))
            vt = _compute_sensor_value_template(scaling_factor, field_size, signed)
            ct = _number_command_template(field_size, scaling_factor, signed)
            min_val, max_val, step_val = _compute_number_range(field_type, scaling_factor)
            # For paired number entities, read state from the status ERD
            if pair_role == 'request' and paired_erd_str and paired_erd_str in by_erd_id:
                paired_entries_list = by_erd_id[paired_erd_str]
                for pe in paired_entries_list:
                    pe_role = pe['review'].get('pair_role') or ''
                    if pe_role == 'status':
                        pe_offset = pe.get('field_offset', 0)
                        pe_size = pe.get('field_size', 1)
                        pe_type = pe.get('field_type', field_type)
                        pe_sf = int(pe['review'].get('scaling_factor') or scaling_factor)
                        pe_signed = bool(re.match(r'^i\d+$', pe_type))
                        hex_s = pe_offset * 2
                        hex_e = (pe_offset + pe_size) * 2
                        if pe_signed:
                            max_val_t = 2 ** (pe_size * 8)
                            half_val_t = max_val_t // 2
                            if pe_sf > 1:
                                dp = _decimal_places(pe_sf)
                                vt = (f"{{{{ ((value[{hex_s}:{hex_e}] | int(base=16)) - {max_val_t}"
                                        f" if (value[{hex_s}:{hex_e}] | int(base=16)) >= {half_val_t}"
                                        f" else (value[{hex_s}:{hex_e}] | int(base=16)))"
                                        f" / {pe_sf} | round({dp}) }}}}")
                            else:
                                vt = (f"{{{{ (value[{hex_s}:{hex_e}] | int(base=16)) - {max_val_t}"
                                        f" if (value[{hex_s}:{hex_e}] | int(base=16)) >= {half_val_t}"
                                        f" else (value[{hex_s}:{hex_e}] | int(base=16)) }}}}")
                        elif pe_sf > 1:
                            dp = _decimal_places(pe_sf)
                            vt = (f"{{{{ (value[{hex_s}:{hex_e}] | int(base=16))"
                                    f" / {pe_sf} | round({dp}) }}}}")
                        else:
                            vt = f"{{{{ value[{hex_s}:{hex_e}] | int(base=16) }}}}"
                        # Recompute range based on paired field type
                        min_val, max_val, step_val = _compute_number_range(pe_type, pe_sf)
                        break

        elif ha_domain == 'button':
            # Buttons have no value_template; they just send a payload
            pass

        collect(erd_id_int, display_name, ha_domain, unit, device_class,
                state_class, scaling_factor, data_size, paired_erd_id,
                pair_role, vt, ct, opts, field_id,
                'box' if ha_domain == 'number' else '',
                p_on, p_off, s_on, s_off,
                min_val, max_val, step_val)

    _deduplicate_field_ids(results)
    return results


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
        r"(?i)(supported.*feature|supported.*state|supported.*equipment|supported.*sound theme|supported.*notification|supported.*setting|supported.*device)"
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

def generate_ha_discovery_jsonl_by_category(entries: List[Dict], filter_config_topics: bool = True) -> Dict[str, str]:
    """Generate compact JSONL content grouped by appliance category.

    Returns a dict mapping category name -> JSONL string content.
    Each JSONL line is a compact JSON object with the pre-computed ha-discovery
    fields for one entity.  Fields that equal their default value are omitted to
    reduce file size.

    Entities matching internal metadata, diagnostics, commissioning, and other
    non-user-facing patterns are always excluded (via _FILTER_PATTERNS).
    Additionally, entries with review.filtered=True are skipped in _collect.
    """
    ha_entries = _collect_ha_discovery_entries(entries)

    # Filter out internal/diagnostic entities via regex patterns
    if filter_config_topics:
        filtered = []
        filtered_count = 0
        for entry in ha_entries:
            if _should_filter_entity(entry['name']):
                filtered_count += 1
            else:
                filtered.append(entry)
        ha_entries = filtered
        if filtered_count:
            print(f"  Filtered out {filtered_count} entities (config topics)", file=sys.stderr)

    categorized: Dict[str, list] = {cat: [] for cat in CATEGORIES}
    for entry in ha_entries:
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
# Main
# ---------------------------------------------------------------------------

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Generate HA discovery JSONL files from processed ERD definitions.")
    parser.add_argument("--processed", default="appliance_api_erd_definitions_processed.json",
                        help="Path to the processed ERD definitions JSON (default: appliance_api_erd_definitions_processed.json).")
    parser.add_argument("--no-filter", action="store_true",
                        help="Disable filtering of internal/diagnostic entities.")
    parser.add_argument("--output-dir", default=None,
                        help="Output directory for JSONL files (default: repo_root/ha_discovery).")
    args = parser.parse_args()

    repo_root = Path(__file__).parent.parent.parent.parent
    output_dir = Path(args.output_dir) if args.output_dir else repo_root / 'ha_discovery'

    # Read processed file
    processed_path = Path(args.processed)
    if not processed_path.exists():
        # Try relative to repo root
        processed_path = repo_root / args.processed
    if not processed_path.exists():
        print(f"Error: Processed file not found: {args.processed}", file=sys.stderr)
        sys.exit(1)

    print(f"Reading processed ERD definitions from {processed_path}", file=sys.stderr)
    try:
        with open(processed_path, 'r', encoding='utf-8') as f:
            entries = json.load(f)
    except Exception as e:
        print(f"Failed to read {processed_path}: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Found {len(entries)} processed entries", file=sys.stderr)

    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)
    jsonl_by_cat = generate_ha_discovery_jsonl_by_category(entries, filter_config_topics=not args.no_filter)
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