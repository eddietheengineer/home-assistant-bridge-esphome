#!/usr/bin/env python3
"""
Script to generate Home Assistant MQTT Discovery JSONL files from the
public-appliance-api-documentation ERD definitions.

Reads appliance_api_erd_definitions.json and produces category-specific
JSONL files in ha_discovery/, each line being a compact JSON object
defining one HA entity.

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
  uid - Unique ID suffix override
"""

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional


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


def is_version_erd(erds_data: List[Dict]) -> bool:
    """
    Detect version ERDs: 4-byte u8 sequences where field names suggest
    version components (Major/Minor/Critical/Non-Critical).
    """
    if len(erds_data) != 4:
        return False
    for field in erds_data:
        if field['type'] != 'u8' or field['size'] != 1:
            return False
    # Check if field names contain version-related keywords
    name_lower = ' '.join(f['name'].lower() for f in erds_data)
    return 'major' in name_lower and 'minor' in name_lower


def build_value_template(field: Dict, erd_data: List[Dict], erd_id_hex: str) -> Optional[str]:
    """
    Build a Jinja2 value_template for decoding the hex payload.

    The payload is a hex-encoded string of raw ERD bytes.
    """
    ftype = field['type']
    foffset = field.get('offset', 0)
    fsize = field.get('size', 1)

    if ftype == 'string':
        # Each byte pair decoded as ASCII with 0x20 offset, trailing '_' stripped
        return (
            "{{ value | regex_findall_index('(..)') | "
            "join(' ') | regex_replace('.*', "
            "{{ value.split(' ') | map("
            "  (item) -> (int(item, 16) - 0x20) | chr | default('', true)"
            ") | join | regex_replace('_+$', '') }}') }}"
        )

    if ftype == 'enum':
        values = field.get('values', {})
        if not values:
            return None
        # Build a Jinja2 dict lookup
        pairs = ', '.join(f'"{k}": "{v}"' for k, v in values.items())
        if fsize == 1 and foffset == 0:
            # Single-byte enum at offset 0
            return f"{{{{ value | regex_findall_index('(..)') | first | default('') | regex_replace('.*', {{{{ {pairs} }}}}[\\1]]) }}}}"
        else:
            # Enum at a specific byte offset within the ERD
            byte_idx = foffset  # each byte is a pair of hex chars
            return (
                "{{ value | regex_findall_index('(..)') | "
                f"selectattr('index', 'equalto', {byte_idx}) | first | "
                f"default('') | regex_replace('.*', {{{{ {pairs} }}}}[\\1]]) }}"
            )

    if ftype == 'bool':
        # Bool: single byte, 0 or 1
        return "{{ value | regex_findall_index('(..)') | first | int(0, 16) }}"

    if ftype in ('u8', 'i8'):
        if foffset == 0 and fsize == 1:
            signed = 'signed=true' if ftype == 'i8' else 'signed=false'
            return f"{{{{ value | regex_findall_index('(..)') | first | int(0, 16, {signed}) }}}}"
        else:
            byte_idx = foffset
            signed = 'signed=true' if ftype == 'i8' else 'signed=false'
            return (
                "{{ value | regex_findall_index('(..)') | "
                f"selectattr('index', 'equalto', {byte_idx}) | first | "
                f"int(0, 16, {signed}) }}"
            )

    if ftype in ('u16', 'i16'):
        # Little-endian 16-bit: bytes at offset and offset+1
        byte_lo = foffset
        byte_hi = foffset + 1
        signed = 'signed=true' if ftype == 'i16' else 'signed=false'
        return (
            "{{ (value | regex_findall_index('(..)') | "
            f"selectattr('index', 'equalto', {byte_hi}) | first | int(0, 16) * 256 + "
            f"(value | regex_findall_index('(..)') | selectattr('index', 'equalto', {byte_lo}) | first | int(0, 16, {signed})) }}"
        )

    if ftype in ('u32', 'i32'):
        # Little-endian 32-bit
        signed = 'signed=true' if ftype == 'i32' else 'signed=false'
        return (
            "{{ (value | regex_findall_index('(..)') | "
            f"selectattr('index', 'equalto', {foffset + 3}) | first | int(0, 16) * 16777216 + "
            f"(value | regex_findall_index('(..)') | selectattr('index', 'equalto', {foffset + 2}) | first | int(0, 16) * 65536 + "
            f"(value | regex_findall_index('(..)') | selectattr('index', 'equalto', {foffset + 1}) | first | int(0, 16) * 256 + "
            f"(value | regex_findall_index('(..)') | selectattr('index', 'equalto', {foffset}) | first | int(0, 16, {signed}))) }}"
        )

    if ftype == 'raw':
        # Raw bytes - just pass through
        return "{{ value }}"

    return None


