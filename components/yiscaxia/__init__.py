"""Native Pixel K80 lights with a selectable radio transport."""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, number, spi, text
from esphome.const import CONF_DATA_RATE, CONF_EFFECTS, CONF_ID, CONF_NAME, CONF_OUTPUT_ID
from esphome.types import ConfigType

AUTO_LOAD = ["light", "number", "text", "spi"]
DEPENDENCIES = ["spi"]
MULTI_CONF = True

yiscaxia_ns = cg.esphome_ns.namespace("yiscaxia")
YiscaxiaController = yiscaxia_ns.class_("YiscaxiaController", cg.Component)
YiscaxiaTransport = yiscaxia_ns.class_("YiscaxiaTransport", cg.Component)
Md7105Transport = yiscaxia_ns.class_("Md7105Transport", YiscaxiaTransport, spi.SPIDevice)
YiscaxiaLightOutput = yiscaxia_ns.class_("YiscaxiaLightOutput", light.LightOutput)
YiscaxiaPairs = yiscaxia_ns.class_("YiscaxiaPairs", text.Text, cg.Component)
YiscaxiaNumber = yiscaxia_ns.class_("YiscaxiaNumber", number.Number, cg.Component)

BUILTIN_EFFECT_NAMES: tuple[str, ...] = (
    "SOS", "Lightning 1", "Lightning 2", "TV Screen", "Police", "Ambulance",
    "Fire Engine", "RGB Circle 1", "RGB Circle 2", "Slow Rainbow",
)

TRANSPORT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Md7105Transport),
        cv.Required("type"): cv.one_of("md7105", lower=True),
    }
).extend(spi.spi_device_schema()).extend(
    {
        cv.Optional(spi.CONF_SPI_MODE, default="MODE0"): cv.enum(
            {key: value for key, value in spi.SPI_MODE_OPTIONS.items()
             if key in ("MODE0", 0, "0")}, upper=True,
        ),
        cv.Optional(CONF_DATA_RATE, default="1MHz"): cv.All(
            spi.SPI_DATA_RATE_SCHEMA, cv.Range(min=1000000, max=1000000),
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def validate_pairs(value: str) -> str:
    value = cv.string_strict(value)
    if value == "":
        return value
    entries = value.split(",")
    active = [entry for entry in entries if entry != "-"]
    if any(re.fullmatch(r"(?:[1-9]|[1-3][0-9]|4[0-8])[A-F]", entry) is None for entry in active):
        raise cv.Invalid("Use channel 1–48 + group A–F, e.g. 1A,2B; '-' disables a position")
    if len(active) != len(set(active)):
        raise cv.Invalid("Each channel/group pair must be unique")
    return value


def declare_entities(config: ConfigType) -> ConfigType:
    capacity = config["position_capacity"]
    pairs = config["initial_pairs"]
    if pairs and len(pairs.split(",")) > capacity:
        raise cv.Invalid("initial_pairs exceeds position_capacity")
    prefix = str(config[CONF_ID])
    config["positions"] = [
        light.RGB_LIGHT_SCHEMA.extend(
            {cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(YiscaxiaLightOutput)}
        )(
            {
                CONF_ID: f"{prefix}_position_{position + 1}",
                CONF_OUTPUT_ID: f"{prefix}_position_{position + 1}_output",
                CONF_NAME: f"{config['name_prefix']} Position {position + 1}",
                "gamma_correct": 1.0,
                "default_transition_length": "0s",
                "restore_mode": "RESTORE_DEFAULT_OFF",
            }
        )
        for position in range(capacity)
    ]
    # Final validation resolves action names from metadata; setup_state owns runtime effects.
    for position in config["positions"]:
        position[CONF_EFFECTS] = [
            {"yiscaxia_builtin": {CONF_NAME: name}} for name in BUILTIN_EFFECT_NAMES
        ]
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(YiscaxiaController),
            cv.Required("transport"): TRANSPORT_SCHEMA,
            cv.Optional("name_prefix", default="Yiscaxia"): cv.string_strict,
            cv.Optional("position_capacity", default=12): cv.int_range(min=6, max=64),
            cv.Optional("initial_pairs", default="1A,1B,1C,1D,1E,1F"): validate_pairs,
            cv.Required("available_pairs"): text.text_schema(
                YiscaxiaPairs, entity_category="config", mode="TEXT"
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Required("transmission_attempts"): number.number_schema(
                YiscaxiaNumber, entity_category="config"
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Required("channel_spacing"): number.number_schema(
                YiscaxiaNumber, entity_category="config", unit_of_measurement="ms"
            ).extend(cv.COMPONENT_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    declare_entities,
)


def final_validate(config: ConfigType) -> ConfigType:
    spi.final_validate_device_schema(
        "yiscaxia", require_mosi=True, require_miso=True
    )(config["transport"])
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config: ConfigType) -> None:
    parent = cg.new_Pvariable(config[CONF_ID])
    cg.add(parent.set_position_capacity(config["position_capacity"]))
    cg.add(parent.set_initial_pairs(config["initial_pairs"]))
    await cg.register_component(parent, config)
    transport_config = config["transport"]
    transport = cg.new_Pvariable(transport_config[CONF_ID])
    await cg.register_component(transport, transport_config)
    await spi.register_spi_device(transport, transport_config)
    cg.add(parent.set_transport(transport))
    pairs = cg.new_Pvariable(config["available_pairs"][CONF_ID], parent)
    await text.register_text(
        pairs, config["available_pairs"], min_length=0,
        max_length=config["position_capacity"] * 4 - 1,
    )
    await cg.register_component(pairs, config["available_pairs"])
    for key, spacing in (("transmission_attempts", False), ("channel_spacing", True)):
        setting = await number.new_number(
            config[key], parent, spacing, min_value=1,
            max_value=65535 if spacing else 255, step=1,
        )
        await cg.register_component(setting, config[key])
    for position, entity in enumerate(config["positions"]):
        output = cg.new_Pvariable(entity[CONF_OUTPUT_ID], parent, position)
        registration = dict(entity)
        registration.pop(CONF_EFFECTS, None)
        await light.register_light(output, registration)
