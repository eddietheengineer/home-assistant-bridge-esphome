# Scripts

This directory contains utility scripts for the home-assistant-bridge-esphome project.

## generate_erd_lists.py

Generates the `components/geappliances_bridge/erd_lists.h` file from the ERD definitions in the `public-appliance-api-documentation` library.

### Usage

The script is automatically run during the build process via the Makefile. To manually regenerate the header file:

```bash
python3 scripts/generate_erd_lists.py
```

### Requirements

- Python 3.6+
- Git submodules initialized (`git submodule update --init --recursive`)

### What it does

The script:
1. Reads ERD definitions from `lib/public-appliance-api-documentation/appliance_api_erd_definitions.json`
2. Categorizes ERDs into appliance types based on their hex address ranges:
   - `0x0000-0x0FFF`: Common ERDs (all appliance types)
   - `0x1000-0x1FFF`: Refrigeration ERDs
   - `0x2000-0x2FFF`: Laundry ERDs
   - `0x3000-0x3FFF`: Dishwasher ERDs
   - `0x4000-0x4FFF`: Water heater ERDs
   - `0x5000-0x5FFF`: Range ERDs (stoves, cooktops, ovens)
   - `0x7000-0x7FFF`: Air conditioning ERDs (mini split, Zoneline, etc.)
   - `0x8000-0x8FFF`: Water filter ERDs
   - `0x9000-0x9FFF`: Small appliance ERDs (coffee makers, etc.)
   - `0xD000-0xDFFF`: Energy ERDs (all appliance types)
3. Generates C arrays for each category with sorted ERD values
4. Creates the appliance type to ERD list translation table
5. Writes the complete header file to `components/geappliances_bridge/erd_lists.h`
6. Calculates the maximum possible polling list size and writes `#define POLLING_LIST_MAX_SIZE`

### Note

The generated `erd_lists.h` file should not be manually edited. It is regenerated automatically during the build process.

## ha_discovery/

Pipeline for generating Home Assistant MQTT Discovery JSONL files and compressing them into `ha_discovery_data.h`. See [scripts/ha_discovery/README.md](ha_discovery/README.md) for details.

Run the full pipeline manually:

```bash
python3 scripts/ha_discovery/run_pipeline.py
```