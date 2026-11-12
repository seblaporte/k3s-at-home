from esphome.components import binary_sensor

from . import BINARY_CONVIDS, register_value, value_schema

DEPENDENCIES = ["daikin_x10a"]

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema().extend(value_schema(BINARY_CONVIDS, "binary_sensor"))


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await register_value("add_binary_sensor", var, config)
