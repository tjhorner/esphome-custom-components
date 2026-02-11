import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from . import CONF_LIS3DH_ID, LIS3DH_SENSOR_SCHEMA

CODEOWNERS = ["@tjhorner"]
DEPENDENCIES = ["lis3dh"]

CONF_ORIENTATION_XY = "orientation_xy"
CONF_ORIENTATION_Z = "orientation_z"

CONFIG_SCHEMA = LIS3DH_SENSOR_SCHEMA.extend(
    {
        cv.Optional(CONF_ORIENTATION_XY): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_ORIENTATION_Z): text_sensor.text_sensor_schema(),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LIS3DH_ID])
    if CONF_ORIENTATION_XY in config:
        sens = await text_sensor.new_text_sensor(config[CONF_ORIENTATION_XY])
        cg.add(hub.set_orientation_xy_text_sensor(sens))
    if CONF_ORIENTATION_Z in config:
        sens = await text_sensor.new_text_sensor(config[CONF_ORIENTATION_Z])
        cg.add(hub.set_orientation_z_text_sensor(sens))
