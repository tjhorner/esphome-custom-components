import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_LOGGER

CODEOWNERS = ["@esphome/core"]
DEPENDENCIES = ["logger", "wifi"]
AUTO_LOAD = ["json"]

serial_rpc_ns = cg.esphome_ns.namespace("serial_rpc")

SerialRpcComponent = serial_rpc_ns.class_("SerialRpcComponent", cg.Component)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(SerialRpcComponent)})
    .extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)