from esphome.components import sensor
from esphome.const import STATE_CLASS_MEASUREMENT

from . import NUMBER_CONVIDS, register_value, value_schema

DEPENDENCIES = ["daikin_x10a"]

CONFIG_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(value_schema(NUMBER_CONVIDS, "sensor"))


async def to_code(config):
    var = await sensor.new_sensor(config)
    await register_value("add_sensor", var, config)
