#!/usr/bin/env python3
"""
Tests for generate_ha_discovery.py output.

Validates that all generated JSONL files:
  1. Are valid JSON with correct structure
  2. Have Jinja2 value/command templates that compile and execute
  3. Produce correct results for known test payloads
  4. Cover all expected data types (signed, unsigned, enum, bitfield)

Run with:
    python3 -m pytest scripts/test_ha_discovery.py -v
  or:
    python3 -m unittest scripts.test_ha_discovery -v
"""

import json
import os
import re
import sys
import unittest
from pathlib import Path
from typing import Any, Dict, List, Tuple

# Add scripts dir to path so we can import generate_ha_discovery
sys.path.insert(0, str(Path(__file__).parent))
import generate_ha_discovery as gen

# Jinja2 for template validation
import jinja2


JINJA2_ENV = jinja2.Environment()

# All category files
CATEGORIES = [
    'common', 'airconditioning', 'refrigeration', 'laundry',
    'dishwasher', 'waterheater', 'range', 'waterfilter',
    'smallappliance', 'energy',
]


def _get_erd_definitions() -> List[Dict[str, Any]]:
    """Load ERD definitions, trying local submodule then GitHub fallback."""
    json_file = Path(__file__).parent.parent / 'lib' / 'public-appliance-api-documentation' / 'appliance_api_erd_definitions.json'
    if json_file.exists():
        with open(json_file) as f:
            data = json.load(f)
        return data.get('erds', [])
    # Fallback: fetch from GitHub (eddietheengineer fork has ha_domain metadata)
    import urllib.request
    url = "https://raw.githubusercontent.com/eddietheengineer/public-appliance-api-documentation/main/appliance_api_erd_definitions.json"
    with urllib.request.urlopen(url, timeout=30) as resp:
        data = json.loads(resp.read().decode('utf-8'))
    return data.get('erds', [])


def load_all_entities(filter_config_topics: bool = False) -> List[Dict[str, Any]]:
    """Generate all entities in-memory from ERD definitions."""
    erds = _get_erd_definitions()
    jsonl_by_cat = gen.generate_ha_discovery_jsonl_by_category(erds, filter_config_topics)
    entities: List[Dict[str, Any]] = []
    for cat, content in jsonl_by_cat.items():
        for line in content.split('\n'):
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            obj['_category'] = cat
            entities.append(obj)
    return entities


def load_erd_definitions() -> List[Dict[str, Any]]:
    """Load ERD definitions from the JSON file."""
    return _get_erd_definitions()


class TestJSONLStructure(unittest.TestCase):
    """Test that all generated entities have valid structure."""

    def setUp(self):
        self.entities = load_all_entities()

    def test_all_categories_have_entities(self):
        """All expected categories have entities."""
        categories_with_entities = set(e['_category'] for e in self.entities)
        for cat in CATEGORIES:
            self.assertIn(cat, categories_with_entities,
                f'{cat} has no entities')

    def test_all_entities_valid_json(self):
        """Every entity is a valid dict with expected fields."""
        for obj in self.entities:
            with self.subTest(entity=obj.get('n', '?'), erd=obj.get('i', '?')):
                self.assertIsInstance(obj, dict)

    def test_required_fields_present(self):
        """Every entity has the required fields: i, n, d, ds."""
        for obj in self.entities:
            with self.subTest(entity=obj.get('n', '?'), erd=obj.get('i', '?')):
                self.assertIn('i', obj, f'Missing "i" in {obj.get("n")}')
                self.assertIn('n', obj, f'Missing "n" in {obj.get("i")}')
                self.assertIn('d', obj, f'Missing "d" in {obj.get("i")}')
                self.assertIn('ds', obj, f'Missing "ds" in {obj.get("i")}')

    def test_valid_domains(self):
        """Every entity has a valid HA domain."""
        valid_domains = {'sensor', 'binary_sensor', 'switch', 'select', 'number', 'button'}
        for obj in self.entities:
            with self.subTest(entity=obj.get('n', '?')):
                self.assertIn(obj['d'], valid_domains,
                    f'{obj["n"]} has invalid domain {obj["d"]}')

    def test_data_size_positive(self):
        """Every entity has a positive data size."""
        for obj in self.entities:
            with self.subTest(entity=obj.get('n', '?')):
                self.assertGreater(obj['ds'], 0,
                    f'{obj["n"]} has non-positive data_size {obj["ds"]}')

    def test_no_duplicate_unique_ids(self):
        """No two entities share the same (erd_id, field_id) combination."""
        seen: Dict[Tuple[str, str], str] = {}
        for obj in self.entities:
            key = (obj['i'], obj.get('fi', ''))
            if key in seen:
                if seen[key] == obj['n']:
                    continue
                self.fail(
                    f'Duplicate unique_id key {key}: '
                    f'{seen[key]} and {obj["n"]}')
            seen[key] = obj['n']

    def test_total_entity_count(self):
        """Verify total entity count is reasonable (not zero, not excessive)."""
        self.assertGreater(len(self.entities), 5000,
            f'Expected >5000 entities, got {len(self.entities)}')
        self.assertLess(len(self.entities), 20000,
            f'Expected <20000 entities, got {len(self.entities)}')


