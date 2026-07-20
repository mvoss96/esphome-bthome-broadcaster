# esphome-bthome-broadcaster

An external [ESPHome](https://esphome.io) component that broadcasts sensor and
binary sensor values as [BTHome v2](https://bthome.io) BLE advertisements.
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
  - source: github://mvoss96/esphome-bthome-broadcaster
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

## Configuration

| Option | Default | Description |
| --- | --- | --- |
| `interval` | `10s` | Minimum time between payload updates/rotations (≥ 100ms). |
| `name` | `true` | Advertise the device name. `true` uses the ESPHome device name, a string overrides it, `false` disables. Names longer than 10 chars are truncated (Shortened Local Name). |
| `min_interval` / `max_interval` | `100ms` | BLE advertising interval range (20ms–10.24s). |
| `tx_power` | `3dBm` | BLE TX power (not available with `esp32_hosted`). |
| `sensors` | — | List of `{type, source}`: BTHome measurement type + id of an existing `sensor`. |
| `binary_sensors` | — | List of `{type, source}`: BTHome binary type + id of an existing `binary_sensor`. |

Supported `type` values map 1:1 to the bthome-cpp factory names, e.g.
`temperature`, `humidity`, `pressure`, `battery`, `voltage`, `co2`, `power`,
`energy`, `illuminance`, `pm2_5`, `distance_mm`, … for sensors and `motion`,
`door`, `window`, `occupancy`, `smoke`, `opening`, … for binary sensors. See
[`__init__.py`](components/bthome_broadcaster/__init__.py) for the full lists.

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
- nRF52/Zephyr targets (ESP32 family only).

> **TODO before first release:** pin the bthome-cpp dependency to a tagged
> version in `__init__.py` (currently tracking `#main`).

## License

[MIT](LICENSE)
