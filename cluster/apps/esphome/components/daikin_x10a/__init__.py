import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_OFFSET, CONF_SIZE

CODEOWNERS = []
DEPENDENCIES = ["uart"]
MULTI_CONF = True

CONF_DAIKIN_X10A_ID = "daikin_x10a_id"
CONF_REGISTER = "register"
CONF_CONVID = "convid"

daikin_x10a_ns = cg.esphome_ns.namespace("daikin_x10a")
DaikinX10A = daikin_x10a_ns.class_("DaikinX10A", cg.PollingComponent, uart.UARTDevice)

# Mêmes plages que x10a::kind_of() côté C++
NUMBER_CONVIDS = {
    *range(101, 120),
    *range(151, 159),
    *range(161, 166),
    211,
    312,
    *range(401, 407),
}
TEXT_CONVIDS = {100, 201, 203, 204, 215, 216, 217, 315, 316}
BINARY_CONVIDS = {200, *range(300, 308)}

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(DaikinX10A)})
    .extend(cv.polling_component_schema("30s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "daikin_x10a",
    baud_rate=9600,
    require_rx=True,
    require_tx=True,
    data_bits=8,
    parity="EVEN",
    stop_bits=1,
)


def value_schema(convids, kind):
    """Une valeur d'une ligne LabelDef d'ESPAltherma : {registre, offset, convid, taille, …}."""

    def validate_convid(value):
        value = cv.int_(value)
        if value not in convids:
            raise cv.Invalid(f"convid {value} non pris en charge par la plateforme {kind}")
        return value

    return {
        cv.GenerateID(CONF_DAIKIN_X10A_ID): cv.use_id(DaikinX10A),
        cv.Required(CONF_REGISTER): cv.hex_uint8_t,
        cv.Required(CONF_OFFSET): cv.uint8_t,
        cv.Required(CONF_CONVID): validate_convid,
        cv.Optional(CONF_SIZE, default=1): cv.int_range(min=1, max=32),
    }


async def register_value(setter, var, config):
    hub = await cg.get_variable(config[CONF_DAIKIN_X10A_ID])
    cg.add(
        getattr(hub, setter)(
            var, config[CONF_REGISTER], config[CONF_OFFSET], config[CONF_SIZE], config[CONF_CONVID]
        )
    )


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
