#!/usr/bin/env python3
"""
validate_json_format.py

Validates that appliance_api_erd_definitions.json follows the non-standard
compact JSON format defined in AGENTS.md.

Checks:
  1. Valid JSON
  2. No escaped non-ASCII (\\uXXXX) — must be literal UTF-8
  3. 2-space indentation per level
  4. Top-level structure is an object with "erds" array

Usage:
    python3 scripts/validate_json_format.py
"""

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from ha_constants import ERD_DEFINITIONS_FILE
from validator_utils import emit_error


def validate_format(input_file=None) -> list[str]:
    """Validate the JSON file format and return a list of errors."""
    errors: list[str] = []
    target = Path(input_file) if input_file else ERD_DEFINITIONS_FILE
    try:
        raw = target.read_text(encoding="utf-8")
    except (FileNotFoundError, OSError) as e:
        errors.append(f"Cannot read {target}: {e}")
        return errors

    # 1. Valid JSON
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        errors.append(f"Invalid JSON: {e}")
        return errors

    # 2. No escaped non-ASCII
    for i, line in enumerate(raw.splitlines(), 1):
        m = re.search(r'\\u[0-9a-fA-F]{4}', line)
        if m:
            errors.append(
                f"L{i}: escaped non-ASCII '{m.group()}' — use literal UTF-8"
            )

    # 3. Check indentation is 2-space based
    for i, line in enumerate(raw.splitlines(), 1):
        if not line.strip():
            continue
        leading = len(line) - len(line.lstrip())
        if leading > 0 and leading % 2 != 0:
            errors.append(
                f"L{i}: odd indentation ({leading} spaces) — must be multiple of 2"
            )

    # 4. Verify top-level structure
    if not isinstance(data, dict):
        errors.append("Top-level value must be an object")
    elif "erds" not in data:
        errors.append("Missing required top-level key 'erds'")
    elif not isinstance(data.get("erds"), list):
        errors.append("'erds' must be an array")

    return errors

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Validate JSON format.")
    parser.add_argument("--input", help="Path to JSON file (default: appliance_api_erd_definitions.json)")
    args = parser.parse_args()
    errors = validate_format(args.input)
    if errors:
        for e in errors:
            emit_error(e, file=str(ERD_DEFINITIONS_FILE))
        sys.exit(1)
    else:
        print("JSON format validation passed")


if __name__ == "__main__":
    main()
