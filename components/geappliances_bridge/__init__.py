"""ESPHome component for GE Appliances Bridge."""
from __future__ import annotations

import json
import logging
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, sensor, uart
from esphome.const import CONF_ID, CONF_STATE_CLASS
from esphome.core import EnumValue, ID

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@joshualongenecker"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "button"]

# UART configuration keys
CONF_GEA3_UART_ID = "gea3_uart_id"
CONF_GEA2_UART_ID = "gea2_uart_id"

# Bridge (MQTT) configuration keys
CONF_ADAPTER_ADDRESS = "adapter_address"
CONF_DEVICE_ID = "device_id"
CONF_MODE = "mode"
CONF_POLLING_INTERVAL = "polling_interval"
CONF_POLLING_ONLY_PUBLISH_ON_CHANGE = "polling_onlypublish_onchange"
CONF_APPLIANCE_API_PARSING = "appliance_api_parsing"
CONF_CUSTOM_ERDS = "custom_erds"
CONF_GENERATE_DEVICE_CONFIG = "generate_device_config"
CONF_ERD_PUBLISH_RATE_SENSOR = "erd_publish_rate_sensor"
CONF_ERD_CACHE_ENTRIES_SENSOR = "erd_cache_entries_sensor"
CONF_ERD_CACHE_UPDATES_SENSOR = "erd_cache_updates_sensor"
CONF_MQTT_PUBLISH_RATE_SENSOR = "mqtt_publish_rate_sensor"
CONF_THROTTLE_RATE_SECONDS = "throttle_rate_seconds"
CONF_FILTER_CONFIG_TOPICS = "filter_config_topics"
CONF_DISCOVERY_REFRESH_BUTTON = "discovery_refresh_button"



# Bridge mode options (polling vs subscriptions)
MODE_POLL = "poll"
MODE_SUBSCRIBE = "subscribe"
MODE_AUTO = "auto"

# Mode enum values (must match BridgeMode enum in C++)
MODE_POLL_VALUE = 0
MODE_SUBSCRIBE_VALUE = 1
MODE_AUTO_VALUE = 2

geappliances_bridge_ns = cg.esphome_ns.namespace("geappliances_bridge")
GeappliancesBridge = geappliances_bridge_ns.class_(
    "GeappliancesBridge", cg.Component
)

# Concrete button subclass for discovery refresh (Button is abstract)
DiscoveryRefreshButton = geappliances_bridge_ns.class_(
    "DiscoveryRefreshButton", button.Button
)


def _make_state_class(value: str):
    """Wrap a state_class string as an EnumValue for codegen."""
    v = cv.add_class_to_obj(value, EnumValue)
    v.enum_value = sensor.STATE_CLASSES[value]
    return v



def sanitize_appliance_name(name: str) -> str:
    """Sanitize appliance type name for use in C++ identifiers.

    Replaces special characters with readable equivalents and removes
    any non-alphanumeric characters to create valid C++ identifiers.

    Args:
        name: The appliance type name to sanitize

    Returns:
        A sanitized string suitable for use as a C++ identifier
    """
    # Replace special characters with more readable equivalents
    replacements = {
        ' ': '',
        '/': '',
        '&': 'And',
        '-': '',
        '(': '',
        ')': '',
    }
    
    result = name
    for old, new in replacements.items():
        result = result.replace(old, new)
    
    # Remove any remaining non-alphanumeric characters
    result = re.sub(r'[^a-zA-Z0-9]', '', result)
    return result