class TestJinja2Templates(unittest.TestCase):
    """Test that all Jinja2 templates compile and execute correctly."""

    def test_all_value_templates_compile(self):
        """Every value_template compiles without error."""
        entities = load_all_entities()
        for obj in entities:
            vt = obj.get('vt', '')
            if not vt:
                continue
            with self.subTest(entity=obj['n'], erd=obj['i']):
                try:
                    JINJA2_ENV.from_string(vt)
                except jinja2.TemplateSyntaxError as e:
                    self.fail(f'{obj["n"]} vt syntax error: {e}\n  {vt[:100]}')

    def test_all_command_templates_compile(self):
        """Every command_template compiles without error."""
        entities = load_all_entities()
        for obj in entities:
            ct = obj.get('ct', '')
            if not ct:
                continue
            with self.subTest(entity=obj['n'], erd=obj['i']):
                try:
                    JINJA2_ENV.from_string(ct)
                except jinja2.TemplateSyntaxError as e:
                    self.fail(f'{obj["n"]} ct syntax error: {e}\n  {ct[:100]}')

    def test_value_templates_execute(self):
        """Every value_template executes without error with sample payloads."""
        entities = load_all_entities()
        for obj in entities:
            vt = obj.get('vt', '')
            if not vt:
                continue
            ds = obj['ds']
            # Generate a hex payload matching the data size
            payload = '00' * ds
            compiled = JINJA2_ENV.from_string(vt)
            with self.subTest(entity=obj['n'], erd=obj['i']):
                try:
                    result = compiled.render(value=payload)
                    self.assertIsInstance(result, str)
                except Exception as e:
                    self.fail(f'{obj["n"]} vt execution error with payload {payload}: {e}\n  {vt[:100]}')

    def test_command_templates_execute(self):
        """Every command_template executes without error with sample values."""
        entities = load_all_entities()
        for obj in entities:
            ct = obj.get('ct', '')
            if not ct:
                continue
            compiled = JINJA2_ENV.from_string(ct)
            with self.subTest(entity=obj['n'], erd=obj['i']):
                try:
                    result = compiled.render(value=0)
                    self.assertIsInstance(result, str)
                except Exception as e:
                    self.fail(f'{obj["n"]} ct execution error with value=0: {e}\n  {ct[:100]}')

    def test_no_unbalanced_braces(self):
        """All templates have balanced {{ }}."""
        entities = load_all_entities()
        for obj in entities:
            for tmpl_type, tmpl in [('vt', obj.get('vt', '')), ('ct', obj.get('ct', ''))]:
                if not tmpl:
                    continue
                with self.subTest(entity=obj['n'], tmpl=tmpl_type):
                    open_count = tmpl.count('{{')
                    close_count = tmpl.count('}}')
                    self.assertEqual(open_count, close_count,
                        f'{obj["n"]} {tmpl_type} unbalanced braces: {{ = {open_count}, }} = {close_count}')

    def test_no_unbalanced_parens(self):
        """All templates have balanced parentheses."""
        entities = load_all_entities()
        for obj in entities:
            for tmpl_type, tmpl in [('vt', obj.get('vt', '')), ('ct', obj.get('ct', ''))]:
                if not tmpl:
                    continue
                with self.subTest(entity=obj['n'], tmpl=tmpl_type):
                    self.assertEqual(tmpl.count('('), tmpl.count(')'),
                        f'{obj["n"]} {tmpl_type} unbalanced parens')


class TestSignedIntegerTemplates(unittest.TestCase):
    """Test that signed integer templates produce correct two's-complement results."""

    def test_i16_positive(self):
        """i16 positive values are decoded correctly."""
        # Template: {{ (value | int(base=16)) - 65536 if (value | int(base=16)) >= 32768 else (value | int(base=16)) }}
        tmpl = JINJA2_ENV.from_string(
            '{{ (value | int(base=16)) - 65536 if (value | int(base=16)) >= 32768 else (value | int(base=16)) }}')
        self.assertEqual(int(tmpl.render(value='0064')), 100)
        self.assertEqual(int(tmpl.render(value='7fff')), 32767)
        self.assertEqual(int(tmpl.render(value='0000')), 0)

    def test_i16_negative(self):
        """i16 negative values are decoded correctly via two's complement."""
        tmpl = JINJA2_ENV.from_string(
            '{{ (value | int(base=16)) - 65536 if (value | int(base=16)) >= 32768 else (value | int(base=16)) }}')
        self.assertEqual(int(tmpl.render(value='ffff')), -1)
        self.assertEqual(int(tmpl.render(value='8000')), -32768)
        self.assertEqual(int(tmpl.render(value='ff96')), -106)

    def test_i16_with_scaling(self):
        """i16 with scaling factor produces correct decimal results."""
        tmpl = JINJA2_ENV.from_string(
            '{{ ((value | int(base=16)) - 65536 if (value | int(base=16)) >= 32768 else (value | int(base=16))) / 10 | round(1) }}')
        self.assertEqual(float(tmpl.render(value='0064')), 10.0)   # 100/10
        self.assertEqual(float(tmpl.render(value='ff96')), -10.6)  # -106/10
        self.assertEqual(float(tmpl.render(value='0032')), 5.0)    # 50/10

    def test_i32_negative(self):
        """i32 negative values are decoded correctly."""
        tmpl = JINJA2_ENV.from_string(
            '{{ (value | int(base=16)) - 4294967296 if (value | int(base=16)) >= 2147483648 else (value | int(base=16)) }}')
        self.assertEqual(int(tmpl.render(value='ffffffff')), -1)
        self.assertEqual(int(tmpl.render(value='80000000')), -2147483648)

    def test_signed_command_template(self):
        """Signed number command template handles negative values."""
        tmpl = JINJA2_ENV.from_string(
            "{{ '%04x' % ((value | int) % 65536) }}")
        self.assertEqual(tmpl.render(value=-1), 'ffff')
        self.assertEqual(tmpl.render(value=-10), 'fff6')
        self.assertEqual(tmpl.render(value=100), '0064')

    def test_signed_command_template_with_scaling(self):
        """Signed number command template with scaling factor."""
        tmpl = JINJA2_ENV.from_string(
            "{{ '%04x' % ((((value | float) * 10) | round | int) % 65536) }}")
        self.assertEqual(tmpl.render(value=10.5), '0069')   # 105
        self.assertEqual(tmpl.render(value=-10.5), 'ff97')  # -105 % 65536 = 65431 = 0xff97


