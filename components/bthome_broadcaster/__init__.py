import logging

import esphome.codegen as cg
from esphome.components import binary_sensor, esp32_ble, sensor, text_sensor
from esphome.components.esp32 import request_bluetooth
from esphome.components.esp32_ble import CONF_BLE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_INTERVAL, CONF_NAME, CONF_TX_POWER, CONF_TYPE
from esphome.core import TimePeriod

CODEOWNERS = ["@mvoss96"]
AUTO_LOAD = ["esp32_ble"]
DEPENDENCIES = ["esp32"]

bthome_broadcaster_ns = cg.esphome_ns.namespace("bthome_broadcaster")
BTHomeBroadcaster = bthome_broadcaster_ns.class_("BTHomeBroadcaster", cg.Component)

CONF_SOURCE = "source"
CONF_SENSORS = "sensors"
CONF_BINARY_SENSORS = "binary_sensors"
CONF_TEXT_SENSORS = "text_sensors"
CONF_NAME_PLACEMENT = "name_placement"

_LOGGER = logging.getLogger(__name__)
CONF_MIN_INTERVAL = "min_interval"
CONF_MAX_INTERVAL = "max_interval"

BTHOME_CPP_REPOSITORY = "https://github.com/mvoss96/bthome-cpp.git#v0.1.0"

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


def validate_config(config):
    if config[CONF_MIN_INTERVAL] > config[CONF_MAX_INTERVAL]:
        raise cv.Invalid("min_interval must be <= max_interval")
    if (
        not config[CONF_SENSORS]
        and not config[CONF_BINARY_SENSORS]
        and not config[CONF_TEXT_SENSORS]
    ):
        raise cv.Invalid(
            "At least one of sensors, binary_sensors or text_sensors is required"
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

    request_bluetooth(ble_42=True)