def _find_json_paths(component_dir: str) -> list[tuple[str, str]]:
    """Search for JSON files from the public-appliance-api-documentation library.

    Returns a list of (location_name, resolved_path) tuples for both
    appliance_api_erd_definitions.json and appliance_api.json, in search order.
    Only paths that actually exist on disk are returned.
    """
    results: list[tuple[str, str]] = []
    seen: set[str] = set()

    for filename in ("appliance_api_erd_definitions.json", "appliance_api.json"):
        # Path 1: Local submodule (for local development)
        p = os.path.normpath(os.path.join(
            component_dir, "..", "..", "lib", "public-appliance-api-documentation", filename))
        if p not in seen and os.path.exists(p):
            results.append(("local submodule", p))
            seen.add(p)

        # Path 2: ESPHome library cache in user home
        p = os.path.join(os.path.expanduser("~"), ".esphome", "external_files", "libraries",
                         "public-appliance-api-documentation", filename)
        if p not in seen and os.path.exists(p):
            results.append(("ESPHome cache (home)", p))
            seen.add(p)

        # Path 3: ESPHome library cache in /config (Home Assistant add-on)
        p = os.path.join("/config", ".esphome", "external_files", "libraries",
                         "public-appliance-api-documentation", filename)
        if p not in seen and os.path.exists(p):
            results.append(("ESPHome cache (/config)", p))
            seen.add(p)

        # Path 3b: ESPHome library cache in /data (Docker container)
        p = os.path.join("/data", ".esphome", "external_files", "libraries",
                         "public-appliance-api-documentation", filename)
        if p not in seen and os.path.exists(p):
            results.append(("ESPHome cache (/data)", p))
            seen.add(p)

        # Path 4: ESPHome library cache relative to component
        p = os.path.normpath(os.path.join(
            component_dir, "..", "..", ".esphome", "external_files", "libraries",
            "public-appliance-api-documentation", filename))
        if p not in seen and os.path.exists(p):
            results.append(("ESPHome cache (relative)", p))
            seen.add(p)

        # Path 5: Parent lib directory
        parent_dir = os.path.dirname(os.path.dirname(component_dir))
        p = os.path.normpath(os.path.join(
            parent_dir, "lib", "public-appliance-api-documentation", filename))
        if p not in seen and os.path.exists(p):
            results.append(("parent library path", p))
            seen.add(p)

    return results

def load_appliance_types() -> dict[int, str]:
    """Load appliance type mappings from the API documentation library.

    Tries multiple locations to find the appliance type definitions JSON,
    falling back to GitHub if no local copy is available.

    Returns:
        Dictionary mapping appliance type IDs (int) to names (str)
    """
    component_dir = os.path.dirname(__file__)
    data = None

    for location_name, json_path in _find_json_paths(component_dir):
        if not json_path.endswith("appliance_api_erd_definitions.json"):
            continue
        try:
            with open(json_path, 'r') as f:
                data = json.load(f)
            _LOGGER.info("Loaded appliance types from %s: %s", location_name, json_path)
            break
        except Exception as e:
            _LOGGER.warning("Failed to load from %s (%s): %s", location_name, json_path, str(e))

    # If local paths failed, try fetching from GitHub as fallback
    if data is None:
        url = "https://raw.githubusercontent.com/eddietheengineer/public-appliance-api-documentation/main/appliance_api_erd_definitions.json"
        _LOGGER.info("Fetching ERD definitions from GitHub: %s", url)

        try:
            with urllib.request.urlopen(url, timeout=5) as response:
                data = json.loads(response.read().decode('utf-8'))
            _LOGGER.info("Successfully fetched appliance types from GitHub (fallback)")
        except urllib.error.HTTPError as e:
            _LOGGER.error(
                "HTTP error fetching appliance API documentation (status %d): %s. Using fallback mapping.",
                e.code, str(e)
            )
            return {
                0: "Unknown",
                255: "Unknown"
            }
        except urllib.error.URLError as e:
            _LOGGER.error(
                "Network error fetching appliance API documentation: %s. Using fallback mapping.",
                str(e.reason)
            )
            return {
                0: "Unknown",
                255: "Unknown"
            }
        except Exception as e:
            _LOGGER.error(
                "Unexpected error fetching appliance API documentation: %s. Using fallback mapping.",
                str(e)
            )
            return {
                0: "Unknown",
                255: "Unknown"
            }

    # Parse the data
    try:
        # Find the ERD with id "0x0008" (Appliance Type)
        for erd in data.get("erds", []):
            if erd.get("id") == "0x0008":
                # Extract the enum values
                erd_data = erd.get("data", [])
                if erd_data and erd_data[0].get("type") == "enum":
                    values = erd_data[0].get("values", {})
                    # Convert string keys to integers and sanitize values for C++
                    mapping = {}
                    for key, value in values.items():
                        int_key = int(key)
                        sanitized = sanitize_appliance_name(value)
                        mapping[int_key] = sanitized

                    _LOGGER.info("Loaded %d appliance type mappings", len(mapping))
                    return mapping
    except Exception as e:
        _LOGGER.error("Failed to parse appliance types: %s", str(e))

    # Fallback mapping
    _LOGGER.warning("Using fallback appliance type mapping")
    return {
        0: "Unknown",
        255: "Unknown"
    }