class TestEnumTemplates(unittest.TestCase):
    """Test that enum templates produce correct label mappings."""

    def test_enum_value_template(self):
        """Enum value template maps hex to label."""
        tmpl = JINJA2_ENV.from_string(
            "{{ {'00': 'Stop', '01': 'Heat', '02': 'Fan', '03': 'Cool'}.get(value[:2], 'Unknown') }}")
        self.assertEqual(tmpl.render(value='00'), 'Stop')
        self.assertEqual(tmpl.render(value='01'), 'Heat')
        self.assertEqual(tmpl.render(value='03'), 'Cool')
        self.assertEqual(tmpl.render(value='ff'), 'Unknown')

    def test_enum_with_apostrophe(self):
        """Enum values with apostrophes are properly escaped."""
        tmpl = JINJA2_ENV.from_string(
            "{{ {'00': 'Don\\'t Care', '01': 'Auto', '02': 'Manual'}.get(value[:2], 'Unknown') }}")
        self.assertEqual(tmpl.render(value='00'), "Don't Care")
        self.assertEqual(tmpl.render(value='01'), 'Auto')
        self.assertEqual(tmpl.render(value='02'), 'Manual')

    def test_enum_command_template(self):
        """Enum command template maps label to hex."""
        tmpl = JINJA2_ENV.from_string(
            "{{ {'Stop': '00', 'Heat': '01', 'Cool': '03'}[value] }}")
        self.assertEqual(tmpl.render(value='Stop'), '00')
        self.assertEqual(tmpl.render(value='Heat'), '01')
        self.assertEqual(tmpl.render(value='Cool'), '03')

    def test_enum_with_apostrophe_command(self):
        """Enum command template with apostrophe in label."""
        tmpl = JINJA2_ENV.from_string(
            "{{ {'Don\\'t Care': '00', 'Auto': '01'}[value] }}")
        self.assertEqual(tmpl.render(value="Don't Care"), '00')


class TestBitfieldTemplates(unittest.TestCase):
    """Test that bitfield templates extract bits correctly using arithmetic."""

    def test_1bit_extraction(self):
        """1-bit bitfield extraction using // and %."""
        # Original: ((value[0:4] | int(base=16)) >> 8) & 1
        # Fixed:    ((value[0:4] | int(base=16)) // 256) % 2
        tmpl = JINJA2_ENV.from_string(
            "{{ '01' if ((value[0:4] | int(base=16)) // 256) % 2 else '00' }}")
        self.assertEqual(tmpl.render(value='0000'), '00')
        self.assertEqual(tmpl.render(value='0100'), '01')
        self.assertEqual(tmpl.render(value='0200'), '00')
        self.assertEqual(tmpl.render(value='0300'), '01')

    def test_1bit_at_various_offsets(self):
        """1-bit extraction at different bit offsets."""
        # Bit 0
        tmpl0 = JINJA2_ENV.from_string(
            "{{ '01' if ((value[0:4] | int(base=16)) // 1) % 2 else '00' }}")
        self.assertEqual(tmpl0.render(value='0001'), '01')
        self.assertEqual(tmpl0.render(value='0000'), '00')

        # Bit 15
        tmpl15 = JINJA2_ENV.from_string(
            "{{ '01' if ((value[0:4] | int(base=16)) // 32768) % 2 else '00' }}")
        self.assertEqual(tmpl15.render(value='8000'), '01')
        self.assertEqual(tmpl15.render(value='0000'), '00')

    def test_multibit_extraction(self):
        """Multi-bit field extraction using // and %."""
        # Original: ((value[0:4] | int(base=16)) >> 24) & 0xFF
        # Fixed:    ((value[0:8] | int(base=16)) // 16777216) % 256
        tmpl = JINJA2_ENV.from_string(
            "{{ ((value[0:8] | int(base=16)) // 16777216) % 256 }}")
        self.assertEqual(int(tmpl.render(value='00000000')), 0)
        self.assertEqual(int(tmpl.render(value='ff000000')), 255)
        self.assertEqual(int(tmpl.render(value='01000000')), 1)
        self.assertEqual(int(tmpl.render(value='80000000')), 128)


class TestNumberCommandTemplates(unittest.TestCase):
    """Test number command templates produce correct hex output."""

    def test_u8_command(self):
        """u8 command template produces 2-char hex."""
        tmpl = JINJA2_ENV.from_string("{{ '%02x' % (value | int) }}")
        self.assertEqual(tmpl.render(value=0), '00')
        self.assertEqual(tmpl.render(value=255), 'ff')
        self.assertEqual(tmpl.render(value=100), '64')

    def test_u16_command(self):
        """u16 command template produces 4-char hex."""
        tmpl = JINJA2_ENV.from_string("{{ '%04x' % (value | int) }}")
        self.assertEqual(tmpl.render(value=0), '0000')
        self.assertEqual(tmpl.render(value=65535), 'ffff')
        self.assertEqual(tmpl.render(value=100), '0064')

    def test_u16_with_scaling(self):
        """u16 with scaling factor produces correct hex."""
        tmpl = JINJA2_ENV.from_string(
            "{{ '%04x' % (((value | float) * 10) | round | int) }}")
        self.assertEqual(tmpl.render(value=10.5), '0069')   # 105
        self.assertEqual(tmpl.render(value=0.1), '0001')    # 1

    def test_i16_command(self):
        """i16 command template handles negative values via modulo."""
        tmpl = JINJA2_ENV.from_string("{{ '%04x' % ((value | int) % 65536) }}")
        self.assertEqual(tmpl.render(value=-1), 'ffff')
        self.assertEqual(tmpl.render(value=-10), 'fff6')
        self.assertEqual(tmpl.render(value=100), '0064')

    def test_i16_with_scaling(self):
        """i16 with scaling factor handles negative values."""
        tmpl = JINJA2_ENV.from_string(
            "{{ '%04x' % ((((value | float) * 10) | round | int) % 65536) }}")
        self.assertEqual(tmpl.render(value=-10.5), 'ff97')
        self.assertEqual(tmpl.render(value=10.5), '0069')