def build_command_template(field: Dict, erd_data: List[Dict]) -> Optional[str]:
    """
    Build a Jinja2 command_template for encoding commands.
    Only for writable ERDs.
    """
    ftype = field['type']
    foffset = field.get('offset', 0)

    if ftype == 'bool':
        # Convert true/false to 01/00
        return "{{ '01' if value else '00' }}"

    if ftype == 'enum':
        values = field.get('values', {})
        if not values:
            return None
        # Reverse lookup: label -> hex byte
        reverse = {}
        for k, v in values.items():
            reverse[v] = format(int(k), '02x')
        pairs = ', '.join(f'"{k}": "{v}"' for k, v in reverse.items())
        return f"{{{{ {{{{ {pairs} }}}}[value] | default('00') }}}}"

    if ftype == 'u8':
        return "{{ '{{:02x}}'.format(value | int) }}"

    if ftype == 'i8':
        return "{{ '{{:02x}}'.format(value | int) }}"

    if ftype == 'u16':
        return "{{ '{{:04x}}'.format(value | int) | regex_replace('(..)(..)', '\\2\\1') }}"

    if ftype == 'i16':
        return "{{ '{{:04x}}'.format(value | int) | regex_replace('(..)(..)', '\\2\\1') }}"

    if ftype == 'u32':
        return "{{ '{{:08x}}'.format(value | int) | regex_replace('(..)(..)(..)(..)', '\\4\\3\\2\\1') }}"

    if ftype == 'i32':
        return "{{ '{{:08x}}'.format(value | int) | regex_replace('(..)(..)(..)(..)', '\\4\\3\\2\\1') }}"

    return None


def determine_domain(erd: Dict) -> str:
    """
    Determine the HA domain for an ERD.

    Uses ha_domain from JSON when present, with corrections:
    - device_class "enum" is not valid for any domain; omit and keep original domain.
    - device_class "restart" with ha_domain "sensor" -> change to "button".
    """
    domain = erd.get('ha_domain', 'sensor')
    device_class = erd.get('device_class')

    # device_class "restart" -> button domain
    if device_class == 'restart' and domain == 'sensor':
        return 'button'

    return domain


def is_writable(erd: Dict) -> bool:
    """Check if an ERD supports write operations."""
    return 'write' in erd.get('operations', [])


def build_entity_name(erd: Dict, field: Dict, field_index: int, total_fields: int) -> str:
    """Build a human-readable entity name."""
    field_name = field.get('name', erd['name'])
    # Remove parenthetical unit suffix from field name for cleaner entity names
    field_name = field_name.split(' (')[0]

    if total_fields > 1:
        # Multi-field ERD: use ERD name + field name
        return f"{erd['name']} {field_name}"
    return field_name


def compute_erd_data_size(erds_data: List[Dict]) -> int:
    """Compute the total data size of an ERD (max offset + size of last field)."""
    if not erds_data:
        return 0
    max_end = 0
    for field in erds_data:
        end = field.get('offset', 0) + field.get('size', 1)
        if end > max_end:
            max_end = end
    return max_end


