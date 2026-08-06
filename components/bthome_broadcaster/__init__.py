import inspect
import logging

from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, esp32_ble, sensor, text_sensor
from esphome.components.esp32 import request_bluetooth
from esphome.components.esp32_ble import CONF_BLE_ID
import esphome.config_validation as cv
from esphome.const import (
    CONF_EVENT,
    CONF_ID,
    CONF_INTERVAL,
    CONF_NAME,
    CONF_TX_POWER,
    CONF_TYPE,
)
from esphome.core import TimePeriod
from esphome.util import parse_esphome_version

CODEOWNERS = ["@mvoss96"]
AUTO_LOAD = ["esp32_ble"]
DEPENDENCIES = ["esp32"]

bthome_broadcaster_ns = cg.esphome_ns.namespace("bthome_broadcaster")
BTHomeBroadcaster = bthome_broadcaster_ns.class_("BTHomeBroadcaster", cg.Component)
ButtonEventAction = bthome_broadcaster_ns.class_("ButtonEventAction", automation.Action)
DimmerEventAction = bthome_broadcaster_ns.class_("DimmerEventAction", automation.Action)

CONF_SOURCE = "source"
CONF_SENSORS = "sensors"
CONF_BINARY_SENSORS = "binary_sensors"
CONF_TEXT_SENSORS = "text_sensors"
CONF_NAME_PLACEMENT = "name_placement"
CONF_ENCRYPTION_KEY = "encryption_key"

_LOGGER = logging.getLogger(__name__)
CONF_MIN_INTERVAL = "min_interval"
CONF_MAX_INTERVAL = "max_interval"

BTHOME_CPP_REPOSITORY = "https://github.com/mvoss96/bthome-cpp.git#v0.3.2"

# Enforced in validate_config: the README documents this, but without a check
# an older core fails somewhere deep in codegen instead of saying so.
MIN_ESPHOME_VERSION = (2026, 7, 0)

# Maps the user-facing type name (== bthome-cpp factory name) to the C++ argument
# type of the factory. "float" factories are passed through directly; integer
# factories are wrapped in rounded_factory<T, F> to convert the float sensor state.
SENSOR_TYPES = {
    "acceleration": "float",
    "acceleration_s32": "float",
    "battery": "uint8_t",
    "channel": "uint8_t",
    "co2": "float",
    "conductivity": "float",
    "count": "uint8_t",
    "count_s8": "int8_t",
    "count_s16": "int16_t",
    "count_s32": "int32_t",
    "count_u16": "uint16_t",
    "count_u32": "uint32_t",
    "current": "float",
    "current_s16": "float",
    "dewpoint": "float",
    "direction": "float",
    "distance_m": "float",
    "distance_mm": "float",
    "duration": "float",
    "energy": "float",
    "energy_u32": "float",
    "gas": "float",
    "gas_u32": "float",
    "gyroscope": "float",
    "humidity": "float",
    "humidity_u8": "uint8_t",
    "illuminance": "float",
    "light_level": "uint8_t",
    "mass_kg": "float",
    "mass_lb": "float",
    "moisture": "float",
    "moisture_u8": "uint8_t",
    "pm10": "float",
    "pm2_5": "float",
    "power": "float",
    "power_s32": "float",
    "precipitation": "float",
    "pressure": "float",
    "rotation": "float",
    "rotational_speed": "float",
    "settings_revision": "uint8_t",
    "speed": "float",
    "speed_s32": "float",
    "temperature": "float",
    "temperature_c1": "float",
    "temperature_s8": "int8_t",
    "temperature_s8_035": "float",
    "timestamp": "uint32_t",
    "tvoc": "float",
    "uv_index": "float",
    "voltage": "float",
    "voltage_c1": "float",
    "volume_flow_rate": "float",
    "volume_l": "float",
    "volume_ml": "float",
    "volume_storage": "float",
    "volume_u32": "float",
    "water": "float",
}

BINARY_SENSOR_TYPES = [
    "battery_charging",
    "battery_low",
    "carbon_monoxide",
    "cold",
    "connectivity",
    "door",
    "garage_door",
    "gas_detected",
    "generic_boolean",
    "heat",
    "light",
    "lock",
    "moisture_detected",
    "motion",
    "moving",
    "occupancy",
    "opening",
    "plug",
    "power_state",
    "presence",
    "problem",
    "running",
    "safety",
    "smoke",
    "sound",
    "tamper",
    "vibration",
    "window",
]

SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TYPE): cv.one_of(*SENSOR_TYPES, lower=True),
        cv.Required(CONF_SOURCE): cv.use_id(sensor.Sensor),
    }
)

BINARY_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TYPE): cv.one_of(*BINARY_SENSOR_TYPES, lower=True),
        cv.Required(CONF_SOURCE): cv.use_id(binary_sensor.BinarySensor),
    }
)