class TestGeneratedTemplatesMatchERD(unittest.TestCase):
    """Test that generated templates match ERD definitions."""

    def setUp(self):
        self.entities = load_all_entities()
        self.erds = load_erd_definitions()
        self.erd_by_id = {e['id']: e for e in self.erds}

    def test_sensor_enum_detection(self):
        """Sensors with enum device_class get enum templates, not raw numeric.

        Only checks entities whose device_class is explicitly 'enum', since
        multi-field ERDs may have both enum and non-enum sub-fields.
        """
        for obj in self.entities:
            if obj['d'] != 'sensor':
                continue
            if obj.get('dc') != 'enum':
                continue
            if not obj.get('vt'):
                continue
            with self.subTest(entity=obj['n']):
                self.assertIn('.get(', obj['vt'],
                    f'{obj["n"]} has device_class=enum but vt has no .get() mapping')

    def test_signed_sensor_has_sign_extension(self):
        """Sensors with signed i16/i32 primary data have two's-complement handling.

        Only checks single-field entities (no field_id) where the primary data
        type is signed. Multi-field sub-fields are handled by the generator
        based on their individual field type, not the parent ERD's type.
        """
        for obj in self.entities:
            if obj['d'] != 'sensor':
                continue
            # Skip sub-fields — they have their own type from the field def
            if obj.get('fi'):
                continue
            erd_id = f'0x{obj["i"]}'
            erd = self.erd_by_id.get(erd_id)
            if not erd:
                continue
            erd_data = erd.get('data', [])
            primary_type = 'u8'
            for d in erd_data:
                if not gen._is_reserved_field(d.get('name', '')):
                    primary_type = d.get('type', 'u8')
                    break
            if not primary_type.startswith('i'):
                continue
            if primary_type == 'i8':
                continue
            if not obj.get('vt'):
                continue
            vt = obj['vt']
            has_sign = '- 65536' in vt or '- 4294967296' in vt
            self.assertTrue(has_sign,
                f'{obj["n"]} is {primary_type} type but vt has no sign extension: {vt[:80]}')

    def test_binary_sensor_bitfield_has_arithmetic(self):
        """Binary sensor bitfields use // and % instead of >> and &."""
        for obj in self.entities:
            if obj['d'] != 'binary_sensor':
                continue
            vt = obj.get('vt', '')
            if not vt:
                continue
            with self.subTest(entity=obj['n']):
                self.assertNotIn('>>', vt,
                    f'{obj["n"]} vt uses >> (invalid Jinja2): {vt[:80]}')
                self.assertNotIn('& 1', vt,
                    f'{obj["n"]} vt uses & 1 (invalid Jinja2): {vt[:80]}')

    def test_select_has_options(self):
        """Select entities have options array (may be JSON string or native list)."""
        for obj in self.entities:
            if obj['d'] != 'select':
                continue
            with self.subTest(entity=obj['n']):
                self.assertIn('o', obj,
                    f'{obj["n"]} is select but has no options')
                opts = obj['o']
                if isinstance(opts, str):
                    opts = json.loads(opts)
                self.assertIsInstance(opts, list)
                self.assertGreater(len(opts), 0)

    def test_select_has_value_and_command_templates(self):
        """Select entities have both value_template and command_template."""
        for obj in self.entities:
            if obj['d'] != 'select':
                continue
            with self.subTest(entity=obj['n']):
                self.assertIn('vt', obj,
                    f'{obj["n"]} is select but has no value_template')
                self.assertIn('ct', obj,
                    f'{obj["n"]} is select but has no command_template')

    def test_number_has_command_template(self):
        """Number entities have command_template."""
        for obj in self.entities:
            if obj['d'] != 'number':
                continue
            with self.subTest(entity=obj['n']):
                self.assertIn('ct', obj,
                    f'{obj["n"]} is number but has no command_template')

    def test_no_bitwise_operators_in_any_template(self):
        """No template uses >> or & bitwise operators (invalid in Jinja2)."""
        import re
        for obj in self.entities:
            for tmpl_type, tmpl in [('vt', obj.get('vt', '')), ('ct', obj.get('ct', ''))]:
                if not tmpl:
                    continue
                with self.subTest(entity=obj['n'], tmpl=tmpl_type):
                    self.assertNotIn('>>', tmpl,
                        f'{obj["n"]} {tmpl_type} uses >> (invalid Jinja2)')
                    if re.search(r'&\s*\d', tmpl):
                        self.fail(
                            f'{obj["n"]} {tmpl_type} uses & as bitwise operator: {tmpl[:80]}')


