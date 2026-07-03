#!/usr/bin/env python3
"""
Shared utilities for all validator scripts.
Provides consistent error/warning emission, CI detection, and common helper functions.
"""

import os
import re

from ha_constants import UNIT_KEYWORD_MAP, DEVICE_CLASS_KEYWORDS, DEVICE_CLASS_EXCLUSIONS

IN_GITHUB_ACTIONS = os.environ.get('GITHUB_ACTIONS') == 'true'


def emit_error(message: str, file: str = "", line: int = 0) -> None:
    """Emit an error message, using GitHub Actions annotation format if in CI."""
    if IN_GITHUB_ACTIONS:
        if file and line:
            print(f"::error file={file},line={line}::{message}")
        else:
            print(f"::error::{message}")
    else:
        print(f"  ERROR: {message}")


def emit_warning(message: str, file: str = "", line: int = 0) -> None:
    """Emit a warning message, using GitHub Actions annotation format if in CI."""
    if IN_GITHUB_ACTIONS:
        if file and line:
            print(f"::warning file={file},line={line}::{message}")
        else:
            print(f"::warning::{message}")
    else:
        print(f"  WARNING: {message}")


def is_reserved_field(name: str) -> bool:
    return name.lower().startswith("reserved")


def extract_unit_hint(field_name: str) -> str:
    m = re.search(r'\(([^)]+)\)', field_name)
    if m:
        hint = m.group(1).strip().lower()
        hint = re.sub(r'x\s*\d+', '', hint).strip()
        return hint
    m = re.search(r'in\s+(fahrenheit|celsius)\s*(?:x\s*\d+)?', field_name, re.IGNORECASE)
    if m:
        return m.group(1).lower()
    return ''


def get_field_unit(hint: str) -> str:
    for keyword, unit in UNIT_KEYWORD_MAP.items():
        if keyword in hint:
            return unit
    return ''


def detect_device_class_from_name(name: str) -> str:
    name_lower = name.lower()
    # Skip hash fields entirely (sha256, sha1, etc.)
    if "sha" in name_lower:
        return ""
    for device_class, keywords in DEVICE_CLASS_KEYWORDS.items():
        for keyword in keywords:
            if keyword in name_lower:
                exclusions = DEVICE_CLASS_EXCLUSIONS.get(device_class, [])
                if any(excl in name_lower for excl in exclusions):
                    continue
                return device_class
    return ""
