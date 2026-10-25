import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import spi
from esphome.const import CONF_ID, CONF_TRIGGER_ID, CONF_ON_TAG

CODEOWNERS = []
DEPENDENCIES = ["spi"]
MULTI_CONF = True

ktb_nfc_ns = cg.esphome_ns.namespace("ktb_nfc")
KtbNfc = ktb_nfc_ns.class_("KtbNfc", cg.PollingComponent, spi.SPIDevice)
TagTrigger = ktb_nfc_ns.class_("TagTrigger", automation.Trigger.template(cg.std_string))
KeyOkTrigger = ktb_nfc_ns.class_("KeyOkTrigger", automation.Trigger.template())
KeyFailTrigger = ktb_nfc_ns.class_("KeyFailTrigger", automation.Trigger.template())

CONF_KEY = "key"
CONF_ON_KEY_OK = "on_key_ok"
CONF_ON_KEY_FAIL = "on_key_fail"


def validate_key(value):
    value = cv.string_strict(value).strip()
    if len(value) != 64:
        raise cv.Invalid("key muss genau 64 Hex-Zeichen (32 Byte) lang sein")
    try:
        bytes.fromhex(value)
    except ValueError as err:
        raise cv.Invalid("key darf nur aus Hex-Zeichen bestehen") from err
    return value.upper()


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(KtbNfc),
            cv.Required(CONF_KEY): validate_key,
            cv.Optional(CONF_ON_TAG): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TagTrigger)}
            ),
            cv.Optional(CONF_ON_KEY_OK): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(KeyOkTrigger)}
            ),
            cv.Optional(CONF_ON_KEY_FAIL): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(KeyFailTrigger)}
            ),
        }
    )
    .extend(cv.polling_component_schema("500ms"))
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    cg.add(var.set_key_hex(config[CONF_KEY]))

    for conf in config.get(CONF_ON_TAG, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.std_string, "x")], conf)
    for conf in config.get(CONF_ON_KEY_OK, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [], conf)
    for conf in config.get(CONF_ON_KEY_FAIL, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [], conf)