class TestFieldSlugGeneration(unittest.TestCase):
    """Test that field slugs are generated correctly."""

    def test_leaf_field_name_with_decimal(self):
        """_leaf_field_name handles dotted names with decimals."""
        self.assertEqual(gen._leaf_field_name('Air Purifier.PM2.5'), 'PM2.5')
        self.assertEqual(gen._leaf_field_name('Reserved 1.2'), 'Reserved 1.2')

    def test_leaf_field_name_normal(self):
        """_leaf_field_name handles normal dotted names."""
        self.assertEqual(gen._leaf_field_name('Allowed Selections.Cyclic Supported'), 'Cyclic Supported')
        self.assertEqual(gen._leaf_field_name('Air Purifier.Air Purifier Off'), 'Air Purifier Off')
        self.assertEqual(gen._leaf_field_name('Bluetooth ERD Stream.MAC address[0]'), 'MAC address[0]')

    def test_leaf_field_name_no_dot(self):
        """_leaf_field_name handles names without dots."""
        self.assertEqual(gen._leaf_field_name('Temperature'), 'Temperature')
        self.assertEqual(gen._leaf_field_name('  Spaced  '), 'Spaced')

    def test_field_slug(self):
        """_field_slug produces valid identifiers."""
        self.assertEqual(gen._field_slug('Critical Major'), 'critical_major')
        self.assertEqual(gen._field_slug('GH (Fan Hi)'), 'gh_fan_hi')
        self.assertEqual(gen._field_slug('Cyclic Supported'), 'cyclic_supported')

    def test_jinja2_escape(self):
        """_jinja2_escape properly escapes apostrophes."""
        self.assertEqual(gen._jinja2_escape("Don't Care"), "Don\\'t Care")
        self.assertEqual(gen._jinja2_escape('Normal'), 'Normal')
        self.assertEqual(gen._jinja2_escape("It's a test"), "It\\'s a test")


class TestActualZonelineERDs(unittest.TestCase):
    """Test specific Zoneline ERD templates that are critical for the adapter."""

    def test_erd_7000_system_mode(self):
        """ERD 0x7000 System Mode enum template works correctly."""
        # Find the entity in generated files
        entities = load_all_entities()
        obj = None
        for e in entities:
            if e['i'] == '7000' and not e.get('fi'):
                obj = e
                break
        self.assertIsNotNone(obj, 'ERD 0x7000 not found')
        self.assertEqual(obj['d'], 'sensor')
        self.assertIn('vt', obj)

        tmpl = JINJA2_ENV.from_string(obj['vt'])
        self.assertEqual(tmpl.render(value='00'), 'Stop')
        self.assertEqual(tmpl.render(value='01'), 'Heat')
        self.assertEqual(tmpl.render(value='03'), 'Cool')
        self.assertEqual(tmpl.render(value='ff'), 'Unknown')

    def test_erd_7002_target_heating_temp(self):
        """ERD 0x7002 Target Heating Temperature signed i16 template."""
        entities = load_all_entities()
        obj = None
        for e in entities:
            if e['i'] == '7002' and not e.get('fi'):
                obj = e
                break
        self.assertIsNotNone(obj, 'ERD 0x7002 not found')
        self.assertEqual(obj['d'], 'sensor')
        self.assertEqual(obj.get('dc'), 'temperature')
        self.assertIn('vt', obj)

        tmpl = JINJA2_ENV.from_string(obj['vt'])
        self.assertEqual(int(tmpl.render(value='0064')), 100)
        self.assertEqual(int(tmpl.render(value='ffff')), -1)

    def test_erd_7100_inside_ambient_temp(self):
        """ERD 0x7100 Inside ambient temperature signed i16 with scaling."""
        entities = load_all_entities()
        obj = None
        for e in entities:
            if e['i'] == '7100' and not e.get('fi'):
                obj = e
                break
        self.assertIsNotNone(obj, 'ERD 0x7100 not found')
        self.assertEqual(obj['d'], 'sensor')
        self.assertEqual(obj.get('dc'), 'temperature')
        self.assertEqual(obj.get('sf'), 10)
        self.assertIn('vt', obj)

        tmpl = JINJA2_ENV.from_string(obj['vt'])
        self.assertEqual(float(tmpl.render(value='0064')), 10.0)
        self.assertEqual(float(tmpl.render(value='ff96')), -10.6)

    def test_erd_7010_relay_status_bitfields(self):
        """ERD 0x7010 Relay Status bitfield templates use arithmetic."""
        entities = load_all_entities()
        relay_entities = [e for e in entities if e['i'] == '7010']
        self.assertGreater(len(relay_entities), 0, 'ERD 0x7010 not found')

        for obj in relay_entities:
            self.assertEqual(obj['d'], 'binary_sensor')
            self.assertIn('vt', obj)
            vt = obj['vt']
            # Verify no bitwise operators
            self.assertNotIn('>>', vt, f'{obj["n"]} uses >>')
            self.assertNotIn('& 1', vt, f'{obj["n"]} uses & 1')
            # Verify it uses arithmetic
            self.assertIn('//', vt, f'{obj["n"]} missing //')
            self.assertIn('% 2', vt, f'{obj["n"]} missing % 2')

            # Test execution
            tmpl = JINJA2_ENV.from_string(vt)
            result = tmpl.render(value='0000')
            self.assertIn(result, ('00', '01'))

    def test_erd_7052_energy_conservation_select(self):
        """ERD 0x7052 Energy Conservation select entity."""
        entities = load_all_entities()
        obj = None
        for e in entities:
            if e['i'] == '7052' and not e.get('fi'):
                obj = e
                break
        self.assertIsNotNone(obj, 'ERD 0x7052 not found')
        self.assertEqual(obj['d'], 'select')
        self.assertIn('vt', obj)
        self.assertIn('ct', obj)
        self.assertIn('o', obj)

        vt_tmpl = JINJA2_ENV.from_string(obj['vt'])
        self.assertEqual(vt_tmpl.render(value='00'), 'None')
        self.assertEqual(vt_tmpl.render(value='01'), 'Simple')

        ct_tmpl = JINJA2_ENV.from_string(obj['ct'])
        self.assertEqual(ct_tmpl.render(value='None'), '00')
        self.assertEqual(ct_tmpl.render(value='Simple'), '01')


