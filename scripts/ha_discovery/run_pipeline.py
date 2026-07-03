#!/usr/bin/env python3
"""Run the full HA discovery pipeline.

Orchestrates the sequence of scripts that convert source ERD definitions
into compressed HA discovery data. All entities are included in the output;
runtime filtering is controlled by the filter_config_topics config option.

  1. validate_json_format.py     - validate source JSON
  2. generate_flattened_review.py - flatten ERDs, one entry per sub-field
  3. auto_detect_ha_domain.py    - assign ha_domain
  4. auto_detect_device_class.py - assign device_class
  5. auto_detect_state_class.py  - assign state_class
  6. auto_detect_scaling.py      - infer scaling_factor
  7. auto_detect_pairings.py     - detect Request/Status pairs
  8. post_process.py             - fix cross-field consistency
  9. generate_ha_discovery.py    - produce ha_discovery/*.jsonl (all entities)
  10. compress_ha_discovery.py   - produce ha_discovery_data.h

Usage:
    python3 scripts/ha_discovery/run_pipeline.py [--reprocess]

--reprocess: Regenerate the processed JSON from scratch instead of using
             the committed appliance_api_erd_definitions_processed.json.
"""

import os
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = SCRIPT_DIR.parent.parent

PIPELINE = SCRIPT_DIR / "pipeline"
GENERATORS = SCRIPT_DIR / "generators"

SUBMODULE_DIR = REPO_ROOT / "lib" / "public-appliance-api-documentation"

SOURCE_ERD_JSON = SUBMODULE_DIR / "appliance_api_erd_definitions.json"
SOURCE_API_JSON = SUBMODULE_DIR / "appliance_api.json"
PROCESSED_JSON = REPO_ROOT / "appliance_api_erd_definitions_processed.json"

HA_DISCOVERY_DIR = REPO_ROOT / "ha_discovery"

# Steps 1-8: shared preprocessing
PREPROCESS_STEPS = [
    {
        "name": "Validate source JSON format",
        "script": PIPELINE / "validate_json_format.py",
        "args": ["--input", str(SOURCE_ERD_JSON)],
    },
    {
        "name": "Generate flattened review",
        "script": GENERATORS / "generate_flattened_review.py",
        "args": [
            "--input", str(SOURCE_ERD_JSON),
            "--api", str(SOURCE_API_JSON),
            "--output", str(PROCESSED_JSON),
        ],
    },
    {
        "name": "Auto-detect ha_domain",
        "script": PIPELINE / "auto_detect_ha_domain.py",
        "args": ["--input", str(PROCESSED_JSON), "--output", str(PROCESSED_JSON)],
    },
    {
        "name": "Auto-detect device_class",
        "script": PIPELINE / "auto_detect_device_class.py",
        "args": ["--input", str(PROCESSED_JSON), "--output", str(PROCESSED_JSON)],
    },
    {
        "name": "Auto-detect state_class",
        "script": PIPELINE / "auto_detect_state_class.py",
        "args": ["--input", str(PROCESSED_JSON), "--output", str(PROCESSED_JSON)],
    },
    {
        "name": "Auto-detect scaling",
        "script": PIPELINE / "auto_detect_scaling.py",
        "args": ["--input", str(PROCESSED_JSON), "--output", str(PROCESSED_JSON)],
    },
    {
        "name": "Auto-detect pairings",
        "script": PIPELINE / "auto_detect_pairings.py",
        "args": ["--input", str(PROCESSED_JSON), "--output", str(PROCESSED_JSON)],
    },
    {
        "name": "Post-process consistency",
        "script": PIPELINE / "post_process.py",
        "args": ["--input", str(PROCESSED_JSON)],
    },
]


def run_step(step):
    """Run a single pipeline step."""
    script = step["script"]
    if not script.exists():
        print(f"  ERROR: Script not found: {script}", file=sys.stderr)
        return False

    cmd = ["python3", str(script)] + step["args"]
    print(f"  Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=str(REPO_ROOT))
    if result.returncode != 0:
        print(f"  FAILED: {step['name']} (exit code {result.returncode})", file=sys.stderr)
        return False
    return True


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Run the full HA discovery pipeline.")
    parser.add_argument(
        "--reprocess",
        action="store_true",
        help="Regenerate processed JSON from scratch instead of using the committed file.",
    )
    args = parser.parse_args()

    # Check source files exist
    if not SOURCE_ERD_JSON.exists():
        print(f"Error: Source ERD definitions not found: {SOURCE_ERD_JSON}", file=sys.stderr)
        print("Run 'git submodule update --init' to fetch the submodule.", file=sys.stderr)
        sys.exit(1)

    if not SOURCE_API_JSON.exists():
        print(f"Error: Source API definitions not found: {SOURCE_API_JSON}", file=sys.stderr)
        sys.exit(1)

    print("HA Discovery Pipeline")
    print(f"  Source: {SOURCE_ERD_JSON}")
    print(f"  Processed: {PROCESSED_JSON}")
    print()

    # Step 1: Validate source JSON
    if not run_step(PREPROCESS_STEPS[0]):
        sys.exit(1)

    # Steps 2-8: Generate flattened review and auto-detect (only if --reprocess or missing)
    if args.reprocess or not PROCESSED_JSON.exists():
        if not run_step(PREPROCESS_STEPS[1]):
            sys.exit(1)
        for step in PREPROCESS_STEPS[2:]:
            if not run_step(step):
                sys.exit(1)
    else:
        print(f"  Using existing processed file: {PROCESSED_JSON}")

    # Step 9: Generate JSONL (all entities, no filtering - done at runtime)
    gen_step = {
        "name": "Generate HA discovery JSONL",
        "script": GENERATORS / "generate_ha_discovery.py",
        "args": ["--processed", str(PROCESSED_JSON), "--output-dir", str(HA_DISCOVERY_DIR), "--no-filter"],
    }
    if not run_step(gen_step):
        sys.exit(1)

    # Step 10: Compress
    comp_step = {
        "name": "Compress HA discovery",
        "script": GENERATORS / "compress_ha_discovery.py",
        "args": ["--input-dir", str(HA_DISCOVERY_DIR), "--header-name", "ha_discovery_data"],
    }
    if not run_step(comp_step):
        sys.exit(1)

    print()
    print("Pipeline complete!")
    print(f"  JSONL files:   {HA_DISCOVERY_DIR}")
    print(f"  Compressed header: {REPO_ROOT / 'components' / 'geappliances_bridge' / 'ha_discovery_data.h'}")


if __name__ == "__main__":
    main()