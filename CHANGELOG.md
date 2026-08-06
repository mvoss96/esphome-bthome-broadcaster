# Changelog

User-facing changes per release. The release workflow publishes the matching
section as the GitHub release notes and appends the tested-against footer
(ESPHome version, bthome-cpp pin) itself — every tagged version needs a
section here before tagging.

## v0.6.0 (2026-08-06)

### New: command events

```yaml
on_click:
  - bthome_broadcaster.command_event:
      command: toggle        # "off" | "on" | toggle | step_up | step_down
  - bthome_broadcaster.command_event:
      command: step_up
      steps: 5               # templatable, step_up/step_down only
```

BTHome object `0x3B`. Unlike button and dimmer events these are not a report of what happened on this device — they instruct whichever BTHome device is listening, which makes the ESP32 a remote control. Home Assistant does not consume them.

`steps` is part of the encoded object only for `step_up`/`step_down` and is rejected on the other commands rather than silently dropped. Quote `"off"` and `"on"` — YAML would otherwise read them as booleans.

**Use `encryption_key` with command events.** The spec strongly advises it and the reason is specific to commands: a plaintext button *report* only leaks that a button was pressed, while a plaintext *command* can be recorded and replayed by anyone in range to switch your actuator. Without encryption the component warns at boot.

### Encryption counter can no longer wrap

The persisted replay-protection counter is restored with a safety margin on every boot. That addition is now **saturating**:

```
stored=4294967294   before: 1022 (wrapped)   now: 4294967295 (stops)
```

A counter within 1024 of the 32-bit ceiling wrapped back to a low value and re-encrypted with CCM nonces already used under the same key — the one failure that actually breaks the encryption rather than merely inconveniencing a receiver. It now saturates at the ceiling, where bthome-cpp refuses to build a packet, and the log says to generate a new key.

Reaching the ceiling takes about 4.3 billion packets, over a thousand years at the default 10 s interval, so no deployed device is anywhere near it. No action required.

### Dependency

bthome-cpp `v0.3.2` → `v0.5.1`, four releases including the fixes from an external review of the library. No API adaptation was needed.

## v0.5.0 (2026-08-06)

- Fixed codegen against ESPHome 2026.7.4; the minimum supported ESPHome
  version is now enforced at config time (`cv.require_esphome_version`) and
  CI tests against it.
- Events raised outside the advertising slot are no longer lost.
- Out-of-range sensor states are clamped instead of wrapping around.
- `encryption_key` is marked sensitive.
- The advertisement name is dropped when an event packet needs the space.
- Device names truncate at UTF-8 character boundaries instead of mid-character.
- Docs: value precision limits documented, hardware claim softened, the
  example encryption key defused.

## v0.4.1 (2026-07-24)

- bthome-cpp dependency bumped to `v0.3.2`.

## v0.4.0 (2026-07-23)

- **BTHome events**: new `bthome_broadcaster.button_event` and
  `bthome_broadcaster.dimmer_event` actions.

## v0.3.0 (2026-07-21)

- **BTHome AES-CCM encryption** support (`encryption_key`).

## v0.2.0 (2026-07-21)

- Text sensor support + device name via scan response.
- Fix: advertising never started in `scan_response` mode.

## v0.1.0 (2026-07-21)

First release: BTHome v2 broadcaster for ESPHome — verified on real ESP32-C6
hardware with Home Assistant BTHome auto-discovery.

- Sensors + binary sensors (all BTHome v2 types)
- Automatic packet rotation for payloads exceeding 31 bytes
- Advertises via ESPHome's esp32_ble (raw advertisement callback) — no own BLE stack
- Configurable device name, intervals, TX power