# No type field: BTHome has exactly one text measurement (0x53).
TEXT_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SOURCE): cv.use_id(text_sensor.TextSensor),
    }
)


def validate_encryption_key(value):
    # Same format Home Assistant asks for when adding an encrypted BTHome
    # device: the 16-byte AES key as 32 hex characters.
    value = cv.string_strict(value)
    if len(value) != 32 or any(c not in "0123456789abcdefABCDEF" for c in value):
        raise cv.Invalid(
            "encryption_key must be 32 hexadecimal characters (16 bytes), "
            'e.g. "231d39c1d7cc1ab1aee224cd096db932"'
        )
    return value.lower()


def validate_config(config):
    if parse_esphome_version() < MIN_ESPHOME_VERSION:
        raise cv.Invalid(
            "bthome_broadcaster requires ESPHome "
            f"{'.'.join(str(part) for part in MIN_ESPHOME_VERSION)} or newer"
        )
    if config[CONF_MIN_INTERVAL] > config[CONF_MAX_INTERVAL]:
        raise cv.Invalid("min_interval must be <= max_interval")
    # No sensors required: a pure event device (only bthome_broadcaster.*_event
    # actions in automations) broadcasts nothing between events.
    if CONF_ENCRYPTION_KEY in config and config[CONF_NAME_PLACEMENT] == "advertisement":
        raise cv.Invalid(
            "encryption_key cannot be combined with name_placement: advertisement: "
            "the 8-byte encryption overhead plus an in-advertisement name leaves "
            "no room for measurements. Use name_placement: scan_response."
        )
    if config[CONF_NAME_PLACEMENT] == "advertisement" and config[CONF_TEXT_SENSORS]:
        _LOGGER.warning(
            "name_placement: advertisement shrinks the per-packet data budget; "
            "text values will be truncated to as few as 7 bytes. Use "
            "name_placement: scan_response for the full 19 bytes."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BTHomeBroadcaster),
            cv.GenerateID(CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
            cv.Optional(CONF_INTERVAL, default="10s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=TimePeriod(milliseconds=100)),
            ),
            cv.Optional(CONF_NAME, default=True): cv.Any(cv.boolean, cv.string_strict),
            # scan_response keeps the full advertisement for sensor data;
            # advertisement makes the name visible to passive scanners.
            cv.Optional(CONF_NAME_PLACEMENT, default="scan_response"): cv.one_of(
                "scan_response", "advertisement", lower=True
            ),
            cv.Optional(CONF_ENCRYPTION_KEY): validate_encryption_key,
            cv.Optional(CONF_MIN_INTERVAL, default="100ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=TimePeriod(milliseconds=20), max=TimePeriod(milliseconds=10240)
                ),
            ),
            cv.Optional(CONF_MAX_INTERVAL, default="100ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=TimePeriod(milliseconds=20), max=TimePeriod(milliseconds=10240)
                ),
            ),
            cv.OnlyWithout(CONF_TX_POWER, "esp32_hosted", default="3dBm"): cv.All(
                cv.conflicts_with_component("esp32_hosted"),
                cv.decibel,
                cv.enum(esp32_ble.TX_POWER_LEVELS, int=True),
            ),
            cv.Optional(CONF_SENSORS, default=[]): cv.ensure_list(SENSOR_SCHEMA),
            cv.Optional(CONF_BINARY_SENSORS, default=[]): cv.ensure_list(
                BINARY_SENSOR_SCHEMA
            ),
            # Only one: Home Assistant cannot distinguish multiple BTHome text
            # measurements unless they share one advertisement, which a full
            # text entry (up to 26 bytes) never can.
            cv.Optional(CONF_TEXT_SENSORS, default=[]): cv.All(
                cv.ensure_list(TEXT_SENSOR_SCHEMA), cv.Length(max=1)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_config,
)

FINAL_VALIDATE_SCHEMA = esp32_ble.validate_variant

# Maps the YAML event name to the BTHome::ButtonEventType enumerator.
BUTTON_EVENTS = {
    "press": "Press",
    "double_press": "DoublePress",
    "triple_press": "TriplePress",
    "long_press": "LongPress",
    "long_double_press": "LongDoublePress",
    "long_triple_press": "LongTriplePress",
    "hold_press": "HoldPress",
}

DIMMER_EVENTS = {
    "rotate_left": "RotateLeft",
    "rotate_right": "RotateRight",
}

CONF_BUTTON_INDEX = "button_index"
CONF_STEPS = "steps"

# Each earlier button costs 2 padding bytes; 6 keeps the deepest index
# broadcastable even in an encrypted packet (20-byte budget).
MAX_BUTTON_INDEX = 6