def generate_entities(erd: Dict) -> List[Dict[str, Any]]:
    """
    Generate JSONL entity definitions for a single ERD.

    For multi-field ERDs, generates one entity per field.
    For paired ERDs, the request ERD carries the paired info.
    """
    entities = []
    erds_data = erd.get('data', [])
    if not erds_data:
        return entities

    erd_id_hex = erd_id_to_hex(erd['id'])
    domain = determine_domain(erd)
    data_size = compute_erd_data_size(erds_data)
    is_version = is_version_erd(erds_data)

    # Check if this ERD has a pair
    paired_erd = erd.get('paired_erd')
    pair_role = erd.get('pair_role')

    for idx, field in enumerate(erds_data):
        entity: Dict[str, Any] = {}

        # ERD ID
        entity['i'] = erd_id_hex

        # Entity name
        entity['n'] = build_entity_name(erd, field, idx, len(erds_data))

        # Domain
        entity['d'] = domain

        # Data size (total ERD payload size, not just field size)
        entity['ds'] = data_size

        # Value template
        vt = build_value_template(field, erds_data, erd_id_hex)
        if vt:
            entity['vt'] = vt

        # Command template (only for writable ERDs)
        if is_writable(erd):
            ct = build_command_template(field, erds_data)
            if ct:
                entity['ct'] = ct

        # Unit of measurement
        if 'unit_of_measurement' in erd:
            entity['u'] = erd['unit_of_measurement']

        # Device class (validate against domain)
        dc = erd.get('device_class')
        if dc and dc != 'enum':
            entity['dc'] = dc

        # State class
        if 'state_class' in erd:
            entity['sc'] = erd['state_class']

        # Field ID for multi-field ERDs
        if len(erds_data) > 1:
            entity['fi'] = idx

        # Paired ERD info
        if paired_erd:
            entity['p'] = erd_id_to_hex(paired_erd)
        if pair_role:
            entity['r'] = pair_role

        # Options for select domain
        if domain == 'select' and field.get('type') == 'enum' and field.get('values'):
            entity['o'] = list(field['values'].values())

        # Data type for number domain
        if domain == 'number':
            entity['dt'] = field['type']

        # Scale factor
        if 'scaling_factor' in field:
            entity['sf'] = field['scaling_factor']

        entities.append(entity)

    return entities


def categorize_entities(erds: List[Dict]) -> Dict[str, List[Dict[str, Any]]]:
    """
    Group ERDs into categories and generate entity definitions.
    Returns a dict of category -> list of entity JSONL objects.
    """
    result = {name: [] for name in CATEGORIES}

    for erd in erds:
        erd_id = parse_erd_id(erd['id'])
        category = get_category(erd_id)
        if not category:
            continue

        entities = generate_entities(erd)
        result[category].extend(entities)

    return result


def write_jsonl(output_dir: Path, entities: List[Dict[str, Any]], filename: str) -> int:
    """Write entity definitions to a JSONL file. Returns number of lines written."""
    path = output_dir / filename
    with open(path, 'w') as f:
        for entity in entities:
            f.write(json.dumps(entity, separators=(',', ':')) + '\n')
    return len(entities)


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
    url = "https://raw.githubusercontent.com/geappliances/public-appliance-api-documentation/main/appliance_api_erd_definitions.json"
    print(f"Fetching ERD definitions from GitHub: {url}", file=sys.stderr)
    try:
        with urllib.urlopen(url, timeout=10) as response:
            return json.loads(response.read().decode('utf-8'))
    except Exception as e:
        print(f"Failed to fetch from GitHub: {e}", file=sys.stderr)
        return None


def main():
    """Main entry point for the script."""
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    output_dir = repo_root / 'ha_discovery'

    # Try to find the JSON file locally
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

    # Categorize and generate entities
    categories = categorize_entities(erds)

    total_entities = 0
    for category, entities in categories.items():
        filename = f"{category}.jsonl"
        count = write_jsonl(output_dir, entities, filename)
        total_entities += count
        print(f"  {category}: {count} entities -> {filename}", file=sys.stderr)

    print(f"\nTotal entities generated: {total_entities}", file=sys.stderr)
    print(f"Output directory: {output_dir}", file=sys.stderr)
    print("Done!", file=sys.stderr)


if __name__ == '__main__':
    main()
