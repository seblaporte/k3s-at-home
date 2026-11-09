import esphome.codegen as cg
from esphome.components import binary_sensor, ble_client, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BATTERY_LEVEL,
    CONF_CURRENT,
    CONF_ID,
    CONF_PASSWORD,
    CONF_TEMPERATURE,
    CONF_VOLTAGE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_BATTERY_CHARGING,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_VOLT,
)

CODEOWNERS = []
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "binary_sensor"]

CONF_BETTER_PERCENTS = "better_percents"
CONF_PUBLISH_INTERVAL = "publish_interval"
CONF_CHARGING = "charging"
CONF_ONLINE = "online"

inmotion_v1_ns = cg.esphome_ns.namespace("inmotion_v1")
InMotionV1 = inmotion_v1_ns.class_("InMotionV1", cg.Component, ble_client.BLEClientNode)


def validate_password(value):
    value = cv.string_strict(value)
    if not value.isascii() or len(value) > 6:
        raise cv.Invalid("Le mot de passe InMotion fait au plus 6 caractères ASCII")
    return value.rjust(6, "0")


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(InMotionV1),
            cv.Optional(CONF_PASSWORD, default="000000"): validate_password,
            cv.Optional(CONF_BETTER_PERCENTS, default=False): cv.boolean,
            cv.Optional(CONF_PUBLISH_INTERVAL, default="2s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_CHARGING): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_BATTERY_CHARGING,
            ),
            cv.Optional(CONF_ONLINE): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_better_percents(config[CONF_BETTER_PERCENTS]))
    cg.add(var.set_publish_interval(config[CONF_PUBLISH_INTERVAL]))

    for key, setter in (
        (CONF_VOLTAGE, var.set_voltage_sensor),
        (CONF_BATTERY_LEVEL, var.set_battery_level_sensor),
        (CONF_CURRENT, var.set_current_sensor),
        (CONF_TEMPERATURE, var.set_temperature_sensor),
    ):
        if key in config:
            cg.add(setter(await sensor.new_sensor(config[key])))
    for key, setter in (
        (CONF_CHARGING, var.set_charging_sensor),
        (CONF_ONLINE, var.set_online_sensor),
    ):
        if key in config:
            cg.add(setter(await binary_sensor.new_binary_sensor(config[key])))