BUTTON_EVENT_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(BTHomeBroadcaster),
        cv.Optional(CONF_EVENT, default="press"): cv.one_of(*BUTTON_EVENTS, lower=True),
        cv.Optional(CONF_BUTTON_INDEX, default=1): cv.int_range(
            min=1, max=MAX_BUTTON_INDEX
        ),
    }
)

DIMMER_EVENT_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(BTHomeBroadcaster),
        cv.Required(CONF_EVENT): cv.one_of(*DIMMER_EVENTS, lower=True),
        cv.Optional(CONF_STEPS, default=1): cv.templatable(cv.int_range(min=1, max=255)),
    }
)


@automation.register_action(
    "bthome_broadcaster.button_event",
    ButtonEventAction,
    BUTTON_EVENT_ACTION_SCHEMA,
    # play() builds and broadcasts the event packet inline before returning.
    synchronous=True,
)
async def button_event_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(parent.set_has_events(True))
    cg.add(
        var.set_event(
            cg.RawExpression(f"BTHome::ButtonEventType::{BUTTON_EVENTS[config[CONF_EVENT]]}")
        )
    )
    cg.add(var.set_button_index(config[CONF_BUTTON_INDEX]))
    return var


@automation.register_action(
    "bthome_broadcaster.dimmer_event",
    DimmerEventAction,
    DIMMER_EVENT_ACTION_SCHEMA,
    synchronous=True,
)
async def dimmer_event_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(parent.set_has_events(True))
    cg.add(
        var.set_event(
            cg.RawExpression(f"BTHome::DimmerEventType::{DIMMER_EVENTS[config[CONF_EVENT]]}")
        )
    )
    template_ = await cg.templatable(config[CONF_STEPS], args, cg.uint8)
    cg.add(var.set_steps(template_))
    return var


def sensor_factory_expression(type_name: str) -> cg.RawExpression:
    arg_type = SENSOR_TYPES[type_name]
    if arg_type == "float":
        return cg.RawExpression(f"BTHome::{type_name}")
    return cg.RawExpression(
        f"esphome::bthome_broadcaster::rounded_factory<std::{arg_type}, BTHome::{type_name}>"
    )


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    parent = await cg.get_variable(config[CONF_BLE_ID])
    esp32_ble.register_gap_event_handler(parent, var)

    await cg.register_component(var, config)

    cg.add(var.set_advertise_interval(config[CONF_INTERVAL]))
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL]))
    cg.add(var.set_max_interval(config[CONF_MAX_INTERVAL]))

    name = config[CONF_NAME]
    if isinstance(name, bool):
        cg.add(var.set_name_enabled(name))
    else:
        cg.add(var.set_name_enabled(True))
        cg.add(var.set_local_name(name))
    cg.add(
        var.set_name_in_advertisement(config[CONF_NAME_PLACEMENT] == "advertisement")
    )

    if CONF_ENCRYPTION_KEY in config:
        cg.add(var.set_encryption_key(list(bytes.fromhex(config[CONF_ENCRYPTION_KEY]))))
        cg.add_define("USE_BTHOME_ENCRYPTION")

    # TX power control only available on native Bluetooth (not ESP-Hosted)
    if CONF_TX_POWER in config:
        cg.add(var.set_tx_power(config[CONF_TX_POWER]))

    for conf in config[CONF_SENSORS]:
        source = await cg.get_variable(conf[CONF_SOURCE])
        cg.add(var.add_sensor(source, sensor_factory_expression(conf[CONF_TYPE])))

    for conf in config[CONF_BINARY_SENSORS]:
        source = await cg.get_variable(conf[CONF_SOURCE])
        cg.add(
            var.add_binary_sensor(
                source, cg.RawExpression(f"BTHome::{conf[CONF_TYPE]}")
            )
        )

    for conf in config[CONF_TEXT_SENSORS]:
        source = await cg.get_variable(conf[CONF_SOURCE])
        cg.add(var.set_text_sensor(source))

    cg.add_library("bthome-cpp", None, BTHOME_CPP_REPOSITORY)
    cg.add_define("USE_ESP32_BLE_UUID")
    cg.add_define("USE_ESP32_BLE_ADVERTISING")

    # The component uses the legacy (BLE 4.2) advertising API, which is not
    # compiled in on BLE-5.0-only variants such as the ESP32-C6 unless
    # CONFIG_BT_BLE_42_FEATURES_SUPPORTED is set. Up to ESPHome 2026.7.3 that
    # needed request_bluetooth(ble_42=True); since 2026.7.4 requesting
    # Bluetooth always enables the 4.2 features and the parameter is gone.
    if "ble_42" in inspect.signature(request_bluetooth).parameters:
        request_bluetooth(ble_42=True)
    else:
        request_bluetooth()
