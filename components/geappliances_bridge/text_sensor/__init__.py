"""Text sensor platform for the auto-generated device ID."""

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from .. import geappliances_bridge_ns, GeappliancesBridge

CONF_BRIDGE_ID = "bridge_id"

GeappliancesBridgeDeviceIdSensor = geappliances_bridge_ns.class_(
    "GeappliancesBridgeDeviceIdSensor", text_sensor.TextSensor, cg.PollingComponent
)

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema()
    .extend(
        {
            cv.GenerateID(): cv.declare_id(GeappliancesBridgeDeviceIdSensor),
            cv.GenerateID(CONF_BRIDGE_ID): cv.use_id(GeappliancesBridge),
        }
    )    .extend(cv.polling_component_schema("60s")))


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_BRIDGE_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.register_device_id_sensor(var))
