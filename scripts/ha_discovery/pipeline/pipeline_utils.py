#!/usr/bin/env python3
"""Shared utilities for pipeline scripts.

Provides common constants and helpers to avoid duplication across
auto-detection scripts. Each script should import from this module
instead of defining its own copies.
"""

import json
import os
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = SCRIPT_DIR.parent.parent


def load_json(path):
    """Load a JSON file and return the parsed object."""
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def save_json(path, data):
    """Save data as formatted JSON to a file."""
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write('\n')