from esphome.components import text_sensor

from . import TEXT_CONVIDS, register_value, value_schema

DEPENDENCIES = ["daikin_x10a"]

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(value_schema(TEXT_CONVIDS, "text_sensor"))


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await register_value("add_text_sensor", var, config)
