#!/usr/bin/env python3
"""
Unit tests for pipeline processing functions.

Covers:
  - apply_overrides: offset-based keys, bare erd_id fallback, None guard,
    type validation, idempotency
  - auto_detect_scaling non-numeric field guard
  - apply_post_processing rules

Run with:
    python3 -m pytest scripts/test_pipeline.py -v
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "ha_discovery" / "pipeline"))
import auto_detect_scaling as scaling
from post_process import apply_overrides, apply_post_processing


class TestApplyOverrides(unittest.TestCase):
    """Test apply_overrides with mock entries."""

    def _entry(self, erd_id, field_offset, **review):
        """Create a mock entry dict."""
        return {
            "erd_id": erd_id,
            "field_offset": field_offset,
            "field_name": "Test Field",
            "field_type": "u16",
            "review": review,
        }

    def test_offset_based_key_match(self):
        """erd_id:offset key matches only the target offset."""
        entries = [
            self._entry("0x3015", 0),
            self._entry("0x3015", 2),
            self._entry("0x3015", 3),
        ]
        applied = apply_overrides(entries)
        # Only offset 0 should match "0x3015:0".
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "gal/min")
        self.assertEqual(entries[0]["review"]["scaling_factor"], 10000)
        # Offset 2 and 3 should not get the override.
        self.assertIsNone(entries[1]["review"].get("unit_of_measurement"))
        self.assertIsNone(entries[2]["review"].get("unit_of_measurement"))
        self.assertEqual(applied, 2)  # unit + scaling_factor

    def test_none_offset_falls_through_to_bare(self):
        """field_offset=None skips offset-based key, falls to bare erd_id."""
        entries = [
            self._entry("0x7130", None),
        ]
        applied = apply_overrides(entries)
        self.assertEqual(entries[0]["review"]["ha_domain"], "sensor")
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "rpm")
        self.assertEqual(applied, 2)

    def test_string_offset_treated_as_none(self):
        """field_offset as string is normalized to None, falls to bare erd_id."""
        entries = [
            self._entry("0x7130", "0"),
        ]
        applied = apply_overrides(entries)
        self.assertEqual(entries[0]["review"]["ha_domain"], "sensor")
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "rpm")
        self.assertEqual(applied, 2)

    def test_bare_override_applies_to_all_fields(self):
        """Bare erd_id override applies to every field of that ERD."""
        entries = [
            self._entry("0x7130", 0),
            self._entry("0x7130", 1),
        ]
        applied = apply_overrides(entries)
        # Both fields get the override.
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "rpm")
        self.assertEqual(entries[1]["review"]["unit_of_measurement"], "rpm")
        self.assertEqual(applied, 4)  # 2 fields * 2 keys

    def test_offset_key_takes_precedence_over_bare(self):
        """When both erd_id:offset and erd_id exist, offset key wins for that field."""
        entries = [
            self._entry("0x404c", 0),
            self._entry("0x404c", 4),
        ]
        applied = apply_overrides(entries)
        # Offset 0 matches "0x404c:0" with force_classification.
        self.assertEqual(entries[0]["review"]["force_classification"], "single")
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "g")
        # Offset 4 has no bare "0x404c" override, so it gets nothing.
        self.assertIsNone(entries[1]["review"].get("force_classification"))
        self.assertIsNone(entries[1]["review"].get("unit_of_measurement"))

    def test_idempotency(self):
        """Calling apply_overrides twice returns 0 on second call."""
        entries = [self._entry("0x3015", 0)]
        applied1 = apply_overrides(entries)
        self.assertGreater(applied1, 0)
        applied2 = apply_overrides(entries)
        self.assertEqual(applied2, 0)

    def test_no_match_for_unknown_erd(self):
        """Entries for ERDs not in OVERRIDES are untouched."""
        entries = [self._entry("0xFFFF", 0)]
        applied = apply_overrides(entries)
        self.assertEqual(applied, 0)
        self.assertEqual(entries[0]["review"], {})

    def test_offset_key_does_not_match_wrong_offset(self):
        """erd_id:0 does not match field at offset 2."""
        entries = [self._entry("0x3015", 2)]
        applied = apply_overrides(entries)
        self.assertEqual(applied, 0)
        self.assertEqual(entries[0]["review"], {})

    def test_offset_key_with_negative_offset(self):
        """Negative offset is valid int, constructs key, no match."""
        entries = [self._entry("0x3015", -1)]
        applied = apply_overrides(entries)
        self.assertEqual(applied, 0)

    def test_existing_review_values_preserved(self):
        """Override only changes values that differ; other review keys stay."""
        entries = [
            self._entry("0x3015", 0, ha_domain="sensor", device_class="flow"),
        ]
        applied = apply_overrides(entries)
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "gal/min")
        self.assertEqual(entries[0]["review"]["scaling_factor"], 10000)
        # Pre-existing values are preserved.
        self.assertEqual(entries[0]["review"]["ha_domain"], "sensor")
        self.assertEqual(entries[0]["review"]["device_class"], "flow")


class TestNonNumericFieldGuard(unittest.TestCase):
    """Test auto_detect_scaling clears stale units on non-numeric fields."""

    def _entry(self, field_type, **review):
        return {
            "field_name": "Test Field",
            "field_type": field_type,
            "field_bits": None,
            "review": review,
        }

    def test_enum_stale_unit_cleared(self):
        """Enum field with stale unit_of_measurement gets it cleared."""
        entries = [self._entry("enum", unit_of_measurement="gal/min")]
        scaling.apply_detection(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])

    def test_enum_stale_scaling_cleared(self):
        """Enum field with stale scaling_factor gets it cleared."""
        entries = [self._entry("enum", scaling_factor=10000)]
        scaling.apply_detection(entries)
        self.assertIsNone(entries[0]["review"]["scaling_factor"])

    def test_string_stale_unit_cleared(self):
        """String field with stale unit gets it cleared."""
        entries = [self._entry("string", unit_of_measurement="rpm")]
        scaling.apply_detection(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])

    def test_numeric_field_not_cleared(self):
        """Numeric fields are not affected by the non-numeric guard."""
        # Use a field name the detector recognizes so it doesn't clear as stale.
        entry = {
            "field_name": "CLC Temperature",
            "field_type": "u16",
            "field_bits": None,
            "review": {"unit_of_measurement": "gal/min"},
        }
        scaling.apply_detection([entry])
        # The detector overwrites with its own detected unit (°C), not gal/min.
        self.assertNotEqual(entry["review"]["unit_of_measurement"], "gal/min")

    def test_enum_with_no_stale_values_is_noop(self):
        """Enum field with no stale values is a no-op."""
        entries = [self._entry("enum")]
        scaling.apply_detection(entries)
        self.assertEqual(entries[0]["review"], {})

    def test_bitfield_stale_unit_cleared(self):
        """Bitfield field with stale unit gets it cleared."""
        entries = [self._entry("bitfield", unit_of_measurement="steps")]
        scaling.apply_detection(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])


class TestApplyPostProcessing(unittest.TestCase):
    """Test apply_post_processing rules."""

    def _entry(self, ha_domain, **review):
        return {
            "erd_id": "0xFFFF",
            "field_offset": 0,
            "field_name": "Test",
            "field_type": "u8",
            "review": {
                "ha_domain": ha_domain,
                "device_class": None,
                "unit_of_measurement": None,
                "scaling_factor": 1,
                "state_class": None,
                **review,
            },
        }

    def test_binary_sensor_unit_cleared(self):
        """Rule 1: binary_sensor with unit_of_measurement gets it cleared."""
        entries = [self._entry("binary_sensor", unit_of_measurement="W")]
        cleared_unit, _, _, _ = apply_post_processing(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])
        self.assertGreater(cleared_unit, 0)

    def test_switch_unit_cleared(self):
        """Rule 1: switch with unit_of_measurement gets it cleared."""
        entries = [self._entry("switch", unit_of_measurement="W")]
        cleared_unit, _, _, _ = apply_post_processing(entries)
        self.assertIsNone(entries[0]["review"]["unit_of_measurement"])
        self.assertGreater(cleared_unit, 0)

    def test_sensor_unit_not_cleared(self):
        """Rule 1: sensor with unit_of_measurement is NOT cleared."""
        entries = [self._entry("sensor", unit_of_measurement="W")]
        cleared_unit, _, _, _ = apply_post_processing(entries)
        self.assertEqual(entries[0]["review"]["unit_of_measurement"], "W")
        self.assertEqual(cleared_unit, 0)

    def test_scaling_factor_zero_fixed(self):
        """Rule 4: scaling_factor=0 is fixed to 1."""
        entries = [self._entry("sensor", scaling_factor=0)]
        _, _, _, fixed = apply_post_processing(entries)
        self.assertEqual(entries[0]["review"]["scaling_factor"], 1)
        self.assertEqual(fixed, 1)

    def test_scaling_factor_one_not_touched(self):
        """Rule 4: scaling_factor=1 is not changed."""
        entries = [self._entry("sensor", scaling_factor=1)]
        _, _, _, fixed = apply_post_processing(entries)
        self.assertEqual(entries[0]["review"]["scaling_factor"], 1)
        self.assertEqual(fixed, 0)


if __name__ == "__main__":
    unittest.main()