def generate_appliance_type_function(appliance_types: dict[int, str]) -> str:
    """Generate C++ code for the appliance type to string function."""
    # Generate switch cases with consistent indentation
    cases = []
    for type_id, type_name in sorted(appliance_types.items()):
        cases.append(f'    case {type_id}: return "{type_name}";')
    
    cases_str = "\n".join(cases)
    
    # Generate the function with consistent 2-space indentation
    function_code = f'''
std::string appliance_type_to_string(uint8_t appliance_type) {{
  // Auto-generated from public-appliance-api-documentation
  // ERD 0x0008 - Appliance Type enum mapping
  switch (appliance_type) {{
{cases_str}
    default: return "Unknown";
  }}
}}
'''
    return function_code

def validate_at_least_one_uart(config: dict[str, Any]) -> dict[str, Any]:
    """Validate that at least one UART ID is specified.

    Args:
        config: Configuration dictionary

    Returns:
        The validated configuration

    Raises:
        cv.Invalid: If neither gea3_uart_id nor gea2_uart_id is specified
    """
    if CONF_GEA3_UART_ID not in config and CONF_GEA2_UART_ID not in config:
        raise cv.Invalid("At least one of gea3_uart_id or gea2_uart_id must be specified")
    return config


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GeappliancesBridge),
        cv.Optional(CONF_GEA3_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_GEA2_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_ADAPTER_ADDRESS, default=0xE4): cv.int_range(min=0x00, max=0xFF),
        cv.Optional(CONF_DEVICE_ID): cv.string,
        cv.Optional(CONF_MODE, default=MODE_AUTO): cv.enum(
            {
                MODE_POLL: MODE_POLL_VALUE,
                MODE_SUBSCRIBE: MODE_SUBSCRIBE_VALUE,
                MODE_AUTO: MODE_AUTO_VALUE,
            },
            upper=False
        ),
        cv.Optional(CONF_POLLING_INTERVAL, default=10000): cv.positive_int,
        cv.Optional(CONF_POLLING_ONLY_PUBLISH_ON_CHANGE, default=True): cv.boolean,
        cv.Optional(CONF_APPLIANCE_API_PARSING, default=True): cv.boolean,
        cv.Optional(CONF_GENERATE_DEVICE_CONFIG, default=False): cv.boolean,
        cv.Optional(CONF_CUSTOM_ERDS, default=[]): cv.ensure_list(
            cv.int_range(min=0, max=0xFFFF)
        ),
        cv.Optional(CONF_THROTTLE_RATE_SECONDS, default=0): cv.int_range(min=0, max=255),
        cv.Optional(CONF_ERD_PUBLISH_RATE_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="ERD Publish Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_ERD_CACHE_ENTRIES_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema().extend(cv.Schema({
                cv.Optional("name", default="ERD Cache Entries"): cv.string,
            })),
        ),
        cv.Optional(CONF_ERD_CACHE_UPDATES_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="ERD Cache Update Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_MQTT_PUBLISH_RATE_SENSOR, default=True): cv.Any(
            cv.boolean,
            sensor.sensor_schema(state_class="measurement").extend(cv.Schema({
                cv.Optional("name", default="MQTT Publish Rate"): cv.string,
            })),
        ),
        cv.Optional(CONF_FILTER_CONFIG_TOPICS, default=True): cv.boolean,
        cv.Optional(CONF_DISCOVERY_REFRESH_BUTTON, default=True): cv.Any(
            cv.boolean,
            button.button_schema(DiscoveryRefreshButton).extend(cv.Schema({
                cv.Optional("name", default="Discovery Refresh"): cv.string,
            })),
        ),
    }
).extend(cv.COMPONENT_SCHEMA)
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_at_least_one_uart)


