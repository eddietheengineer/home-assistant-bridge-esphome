"""Number sensor platform for bridge health metrics."""

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv

from .. import geappliances_bridge_ns, GeappliancesBridge

CONF_BRIDGE_ID = "bridge_id"

GeappliancesBridgeHealthSensor = geappliances_bridge_ns.class_(
    "GeappliancesBridgeHealthSensor", sensor.Sensor, cg.PollingComponent
)

# Sensor types
CONF_SENSOR_TYPE = "sensor_type"
SENSOR_TYPE_PENDING_UPDATES = "pending_mqtt_updates"
SENSOR_TYPE_POLLING_CYCLE_TIME_MS = "polling_cycle_time_ms"
SENSOR_TYPE_POLLING_CYCLE_COUNT = "polling_cycle_count"

SENSOR_TYPES = {
  SENSOR_TYPE_PENDING_UPDATES: 0,
  SENSOR_TYPE_POLLING_CYCLE_TIME_MS: 1,
  SENSOR_TYPE_POLLING_CYCLE_COUNT: 2,
}

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        accuracy_decimals=0,
    )
    .extend(
        {
            cv.GenerateID(): cv.declare_id(GeappliancesBridgeHealthSensor),
            cv.GenerateID(CONF_BRIDGE_ID): cv.use_id(GeappliancesBridge),
            cv.Required(CONF_SENSOR_TYPE): cv.enum(SENSOR_TYPES, lower=True),
        }
    )
    .extend(cv.polling_component_schema("60s"))
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_BRIDGE_ID])
    cg.add(var.set_parent(parent))
    # Map the integer sensor_type value to the C++ enum
    sensor_type_value = config[CONF_SENSOR_TYPE]
    cg.add(var.set_sensor_type(
        geappliances_bridge_ns.BridgeHealthSensorType(sensor_type_value)
    ))
