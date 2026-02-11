from esphome import automation
import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import (
    CONF_DATA_RATE,
    CONF_ID,
    CONF_RANGE,
    CONF_RESOLUTION,
)

CODEOWNERS = ["@tjhorner"]
DEPENDENCIES = ["i2c"]

MULTI_CONF = True

CONF_LIS3DH_ID = "lis3dh_id"

CONF_ON_TAP = "on_tap"
CONF_ON_DOUBLE_TAP = "on_double_tap"
CONF_ON_FREEFALL = "on_freefall"
CONF_ON_ORIENTATION = "on_orientation"

lis3dh_ns = cg.esphome_ns.namespace("lis3dh")
LIS3DHComponent = lis3dh_ns.class_(
    "LIS3DHComponent", cg.PollingComponent, i2c.I2CDevice
)

LIS3DHRange = lis3dh_ns.enum("Range", True)
LIS3DH_RANGES = {
    "2G": LIS3DHRange.RANGE_2G,
    "4G": LIS3DHRange.RANGE_4G,
    "8G": LIS3DHRange.RANGE_8G,
    "16G": LIS3DHRange.RANGE_16G,
}

LIS3DHDataRate = lis3dh_ns.enum("DataRate", True)
LIS3DH_DATA_RATES = {
    "1HZ": LIS3DHDataRate.ODR_1HZ,
    "10HZ": LIS3DHDataRate.ODR_10HZ,
    "25HZ": LIS3DHDataRate.ODR_25HZ,
    "50HZ": LIS3DHDataRate.ODR_50HZ,
    "100HZ": LIS3DHDataRate.ODR_100HZ,
    "200HZ": LIS3DHDataRate.ODR_200HZ,
    "400HZ": LIS3DHDataRate.ODR_400HZ,
}

LIS3DHResolution = lis3dh_ns.enum("Resolution", True)
LIS3DH_RESOLUTIONS = {
    "LOW_POWER": LIS3DHResolution.RES_LOW_POWER,
    "NORMAL": LIS3DHResolution.RES_NORMAL,
    "HIGH_RES": LIS3DHResolution.RES_HIGH_RES,
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LIS3DHComponent),
            cv.Optional(CONF_RANGE, default="2G"): cv.enum(
                LIS3DH_RANGES, upper=True
            ),
            cv.Optional(CONF_DATA_RATE, default="100HZ"): cv.enum(
                LIS3DH_DATA_RATES, upper=True
            ),
            cv.Optional(CONF_RESOLUTION, default="HIGH_RES"): cv.enum(
                LIS3DH_RESOLUTIONS, upper=True
            ),
            cv.Optional(CONF_ON_TAP): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_DOUBLE_TAP): automation.validate_automation(
                single=True
            ),
            cv.Optional(CONF_ON_FREEFALL): automation.validate_automation(
                single=True
            ),
            cv.Optional(CONF_ON_ORIENTATION): automation.validate_automation(
                single=True
            ),
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(i2c.i2c_device_schema(0x18))
)

LIS3DH_SENSOR_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LIS3DH_ID): cv.use_id(LIS3DHComponent),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_range(config[CONF_RANGE]))
    cg.add(var.set_data_rate(config[CONF_DATA_RATE]))
    cg.add(var.set_resolution(config[CONF_RESOLUTION]))

    if CONF_ON_TAP in config:
        await automation.build_automation(
            var.get_tap_trigger(),
            [],
            config[CONF_ON_TAP],
        )

    if CONF_ON_DOUBLE_TAP in config:
        await automation.build_automation(
            var.get_double_tap_trigger(),
            [],
            config[CONF_ON_DOUBLE_TAP],
        )

    if CONF_ON_FREEFALL in config:
        await automation.build_automation(
            var.get_freefall_trigger(),
            [],
            config[CONF_ON_FREEFALL],
        )

    if CONF_ON_ORIENTATION in config:
        await automation.build_automation(
            var.get_orientation_trigger(),
            [],
            config[CONF_ON_ORIENTATION],
        )