class TestVersionTemplates(unittest.TestCase):
    """Test that version ERD templates produce correct dotted decimal output."""

    def test_common_version_entities_consolidated(self):
        """Common version ERDs produce a single entity each, not four."""
        entities = load_all_entities()
        common = [e for e in entities if e['i'] in ('0039', '003a', '003b', '003c')]
        # Should be exactly 4 entities (one per ERD), not 16
        self.assertEqual(len(common), 4,
                         f"Expected 4 consolidated version entities, got {len(common)}")

    def test_application_version_template(self):
        """Application Version (0x003a) produces dotted decimal from hex."""
        entities = load_all_entities()
        obj = next((e for e in entities if e['i'] == '003a'), None)
        self.assertIsNotNone(obj, "Application Version entity not found")
        self.assertIn('vt', obj)
        tmpl = JINJA2_ENV.from_string(obj['vt'])
        self.assertEqual(tmpl.render(value='01000203'), '1.0.2.3')
        self.assertEqual(tmpl.render(value='00000000'), '0.0.0.0')
        self.assertEqual(tmpl.render(value='ff0a0b0c'), '255.10.11.12')

    def test_boot_loader_version_template(self):
        """Boot Loader Version (0x0039) produces dotted decimal."""
        entities = load_all_entities()
        obj = next((e for e in entities if e['i'] == '0039'), None)
        self.assertIsNotNone(obj)
        tmpl = JINJA2_ENV.from_string(obj['vt'])
        self.assertEqual(tmpl.render(value='02010000'), '2.1.0.0')

    def test_dishwasher_multi_board_versions(self):
        """Dishwasher 0x304d produces per-board version + parametric entities."""
        entities = load_all_entities()
        version_entities = [e for e in entities if e['i'] == '304d']
        # 4 boards x 2 (version + parametric) = 8 entities
        self.assertEqual(len(version_entities), 8,
                         f"Expected 8 dishwasher version entities, got {len(version_entities)}")

        # Check UI version entity
        ui_ver = next((e for e in version_entities if 'UI Version' in e['n']), None)
        self.assertIsNotNone(ui_ver, "UI Version entity not found")
        tmpl = JINJA2_ENV.from_string(ui_ver['vt'])
        # Simulate: UI=1.2.3.4 at offsets 0-3
        payload = '0102030405060708090a0b0c0d0e0f101112131415161718'
        self.assertEqual(tmpl.render(value=payload), '1.2.3.4')

        # Check UI parametric entity
        ui_param = next((e for e in version_entities if 'UI Parametric' in e['n']), None)
        self.assertIsNotNone(ui_param, "UI Parametric Version entity not found")
        tmpl = JINJA2_ENV.from_string(ui_param['vt'])
        self.assertEqual(tmpl.render(value=payload), '5.6')

        # Check MC version
        mc_ver = next((e for e in version_entities if 'MC Version' in e['n']), None)
        self.assertIsNotNone(mc_ver)
        tmpl = JINJA2_ENV.from_string(mc_ver['vt'])
        self.assertEqual(tmpl.render(value=payload), '7.8.9.10')

    def test_dishwasher_version_field_ids_unique(self):
        """Each board version and parametric entity has a unique field_id."""
        entities = load_all_entities()
        version_entities = [e for e in entities if e['i'] == '304d']
        field_ids = [(e['n'], e.get('fi', '')) for e in version_entities]
        # All field_ids must be unique
        fids = [fid for _, fid in field_ids]
        self.assertEqual(len(fids), len(set(fids)),
                         f"Duplicate field_ids in 0x304d: {field_ids}")

    def test_dishwasher_tub1_versions(self):
        """Dishwasher 0x324d (Tub 1) also produces consolidated version entities."""
        entities = load_all_entities()
        version_entities = [e for e in entities if e['i'] == '324d']
        self.assertEqual(len(version_entities), 8,
                         f"Expected 8 Tub 1 version entities, got {len(version_entities)}")

class TestEntityFiltering(unittest.TestCase):
    """Test that entity filtering logic correctly includes/excludes entities."""

    def setUp(self):
        self.erds = load_erd_definitions()
        self.erd_by_id = {e['id']: e for e in self.erds}

    def test_unpaired_request_buttons_included(self):
        """Button ERDs with 'Request' in name but no pair_role should be included."""
        entities = load_all_entities()
        button_erd_ids = {'1041', '1166', '2171'}
        found = {e['i'] for e in entities if e['i'] in button_erd_ids}
        missing = button_erd_ids - found
        self.assertEqual(missing, set(),
            f"Unpaired button ERDs incorrectly filtered out: {missing}")

    def test_status_erd_with_bidirectional_pairing_suppressed(self):
        """Status ERDs with bidirectional controllable pairing should be suppressed."""
        entities = load_all_entities()
        entity_ids = {e['i'] for e in entities}
        for erd in self.erds:
            if erd.get('pair_role') != 'status':
                continue
            paired_id = erd.get('paired_erd', '')
            if not paired_id or paired_id not in self.erd_by_id:
                continue
            paired = self.erd_by_id[paired_id]
            if paired.get('pair_role') != 'request':
                continue
            if paired.get('ha_domain') not in ('switch', 'select', 'number'):
                continue
            if paired.get('paired_erd') == erd['id']:
                erd_hex = erd['id'].lower()
                self.assertNotIn(erd_hex, entity_ids,
                    f"Status ERD {erd['id']} ({erd.get('name')}) should be suppressed")

    def test_status_erd_without_bidirectional_pairing_included(self):
        """Status ERDs with asymmetric pairing should be included."""
        entities = load_all_entities()
        entity_ids = {e['i'] for e in entities}
        self.assertIn('100e', entity_ids,
            "0x100e (Turbo Freeze Status) should be included (asymmetric pairing)")
        self.assertIn('100f', entity_ids,
            "0x100f (Turbo Cool Status) should be included (asymmetric pairing)")

    def test_filter_config_topics_excludes_internal_entities(self):
        """filter_config_topics=True should exclude internal/diagnostic entities."""
        entities_no_filter = load_all_entities(filter_config_topics=False)
        entities_filtered = load_all_entities(filter_config_topics=True)
        filtered_ids = {e['i'] + e.get('fi', '') for e in entities_filtered}
        unfiltered_ids = {e['i'] + e.get('fi', '') for e in entities_no_filter}
        self.assertTrue(len(filtered_ids) < len(unfiltered_ids),
            "filter_config_topics should reduce entity count")
        self.assertTrue(filtered_ids.issubset(unfiltered_ids),
            "Filtered entities should be a subset of unfiltered")


