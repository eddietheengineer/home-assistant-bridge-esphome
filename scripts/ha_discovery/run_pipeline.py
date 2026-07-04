#!/usr/bin/env python3
"""Regenerate all HA discovery artifacts in sequence.

Usage:
    python3 scripts/ha_discovery/run_pipeline.py

Steps:
    1. Run auto_detect_scaling on the processed JSON (in-place).
    2. Post-process (reapply overrides).
    3. Generate JSONL files to ha_discovery/.
    4. Compress filtered JSONL into ha_discovery_data.h.
    5. Compress unfiltered JSONL into ha_discovery_data_unfiltered.inc.
"""

import subprocess
import sys
from pathlib import Path


def main():
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent.parent
    generators = script_dir / "generators"
    pipeline = script_dir / "pipeline"
    processed = repo_root / "appliance_api_erd_definitions_processed.json"
    ha_dir = repo_root / "ha_discovery"

    def run(cmd, **kwargs):
        print(f"  {' '.join(str(c) for c in cmd)}", file=sys.stderr)
        subprocess.run(cmd, check=True, **kwargs)

    # Step 1: Auto-detect scaling
    print("Step 1: Auto-detect scaling...", file=sys.stderr)
    run([sys.executable, str(pipeline / "auto_detect_scaling.py"),
         "--input", str(processed), "--output", str(processed)])

    # Step 2: Post-process (reapply overrides)
    print("Step 2: Post-process...", file=sys.stderr)
    run([sys.executable, str(pipeline / "post_process.py"),
         "--input", str(processed), "--output", str(processed)])

    # Step 3: Generate JSONL
    print("Step 3: Generate JSONL...", file=sys.stderr)
    run([sys.executable, str(generators / "generate_ha_discovery.py"),
         "--processed", str(processed),
         "--output-dir", str(ha_dir)])

    # Step 4: Compress filtered header
    print("Step 4: Compress filtered header...", file=sys.stderr)
    run([sys.executable, str(generators / "compress_ha_discovery.py"),
         "--input-dir", str(ha_dir),
         "--header-name", "ha_discovery_data"])

    # Step 5: Compress unfiltered header from same JSONL
    print("Step 5: Compress unfiltered header...", file=sys.stderr)
    run([sys.executable, str(generators / "compress_ha_discovery.py"),
         "--input-dir", str(ha_dir),
         "--header-name", "ha_discovery_data_unfiltered",
         "--extension", ".inc"])

    print("Done!", file=sys.stderr)


if __name__ == "__main__":
    main()