async def to_code(config: dict[str, Any]) -> None:
    """Generate C++ code for the component.

    Args:
        config: Configuration dictionary
    """
    # Add library dependencies (pinned to specific commit SHAs to prevent
    # silent breakage from upstream branch movement)
    cg.add_library("https://github.com/ryanplusplus/tiny#3747b6ff65eec4b38367c3c8fa94e6ed2a1ccf35", None)
    cg.add_library("https://github.com/geappliances/tiny-gea-api#4fa8fee8297e24baa91bfe4a464088a73e7c6a5a", None)
    cg.add_library("https://github.com/geappliances/public-appliance-api-documentation#6f567e49c84bec838bf84036d0915398f0c2eb63", None)
    
    var = cg.new_Pvariable(config[CONF_ID])
    # Deprecation warning for polling_onlypublish_onchange
    if CONF_POLLING_ONLY_PUBLISH_ON_CHANGE in config:
        _LOGGER.warning(
            "polling_onlypublish_onchange is deprecated and will be removed in a future release. "
            "The component now always publishes only on change."
)
    # Generate required headers from appliance API documentation.
    # erd_lists.h and appliance_api_feature_lists.h are always required.
    # generate_erd_lists.py also calls generate_ha_discovery.py as a side effect.
    component_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.normpath(os.path.join(component_dir, "..", ".."))
    scripts_dir = os.path.join(repo_root, "scripts")

    # Resolve JSON file paths using the same multi-path search as load_appliance_types().
    # This is critical for ESPHome external component builds where the repo is copied
    # to a cache directory and git submodules are not initialized.
    erd_defs_path = None
    api_json_path = None
    for location_name, json_path in _find_json_paths(component_dir):
        if json_path.endswith("appliance_api_erd_definitions.json"):
            erd_defs_path = json_path
        elif json_path.endswith("appliance_api.json"):
            api_json_path = json_path

    cmd = [sys.executable, os.path.join(scripts_dir, "generate_erd_lists.py"),
           "--component-dir", component_dir]
    if erd_defs_path:
        cmd.extend(["--erd-definitions", erd_defs_path])
    if api_json_path:
        cmd.extend(["--appliance-api", api_json_path])
    if not config.get(CONF_FILTER_CONFIG_TOPICS, True):
        cmd.append("--no-filter-config-topics")

    try:
        _LOGGER.info("Generating ERD lists and feature API lists...")
        result = subprocess.run(
            cmd,
            cwd=repo_root,
            check=True,
            capture_output=True,
            text=True,
        )
        _LOGGER.info("ERD lists generated successfully")
    except subprocess.CalledProcessError as e:
        _LOGGER.error(
            "ERD lists generation failed: %s. "
            "Build will fail without erd_lists.h and appliance_api_feature_lists.h.",
            e.stderr if e.stderr else str(e)
        )
        raise
    except FileNotFoundError as e:
        _LOGGER.error(
            "ERD lists generation script not found: %s. "
            "Build will fail without erd_lists.h and appliance_api_feature_lists.h.",
            str(e)
        )
        raise

    # HA discovery compression is now handled by generate_erd_lists.py
    # (in-process, after JSONL generation). No separate step needed.
    await cg.register_component(var, config)
    # Ensure USE_ESP_IDF is defined for ESP-IDF builds so that
    # platform-specific code in our component compiles correctly.
    cg.add_build_flag("-DUSE_ESP_IDF")

    # Get optional GEA3 UART component reference
    if CONF_GEA3_UART_ID in config:
        gea3_uart_component = await cg.get_variable(config[CONF_GEA3_UART_ID])
        cg.add(var.set_gea3_uart(gea3_uart_component))

    # Get optional GEA2 UART component reference
    if CONF_GEA2_UART_ID in config:
        gea2_uart_component = await cg.get_variable(config[CONF_GEA2_UART_ID])
        cg.add(var.set_gea2_uart(gea2_uart_component))

    # Set device ID if provided, otherwise it will be auto-generated
    if CONF_DEVICE_ID in config:
        cg.add(var.set_device_id(config[CONF_DEVICE_ID]))

    # Set adapter address (defaults to 0xE4)
    cg.add(var.set_client_address(config[CONF_ADAPTER_ADDRESS]))

    # Set bridge mode configuration (config[CONF_MODE] is now an integer from cv.enum)
    cg.add(var.set_mode(config[CONF_MODE]))
    cg.add(var.set_polling_interval(config[CONF_POLLING_INTERVAL]))
    cg.add(var.set_appliance_api_parsing(config[CONF_APPLIANCE_API_PARSING]))
    cg.add(var.set_generate_device_config(config[CONF_GENERATE_DEVICE_CONFIG]))
    cg.add(var.set_throttle_rate_seconds(config[CONF_THROTTLE_RATE_SECONDS]))
    cg.add(var.set_filter_config_topics(config[CONF_FILTER_CONFIG_TOPICS]))


    # Create diagnostic sensors (auto-created by default, set to false to disable)
    val = config.get(CONF_ERD_PUBLISH_RATE_SENSOR, True)
    if val is not False:
        if val is True:
            val = {
                "name": "ERD Publish Rate",
                CONF_ID: ID("erd_publish_rate", is_declaration=True, type=sensor.Sensor),
                CONF_STATE_CLASS: _make_state_class("measurement"),
                "disabled_by_default": False,
                "force_update": False,
            }
        sens = await sensor.new_sensor(val)
        cg.add(var.set_erd_publish_rate_sensor(sens))

    val = config.get(CONF_ERD_CACHE_ENTRIES_SENSOR, True)
    if val is not False:
        if val is True:
            val = {
                "name": "ERD Cache Entries",
                CONF_ID: ID("erd_cache_entries", is_declaration=True, type=sensor.Sensor),
                CONF_STATE_CLASS: _make_state_class("measurement"),
                "disabled_by_default": False,
                "force_update": False,
                "accuracy_decimals": 0,
            }
        sens = await sensor.new_sensor(val)
        cg.add(var.set_erd_cache_entries_sensor(sens))

    val = config.get(CONF_ERD_CACHE_UPDATES_SENSOR, True)
    if val is not False:
        if val is True:
            val = {
                "name": "ERD Cache Update Rate",
                CONF_ID: ID("erd_cache_updates", is_declaration=True, type=sensor.Sensor),
                CONF_STATE_CLASS: _make_state_class("measurement"),
                "disabled_by_default": False,
                "force_update": False,
            }
        sens = await sensor.new_sensor(val)
        cg.add(var.set_erd_cache_updates_sensor(sens))

    val = config.get(CONF_MQTT_PUBLISH_RATE_SENSOR, True)
    if val is not False:
        if val is True:
            val = {
                "name": "MQTT Publish Rate",
                CONF_ID: ID("mqtt_publish_rate", is_declaration=True, type=sensor.Sensor),
                CONF_STATE_CLASS: _make_state_class("measurement"),
                "disabled_by_default": False,
                "force_update": False,
            }
        sens = await sensor.new_sensor(val)
        cg.add(var.set_mqtt_publish_rate_sensor(sens))

    # Create discovery refresh button (auto-created by default, set to false to disable)
    val = config.get(CONF_DISCOVERY_REFRESH_BUTTON, True)
    if val is not False:
        if val is True:
            val = {
                "name": "Discovery Refresh",
                CONF_ID: ID("discovery_refresh", is_declaration=True, type=DiscoveryRefreshButton),
                "disabled_by_default": False,
            }
        btn = cg.new_Pvariable(val[CONF_ID], var)
        await button.register_button(btn, val)

    # Register any user-configured custom ERDs
    for erd in config[CONF_CUSTOM_ERDS]:
        cg.add(var.add_custom_erd(erd))
    
    # Load appliance types from JSON and generate C++ mapping function
    appliance_types = load_appliance_types()
    function_code = generate_appliance_type_function(appliance_types)
    
    # Add the generated function to the global namespace
    cg.add_global(cg.RawStatement(function_code))