class TestDeduplicateFieldIds(unittest.TestCase):
    """Test that _deduplicate_field_ids resolves collisions correctly."""

    def test_no_collision_passes_through(self):
        """Entries with unique field_ids are unchanged."""
        entries = [
            {'erd_id': 0x301b, 'field_id': 'temp_high', 'value_template': '{{ value[0:2] }}'},
            {'erd_id': 0x301b, 'field_id': 'temp_low', 'value_template': '{{ value[2:4] }}'},
        ]
        gen._deduplicate_field_ids(entries)
        self.assertEqual(entries[0]['field_id'], 'temp_high')
        self.assertEqual(entries[1]['field_id'], 'temp_low')

    def test_collision_with_value_template(self):
        """Colliding field_ids are disambiguated using byte offset from value_template."""
        entries = [
            {'erd_id': 0x301b, 'field_id': 'index', 'value_template': '{{ value[0:2] }}'},
            {'erd_id': 0x301b, 'field_id': 'index', 'value_template': '{{ value[4:6] }}'},
            {'erd_id': 0x301b, 'field_id': 'index', 'value_template': '{{ value[8:10] }}'},
        ]
        gen._deduplicate_field_ids(entries)
        self.assertEqual(entries[0]['field_id'], 'index')  # first occurrence kept
        self.assertEqual(entries[1]['field_id'], 'index_4')
        self.assertEqual(entries[2]['field_id'], 'index_8')

    def test_collision_without_value_template(self):
        """Colliding entries without value_template use a counter fallback."""
        entries = [
            {'erd_id': 0x1041, 'field_id': 'press', 'value_template': ''},
            {'erd_id': 0x1041, 'field_id': 'press', 'value_template': ''},
            {'erd_id': 0x1041, 'field_id': 'press', 'value_template': ''},
        ]
        gen._deduplicate_field_ids(entries)
        self.assertEqual(entries[0]['field_id'], 'press')
        self.assertEqual(entries[1]['field_id'], 'press_1')
        self.assertEqual(entries[2]['field_id'], 'press_2')

    def test_collision_across_different_erds_is_independent(self):
        """Same field_id in different ERDs is NOT a collision."""
        entries = [
            {'erd_id': 0x301b, 'field_id': 'temp', 'value_template': '{{ value[0:2] }}'},
            {'erd_id': 0x301c, 'field_id': 'temp', 'value_template': '{{ value[0:2] }}'},
        ]
        gen._deduplicate_field_ids(entries)
        self.assertEqual(entries[0]['field_id'], 'temp')
        self.assertEqual(entries[1]['field_id'], 'temp')

    def test_empty_field_id_skipped(self):
        """Entries with empty field_id are not modified."""
        entries = [
            {'erd_id': 0x301b, 'field_id': '', 'value_template': '{{ value[0:2] }}'},
            {'erd_id': 0x301b, 'field_id': '', 'value_template': '{{ value[2:4] }}'},
        ]
        gen._deduplicate_field_ids(entries)
        self.assertEqual(entries[0]['field_id'], '')
        self.assertEqual(entries[1]['field_id'], '')

    def test_new_id_collides_with_existing(self):
        """If the disambiguated id already exists, a suffix is appended."""
        entries = [
            {'erd_id': 0x301b, 'field_id': 'index', 'value_template': '{{ value[0:2] }}'},
            {'erd_id': 0x301b, 'field_id': 'index_4', 'value_template': '{{ value[2:4] }}'},
            {'erd_id': 0x301b, 'field_id': 'index', 'value_template': '{{ value[4:6] }}'},
        ]
        gen._deduplicate_field_ids(entries)
        self.assertEqual(entries[0]['field_id'], 'index')
        self.assertEqual(entries[1]['field_id'], 'index_4')
        # Third entry would get index_4, but that's taken, so it gets index_4_1
        self.assertEqual(entries[2]['field_id'], 'index_4_1')



