"""Native Pixel K80 lights with a selectable radio transport."""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, number, spi, text
from esphome.const import (
    CONF_DATA_RATE, CONF_DEFAULT_TRANSITION_LENGTH, CONF_EFFECTS, CONF_GAMMA_CORRECT,
    CONF_ID, CONF_NAME, CONF_OUTPUT_ID, CONF_RESTORE_MODE, CONF_TYPE,
)
from esphome.types import ConfigType

CONF_TRANSPORT = "transport"
CONF_NAME_PREFIX = "name_prefix"
CONF_POSITION_CAPACITY = "position_capacity"
CONF_INITIAL_PAIRS = "initial_pairs"
CONF_AVAILABLE_PAIRS = "available_pairs"
CONF_TRANSMISSION_ATTEMPTS = "transmission_attempts"
CONF_CHANNEL_SPACING = "channel_spacing"
CONF_RAINBOW_DEGREES_PER_STEP = "rainbow_degrees_per_step"
CONF_RAINBOW_STEP_SPACING = "rainbow_step_spacing"
CONF_POSITIONS = "positions"

AUTO_LOAD = ["light", "number", "text", "spi"]
DEPENDENCIES = ["spi"]
MULTI_CONF = True

pixel_k80_ns = cg.esphome_ns.namespace("pixel_k80")
PixelK80Controller = pixel_k80_ns.class_("PixelK80Controller", cg.Component)
PixelK80Transport = pixel_k80_ns.class_("PixelK80Transport", cg.Component)
Md7105Transport = pixel_k80_ns.class_("Md7105Transport", PixelK80Transport, spi.SPIDevice)
PixelK80LightOutput = pixel_k80_ns.class_("PixelK80LightOutput", light.LightOutput)
PixelK80LightState = pixel_k80_ns.class_("PixelK80LightState", light.LightState)
PixelK80Pairs = pixel_k80_ns.class_("PixelK80Pairs", text.Text, cg.Component)
PixelK80Number = pixel_k80_ns.class_("PixelK80Number", number.Number, cg.Component)
PixelK80Setting = pixel_k80_ns.enum("PixelK80Setting", is_class=True)

BUILTIN_EFFECT_NAMES: tuple[str, ...] = (
    "SOS", "Lightning 1", "Lightning 2", "TV Screen", "Police", "Ambulance",
    "Fire Engine", "RGB Circle 1", "RGB Circle 2", "Custom Rainbow",
)

TRANSPORT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Md7105Transport),
        cv.Required(CONF_TYPE): cv.one_of("md7105", lower=True),
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
    capacity = config[CONF_POSITION_CAPACITY]
    pairs = config[CONF_INITIAL_PAIRS]
    if pairs and len(pairs.split(",")) > capacity:
        raise cv.Invalid("initial_pairs exceeds position_capacity")
    prefix = str(config[CONF_ID])
    config[CONF_POSITIONS] = [
        light.RGB_LIGHT_SCHEMA.extend(
            {
                cv.GenerateID(CONF_ID): cv.declare_id(PixelK80LightState),
                cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(PixelK80LightOutput),
            }
        )(
            {
                CONF_ID: f"{prefix}_position_{position + 1}",
                CONF_OUTPUT_ID: f"{prefix}_position_{position + 1}_output",
                CONF_NAME: f"{config[CONF_NAME_PREFIX]} Position {position + 1}",
                CONF_GAMMA_CORRECT: 1.0,
                CONF_DEFAULT_TRANSITION_LENGTH: "0s",
                CONF_RESTORE_MODE: "RESTORE_DEFAULT_OFF",
            }
        )
        for position in range(capacity)
    ]
    # Final validation resolves action names from metadata; setup_state owns runtime effects.
    for position in config[CONF_POSITIONS]:
        position[CONF_EFFECTS] = [
            {"pixel_k80_builtin": {CONF_NAME: name}} for name in BUILTIN_EFFECT_NAMES
        ]
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(PixelK80Controller),
            cv.Required(CONF_TRANSPORT): TRANSPORT_SCHEMA,
            cv.Optional(CONF_NAME_PREFIX, default="Pixel K80"): cv.string_strict,
            cv.Optional(CONF_POSITION_CAPACITY, default=12): cv.int_range(min=6, max=64),
            cv.Optional(CONF_INITIAL_PAIRS, default="1A,1B,1C,1D,1E,1F"): validate_pairs,
            cv.Required(CONF_AVAILABLE_PAIRS): text.text_schema(
                PixelK80Pairs, entity_category="config", mode="TEXT"
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Required(CONF_TRANSMISSION_ATTEMPTS): number.number_schema(
                PixelK80Number, entity_category="config"
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Required(CONF_CHANNEL_SPACING): number.number_schema(
                PixelK80Number, entity_category="config", unit_of_measurement="ms"
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Optional(CONF_RAINBOW_DEGREES_PER_STEP): number.number_schema(
                PixelK80Number, entity_category="config", unit_of_measurement="°"
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Optional(CONF_RAINBOW_STEP_SPACING): number.number_schema(
                PixelK80Number, entity_category="config", unit_of_measurement="ms"
            ).extend(cv.COMPONENT_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    declare_entities,
)


def final_validate(config: ConfigType) -> ConfigType:
    spi.final_validate_device_schema(
        "pixel_k80", require_mosi=True, require_miso=True
    )(config[CONF_TRANSPORT])
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config: ConfigType) -> None:
    parent = cg.new_Pvariable(config[CONF_ID])
    cg.add(parent.set_position_capacity(config[CONF_POSITION_CAPACITY]))
    cg.add(parent.set_initial_pairs(config[CONF_INITIAL_PAIRS]))
    await cg.register_component(parent, config)
    transport_config = config[CONF_TRANSPORT]
    transport = cg.new_Pvariable(transport_config[CONF_ID])
    await cg.register_component(transport, transport_config)
    await spi.register_spi_device(transport, transport_config)
    cg.add(parent.set_transport(transport))
    pairs = cg.new_Pvariable(config[CONF_AVAILABLE_PAIRS][CONF_ID], parent)
    await text.register_text(
        pairs, config[CONF_AVAILABLE_PAIRS], min_length=0,
        max_length=config[CONF_POSITION_CAPACITY] * 4 - 1,
    )
    await cg.register_component(pairs, config[CONF_AVAILABLE_PAIRS])
    for key, selector in (
        (CONF_TRANSMISSION_ATTEMPTS, PixelK80Setting.TRANSMISSION_ATTEMPTS),
        (CONF_CHANNEL_SPACING, PixelK80Setting.CHANNEL_SPACING),
        (CONF_RAINBOW_DEGREES_PER_STEP, PixelK80Setting.RAINBOW_STEP_DEGREES),
        (CONF_RAINBOW_STEP_SPACING, PixelK80Setting.RAINBOW_STEP_SPACING),
    ):
        if key not in config:
            continue
        setting = await number.new_number(
            config[key], parent, selector, min_value=1,
            max_value=cg.RawExpression(f"pixel_k80::configuration_setting_max({selector})"), step=1,
        )
        await cg.register_component(setting, config[key])
    for position, entity in enumerate(config[CONF_POSITIONS]):
        output = cg.new_Pvariable(entity[CONF_OUTPUT_ID], parent, position)
        registration = dict(entity)
        registration.pop(CONF_EFFECTS, None)
        await light.register_light(output, registration)
