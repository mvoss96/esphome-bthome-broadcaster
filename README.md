# esphome-bthome-broadcaster

An external [ESPHome](https://esphome.io) component that broadcasts sensor,
binary sensor, and text sensor values as [BTHome v2](https://bthome.io) BLE
advertisements.
Devices show up automatically in Home Assistant via the native BTHome
integration — no WiFi/API connection required for the sensor data path.
Supports optional BTHome AES-CCM encryption.

Payload encoding is done by [bthome-cpp](https://github.com/mvoss96/bthome-cpp)
(pulled in automatically as a PlatformIO library — no vendored code). BLE
advertising goes through ESPHome's own `esp32_ble` component, and the
component itself contains no variant-specific code — verified on the
**ESP32-C6** (use the `esp-idf` framework) and the classic ESP32, expected to
work on the other BLE-capable variants (see [below](#supported-hardware)).

Requires **ESPHome ≥ 2026.7.0** (checked during config validation).

## Usage

```yaml
external_components:
  - source: github://mvoss96/esphome-bthome-broadcaster@v0.5.0
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
from ESPHome's `esp32_ble`. Only the two variants below are actually tested;
the rest is a well-founded expectation, not a promise.

| Variant | Status |
| --- | --- |
| ESP32-C6 | ✅ CI-tested and verified on real hardware |
| ESP32 (classic) | ✅ CI-tested |
| ESP32-C3 / ESP32-C5 / ESP32-S3 | 🔵 Expected to work, untested (same RISC-V/Xtensa code paths as above) |
| ESP32-H2 | ⚠️ Untested. Has BLE but no WiFi — the config needs OpenThread or no network at all |
| ESP32-P4 | ⚠️ Untested. No own radio; BLE only via ESP-Hosted co-processor (code paths present) |
| ESP32-S2 | ❌ Not possible — the chip has no Bluetooth (rejected at config validation) |

Use the `esp-idf` framework; it is required on the C6 and recommended
everywhere else.

## Configuration

| Option | Default | Description |
| --- | --- | --- |
| `interval` | `10s` | Minimum time between payload updates/rotations (≥ 100ms). |
| `name` | `true` | Advertise the device name. `true` uses the ESPHome device name, a string overrides it, `false` disables. |
| `name_placement` | `scan_response` | Where the name is sent. `scan_response` (own 31 bytes, max 29 chars) keeps the full advertisement for sensor data but is invisible to purely passive scanners. `advertisement` (max 10 chars) is visible to passive scanners but shrinks the per-packet data budget. Over-long names are truncated (Shortened Local Name). |
| `encryption_key` | — | Optional 16-byte AES key as 32 hex characters. Enables BTHome AES-CCM encryption; see below. |
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

### Value precision

Every ESPHome sensor state is a 32-bit `float` (24-bit mantissa), so whole
numbers above **16,777,216** (2²⁴) cannot be represented exactly — the value
is already rounded before this component sees it. That matters for the 32-bit
BTHome types:

| Type | Consequence |
| --- | --- |
| `timestamp` | A current Unix timestamp (~1.78 × 10⁹) has a resolution of roughly **128 s**. Second-accurate times are not possible through a `sensor`; use it only for coarse timestamps. |
| `count_u32` / `count_s32` | Exact up to 16,777,216, then in steps of 2, 4, … Individual increments get lost above that. |
| `energy_u32` / `gas_u32` / `volume_u32` | Same limit, applied to the value *after* BTHome's scaling factor (e.g. ×1000 for kWh), so it bites correspondingly earlier. |

Everything else — temperatures, humidity, pressure, `count_u16`, battery, … —
is far below the limit and unaffected.

### Encryption

```yaml
bthome_broadcaster:
  encryption_key: !secret bthome_encryption_key
```

Encrypts every advertisement with BTHome's AES-CCM scheme. Home Assistant asks
for the same 32-hex-character key once when the device is added (or under
*Settings → Devices → BTHome device → Configure* if it was added before).

**Generate your own key** — `openssl rand -hex 16` — and keep it in
`secrets.yaml`. A key copied from documentation is a key everyone else has:
anyone in radio range could then decrypt the advertisements and, worse,
forge them.

What to know:

- **8 bytes of the data budget** go to the encryption counter + auth tag, so
  fewer measurements fit per packet (rotation handles the rest automatically)
  and text values shrink to at most **11 bytes**.
- The **replay-protection counter is persisted** in flash and restored with a
  safety margin of 1024 after every reboot, so receivers never see a repeated
  counter — no re-pairing needed after crashes or power loss. Flash is written
  only once per 1024 packets. The counter is 32 bits and never wraps: at
  roughly 4.3 billion packets it stops instead, with an error in the log,
  because reusing a counter value would reuse an encryption nonce. Reaching
  that takes over a thousand years at the default 10 s interval; if it ever
  happens, generate a new key.
- `name_placement: advertisement` is rejected together with `encryption_key`
  (both would not leave room for any measurement); the default scan-response
  name works normally.

### Events

Three actions broadcast BTHome events from any ESPHome automation. Two of
them report what happened on this device — button presses and dimmer
rotation:

```yaml
binary_sensor:
  - platform: gpio
    pin: GPIO9
    id: phys_button
    on_click:
      - bthome_broadcaster.button_event:
          event: press           # press | double_press | triple_press | long_press |
                                 # long_double_press | long_triple_press | hold_press
          button_index: 1        # optional, 1-6; Home Assistant shows one
                                 # event entity per button

sensor:
  - platform: rotary_encoder
    # ...
    on_clockwise:
      - bthome_broadcaster.dimmer_event:
          event: rotate_right    # rotate_left | rotate_right
          steps: 1               # templatable
```

An event takes over the advertisement (packet id incremented) and is repeated
for 1.5 s — with the default `min_interval` of 100 ms that is ~15
transmissions, and receivers deduplicate via the packet id. Afterwards the
normal sensor rotation resumes.

`esp32_ble` rotates the advertising slot between its own service
advertisement and every registered raw advertiser, each for
`advertising_cycle_time` (10 s by default), so this component only holds the
radio part of the time. An event raised while another advertiser owns the
slot is kept and sent — the 1.5 s burst only starts then, not when the event
was raised — as soon as the slot comes back, whatever the rotation is
configured to. The worst-case delay is therefore one full rotation:
`advertising_cycle_time × (number of advertisers + 1)`. Lower
`esp32_ble: advertising_cycle_time` if events should go out sooner.

#### Command events

A third action broadcasts BTHome **command events** (object `0x3B`). These are
not a report of something that happened here — they instruct whichever BTHome
device is listening to act, so this turns the ESP32 into a remote control:

```yaml
binary_sensor:
  - platform: gpio
    pin: GPIO9
    id: light_button
    on_click:
      - bthome_broadcaster.command_event:
          command: toggle        # "off" | "on" | toggle | step_up | step_down
      - bthome_broadcaster.command_event:
          command: step_up
          steps: 5               # templatable, only for step_up/step_down
```

Quote `"off"` and `"on"` — YAML would otherwise read them as booleans.

`steps` is part of the encoded object only for `step_up`/`step_down` and is
rejected on the other commands rather than silently ignored; it defaults to 1.

Home Assistant does not consume these — they are meant for another BTHome
device. **Use `encryption_key` with them.** The spec strongly advises it, and
the reason is specific to commands: a plaintext button *report* only leaks
that a button was pressed, while a plaintext *command* can be recorded and
replayed by anyone in range to switch your actuator. Without encryption the
component logs a warning at boot.

A device with **only** events (no sensors) is valid: it advertises the BTHome
*trigger-based device* flag so Home Assistant knows that radio silence is
normal, and stays quiet between events.

A `button_index` above 1 pads the packet with 2 bytes per preceding button.
With `name_placement: advertisement` the deeper indices plus the name can
exceed the 31-byte advertisement; the event is then broadcast **without the
name** rather than not at all — receivers identify the device by MAC, and the
name is still part of every sensor packet. On an events-only device there are
no sensor packets, so a deep `button_index` there means the name is never
advertised at all; use `name_placement: scan_response` (the default) if it
has to be visible.

### Text length limits

BLE advertisements are small: after protocol overhead, a text value can use at
most **19 bytes**. Longer values are truncated at a UTF-8 character boundary,
with a one-time warning in the log. With the default
`name_placement: scan_response` the name does not reduce this budget; with
`name_placement: advertisement` the budget shrinks to `17 − name length`
(as few as **7 bytes** with a full 10-char name). With `encryption_key` the
budget is **11 bytes**.

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
- By default the device name is sent in the **scan response** (its own 31
  bytes), so the full advertisement stays available for sensor data. Active
  scanners — Home Assistant and its Bluetooth proxies by default — pick it up
  automatically; purely passive scanners see the data but no name. Device
  identity is based on the MAC address either way, never on the name. Use
  `name_placement: advertisement` if passive scanners must see the name.
  (A one-time `Ignoring unexpected GAP event type: 5` warning at boot is
  harmless — `esp32_ble` does not recognize the scan-response confirmation
  event, which this component does not rely on.)
- If not all values fit into the 31-byte advertisement, the component
  round-robins over the configured entries: each payload continues where the
  previous one stopped, so all values are broadcast over successive intervals.
- Sensors without a published state (or with a NaN/infinite state) are skipped
  until they have a usable value.
- Integer BTHome types (`battery`, `count`, `temperature_s8`, …) clamp the
  sensor state to the range of the target type before rounding, so an
  out-of-range value is capped rather than silently wrapping around.

## Not (yet) supported

- Multiple text sensors (see above).
- nRF52/Zephyr targets (ESP32 family only).

## License

[MIT](LICENSE)