class TestBufferSizeSufficiency(unittest.TestCase):
    """Verify that generated topics fit within C buffer sizes.

    If this test fails, the C buffer sizes in ha_discovery_manager.h
    need to be increased. The buffer constants below MUST match the
    actual C definitions — keep them in sync.

    Key buffers:
      - topic_buf[HA_DISCOVERY_TOPIC_BUF_SIZE]: holds the HA discovery topic
      - cleanup_topic_buf: stores full topic strings for republishing
      - field_id_buf[HA_DISCOVERY_FIELD_ID_BUF_SIZE]: holds the field_id slug
      - unique_id_buf[HA_DISCOVERY_UNIQUE_ID_BUF_SIZE]: holds the unique_id
      - device_id max 63 chars (configured_device_id_[64])
    """

    # These constants MUST match ha_discovery_manager.h — parsed from header.
    _HEADER = Path(__file__).resolve().parent.parent / 'components' / 'geappliances_bridge' / 'ha_discovery_manager.h'
    _HEADER_TEXT = _HEADER.read_text()
    TOPIC_BUF_SIZE = int(re.search(r'#define\s+HA_DISCOVERY_TOPIC_BUF_SIZE\s+(\d+)', _HEADER_TEXT).group(1))
    CLEANUP_TOPIC_BUF_SIZE = TOPIC_BUF_SIZE  # cleanup stores full topics, same bound
    FIELD_ID_BUF_SIZE = int(re.search(r'#define\s+HA_DISCOVERY_FIELD_ID_BUF_SIZE\s+(\d+)', _HEADER_TEXT).group(1))
    UNIQUE_ID_BUF_SIZE = int(re.search(r'#define\s+HA_DISCOVERY_UNIQUE_ID_BUF_SIZE\s+(\d+)', _HEADER_TEXT).group(1))
    DEVICE_ID_MAX = 63  # configured_device_id_[64] minus null terminator

    def setUp(self):
        self.entities = load_all_entities()

    def test_all_field_ids_fit_in_buffer(self):
        """Every field_id must fit in field_id_buf[72]."""
        for obj in self.entities:
            field_id = obj.get('fi', '')
            with self.subTest(entity=obj.get('n', '?'), erd=obj.get('i', '?')):
                self.assertLessEqual(
                    len(field_id), self.FIELD_ID_BUF_SIZE - 1,
                    f'field_id too long ({len(field_id)} chars, max {self.FIELD_ID_BUF_SIZE - 1}): '
                    f'{obj["n"]} erd={obj["i"]} field_id={field_id}')

    def test_all_topics_fit_in_publish_buffer(self):
        """Every generated topic must fit in topic_buf[192] with worst-case device_id.

        Uses the max device_id length (63 chars) to ensure the buffer is
        sufficient regardless of the actual device_id at runtime.
        """
        worst_device_id = 'A' * self.DEVICE_ID_MAX
        longest_topic = ''
        longest_len = 0

        for obj in self.entities:
            domain = obj.get('d', '')
            erd_id = obj.get('i', '')
            field_id = obj.get('fi', '')

            if field_id:
                topic = f'homeassistant/{domain}/{worst_device_id}/{erd_id}_{field_id}/config'
            else:
                topic = f'homeassistant/{domain}/{worst_device_id}/{erd_id}/config'

            if len(topic) > longest_len:
                longest_len = len(topic)
                longest_topic = topic

            with self.subTest(entity=obj.get('n', '?'), erd=obj.get('i', '?')):
                self.assertLessEqual(
                    len(topic), self.TOPIC_BUF_SIZE - 1,
                    f'topic too long ({len(topic)} chars, max {self.TOPIC_BUF_SIZE - 1}): '
                    f'{topic}')

        # Report the longest topic for reference
        print(f'Longest topic ({longest_len} chars): {longest_topic[:80]}...')

    def test_all_topics_fit_in_cleanup_buffer(self):
        """Every generated topic must fit in cleanup_topic_queue entries [192].

        This MUST match the publish buffer size — if cleanup can hold a topic
        but publish cannot (or vice versa), cleanup will silently fail to
        remove retained messages, creating an infinite re-discovery loop.
        """
        self.assertEqual(
            self.CLEANUP_TOPIC_BUF_SIZE, self.TOPIC_BUF_SIZE,
            f'cleanup_topic_queue buffer ({self.CLEANUP_TOPIC_BUF_SIZE}) must match '
            f'topic_buf ({self.TOPIC_BUF_SIZE})')

        worst_device_id = 'A' * self.DEVICE_ID_MAX

        for obj in self.entities:
            domain = obj.get('d', '')
            erd_id = obj.get('i', '')
            field_id = obj.get('fi', '')

            if field_id:
                topic = f'homeassistant/{domain}/{worst_device_id}/{erd_id}_{field_id}/config'
            else:
                topic = f'homeassistant/{domain}/{worst_device_id}/{erd_id}/config'

            with self.subTest(entity=obj.get('n', '?'), erd=obj.get('i', '?')):
                self.assertLessEqual(
                    len(topic), self.CLEANUP_TOPIC_BUF_SIZE - 1,
                    f'cleanup topic too long ({len(topic)} chars, max {self.CLEANUP_TOPIC_BUF_SIZE - 1}): '
                    f'{topic}')

    def test_all_unique_ids_fit_in_buffer(self):
        """Every unique_id must fit in unique_id_buf[160] with worst-case device_id."""
        worst_device_id = 'A' * self.DEVICE_ID_MAX

        for obj in self.entities:
            erd_id = obj.get('i', '')
            field_id = obj.get('fi', '')

            if field_id:
                unique_id = f'{worst_device_id}_erd_{erd_id}_{field_id}'
            else:
                unique_id = f'{worst_device_id}_erd_{erd_id}'

            with self.subTest(entity=obj.get('n', '?'), erd=obj.get('i', '?')):
                self.assertLessEqual(
                    len(unique_id), self.UNIQUE_ID_BUF_SIZE - 1,
                    f'unique_id too long ({len(unique_id)} chars, max {self.UNIQUE_ID_BUF_SIZE - 1}): '
                    f'{unique_id}')
if __name__ == '__main__':
    unittest.main()
