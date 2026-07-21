# esphome-bthome-broadcaster

An external [ESPHome](https://esphome.io) component that broadcasts sensor,
binary sensor, and text sensor values as [BTHome v2](https://bthome.io) BLE
advertisements.
Devices show up automatically in Home Assistant via the native BTHome
integration — no WiFi/API connection required for the sensor data path.

Payload encoding is done by [bthome-cpp](https://github.com/mvoss96/bthome-cpp)
(pulled in automatically as a PlatformIO library — no vendored code). BLE
advertising goes through ESPHome's own `esp32_ble` component, so every ESP32
variant that ESPHome supports with BLE works out of the box — including the
**ESP32-C6** (use the `esp-idf` framework).

Requires **ESPHome ≥ 2026.7.0**.

## Usage

```yaml
external_components:
  - source: github://mvoss96/esphome-bthome-broadcaster@v0.1.0
    components: [bthome_broadcaster]

esp32_ble:

sensor:
  - platform: internal_temperature
    id: cpu_temp

bthome_broadcaster:
  interval: 10s
  sensors:
    - type: temperature
      source: cpu_temp
```

See [example.yaml](example.yaml) for a complete ESP32-C6 example.

## Supported hardware

The component contains no variant-specific code — it inherits BLE support
from ESPHome's `esp32_ble`, so any ESP32 variant that ESPHome supports with
BLE works.

| Variant | Status |
| --- | --- |
| ESP32-C6 | ✅ CI-tested and verified on real hardware |
| ESP32 (classic) | ✅ CI-tested |
| ESP32-C3 / ESP32-C5 / ESP32-S3 | ✅ Expected to work (same RISC-V/Xtensa code paths as above) |
| ESP32-H2 | ⚠️ Untested. Has BLE but no WiFi — the config needs OpenThread or no network at all |
| ESP32-P4 | ⚠️ Untested. No own radio; BLE only via ESP-Hosted co-processor (code paths present) |
| ESP32-S2 | ❌ Not possible — the chip has no Bluetooth (rejected at config validation) |

Use the `esp-idf` framework; it is required on the C6 and recommended
everywhere else.

## Configuration

| Option | Default | Description |
| --- | --- | --- |
| `interval` | `10s` | Minimum time between payload updates/rotations (≥ 100ms). |
| `name` | `true` | Advertise the device name. `true` uses the ESPHome device name, a string overrides it, `false` disables. Names longer than 10 chars are truncated (Shortened Local Name). |
| `min_interval` / `max_interval` | `100ms` | BLE advertising interval range (20ms–10.24s). |
| `tx_power` | `3dBm` | BLE TX power (not available with `esp32_hosted`). |
| `sensors` | — | List of `{type, source}`: BTHome measurement type + id of an existing `sensor`. |
| `binary_sensors` | — | List of `{type, source}`: BTHome binary type + id of an existing `binary_sensor`. |
| `text_sensors` | — | At most one `{source}`: id of an existing `text_sensor`, broadcast as BTHome text (`0x53`). See below for length limits. |

Supported `type` values map 1:1 to the bthome-cpp factory names, e.g.
`temperature`, `humidity`, `pressure`, `battery`, `voltage`, `co2`, `power`,
`energy`, `illuminance`, `pm2_5`, `distance_mm`, … for sensors and `motion`,
`door`, `window`, `occupancy`, `smoke`, `opening`, … for binary sensors. See
[`__init__.py`](components/bthome_broadcaster/__init__.py) for the full lists.

### Text length limits

BLE advertisements are small: after protocol overhead, a text value can use at
most **19 bytes** — and advertising a name eats into that (e.g. **7 bytes**
with a full 10-char name). Longer values are truncated at a UTF-8 character
boundary, with a one-time warning in the log. Disable the name (`name: false`)
or keep it short if you need longer texts.

Only one text sensor is supported: Home Assistant cannot tell multiple BTHome
text measurements apart unless they arrive in the same advertisement, which
two text entries never fit into.

## How it works

- The component registers a raw-advertisement callback with `esp32_ble`
  (the same mechanism `esp32_ble_beacon` uses). `esp32_ble` rotates between all
  registered advertisers; the rotation cadence is controlled by its
  `advertising_cycle_time` option.
- Every `interval`, the current sensor states are packed into a BTHome v2
  service-data payload (service UUID `0xFCD2`) with an auto-incrementing
  `packet_id`.
- If not all values fit into the 31-byte advertisement, the component
  round-robins over the configured entries: each payload continues where the
  previous one stopped, so all values are broadcast over successive intervals.
- Sensors without a published state (or with NaN state) are skipped until they
  have a value.

## Not (yet) supported

- Encryption (AES-CCM) — planned once available in bthome-cpp.
- Button/dimmer events and trigger-based devices.
- Multiple text sensors (see above).
- nRF52/Zephyr targets (ESP32 family only).

## License

[MIT](LICENSE)
