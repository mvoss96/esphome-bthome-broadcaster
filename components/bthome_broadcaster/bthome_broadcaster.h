#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include "esphome/components/esp32_ble/ble.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
#include <esp_bt.h>
#endif
#include <esp_gap_ble_api.h>

#include <bthome.h>
#ifdef USE_BTHOME_ENCRYPTION
#include "esphome/core/preferences.h"

#include <bthome_crypto_mbedtls.h>
#endif

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace esphome::bthome_broadcaster {

using SensorFactory = BTHome::Measurement (*)(float);
using BinarySensorFactory = BTHome::Measurement (*)(bool);

// Wraps an integer-typed bthome-cpp factory so it can be stored as a
// SensorFactory taking the raw float sensor state.
//
// The state is an arbitrary float and may be outside the target type's range:
// a miscalibrated battery reading of -1, an unfiltered ADC spike of 300. A
// direct narrowing cast wraps silently (-1 becomes 255) or, for signed types,
// is undefined — so clamp first. The clamp is evaluated in double: float
// cannot represent the 32-bit limits exactly, and the nearest float to
// INT32_MAX lies *above* it, which would put the clamped value straight back
// out of range. double is exact for every integer limit involved. NaN and
// infinity are filtered out in measurement_for_ before this is reached.
template<typename T, BTHome::Measurement (*F)(T)> inline BTHome::Measurement rounded_factory(float x) {
  constexpr double kLowest = static_cast<double>(std::numeric_limits<T>::lowest());
  constexpr double kMax = static_cast<double>(std::numeric_limits<T>::max());
  const double v = std::round(static_cast<double>(x));
  return F(static_cast<T>(v < kLowest ? kLowest : (v > kMax ? kMax : v)));
}

class BTHomeBroadcaster final : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void set_advertise_interval(uint32_t interval_ms) { this->advertise_interval_ = interval_ms; }
  void set_min_interval(uint16_t val) { this->min_interval_ = val; }
  void set_max_interval(uint16_t val) { this->max_interval_ = val; }
  void set_name_enabled(bool enabled) { this->name_enabled_ = enabled; }
  void set_local_name(const std::string &name) { this->local_name_ = name; }
  void set_name_in_advertisement(bool val) { this->name_in_advertisement_ = val; }
#ifdef USE_BTHOME_ENCRYPTION
  void set_encryption_key(const std::vector<uint8_t> &key);
#endif
  void set_has_events(bool val) { this->has_events_ = val; }

  // Event actions: the event packet replaces the advertisement immediately
  // and is repeated for kEventBurstMs (receivers dedupe via packet id),
  // then the regular sensor rotation resumes.
  void send_button_event(uint8_t button_index, BTHome::ButtonEventType event);
  void send_dimmer_event(BTHome::DimmerEventType event, uint8_t steps);
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
  void set_tx_power(esp_power_level_t val) { this->tx_power_ = val; }
#endif

#ifdef USE_SENSOR
  void add_sensor(sensor::Sensor *source, SensorFactory factory) {
    this->sensors_.push_back(SensorEntry{source, factory});
  }
#endif
#ifdef USE_BINARY_SENSOR
  void add_binary_sensor(binary_sensor::BinarySensor *source, BinarySensorFactory factory) {
    this->binary_sensors_.push_back(BinarySensorEntry{source, factory});
  }
#endif
#ifdef USE_TEXT_SENSOR
  void set_text_sensor(text_sensor::TextSensor *source) { this->text_sensor_ = source; }
#endif

  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

 protected:
  // Raw BLE advertisements are at most 31 bytes: 3 bytes Flags AD, the rest
  // belongs to the BTHome service-data AD element. The device name lives in
  // the separate 31-byte scan response, so it never eats into the data budget.
  static constexpr size_t kMaxAdvBytes = 31;
  static constexpr size_t kFlagsBytes = 3;
  static constexpr size_t kPacketCapacity = kMaxAdvBytes - kFlagsBytes;
  static constexpr size_t kMaxNameLenScanRsp = kMaxAdvBytes - 2;  // Scan response minus AD overhead.
  static constexpr size_t kMaxNameLenAdv = 10;  // Keeps room for measurements in the advertisement.
  // How long an event packet keeps the advertisement slot. With the default
  // 100ms advertising interval this yields ~15 transmissions per event.
  static constexpr uint32_t kEventBurstMs = 1500;

  void on_advertise_();
  void build_next_payload_();
  size_t entry_count_() const;
  bool measurement_for_(size_t index, BTHome::Measurement &out) const;
  // Templated on the packet type: BTHome::Packet<N> for plaintext,
  // BTHome::EncryptedPacket<N> for encrypted advertisements. Both report the
  // final on-air size via size(), so the budget logic is identical.
  template<typename PacketT> bool fill_packet_(PacketT &packet, size_t budget);
#ifdef USE_TEXT_SENSOR
  template<typename PacketT> bool add_text_(PacketT &packet, size_t budget, size_t base_size);
#endif
#ifdef USE_BTHOME_ENCRYPTION
  void save_counter_if_due_();
#endif
  // Builds and broadcasts an event packet; add_entries(packet) appends the
  // event measurements to either packet type.
  template<typename AddFn> void send_event_(AddFn &&add_entries);
  void end_event_burst_();

#ifdef USE_SENSOR
  struct SensorEntry {
    sensor::Sensor *source;
    SensorFactory factory;
  };
  std::vector<SensorEntry> sensors_;
#endif
#ifdef USE_BINARY_SENSOR
  struct BinarySensorEntry {
    binary_sensor::BinarySensor *source;
    BinarySensorFactory factory;
  };
  std::vector<BinarySensorEntry> binary_sensors_;
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *text_sensor_{nullptr};
  bool text_truncation_warned_{false};
#endif

  uint32_t advertise_interval_{10000};
  uint16_t min_interval_{100};
  uint16_t max_interval_{100};
  bool name_enabled_{true};
  bool name_in_advertisement_{false};
  std::string local_name_;
  std::string adv_name_;  // Non-empty only with name_placement: advertisement.
  bool adv_name_complete_{true};
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
  esp_power_level_t tx_power_{};
#endif

#ifdef USE_BTHOME_ENCRYPTION
  // On boot the counter resumes at last-persisted + margin, so counter values
  // consumed between flash writes can never repeat after a crash or power
  // loss (receivers reject non-increasing counters as replays).
  static constexpr uint32_t kCounterMargin = 1024;
  BTHome::Encryptor encryptor_{&BTHome::mbedtls_ccm_backend};
  bool encrypted_{false};
  ESPPreferenceObject counter_pref_;
  uint32_t counter_saved_{0};
#endif

  uint8_t adv_data_[kMaxAdvBytes];
  size_t adv_size_{0};
  uint8_t scan_rsp_data_[kMaxAdvBytes];
  size_t scan_rsp_size_{0};
  bool scan_rsp_configured_{false};
  size_t next_index_{0};
  uint8_t packet_id_{0};
  bool has_events_{false};
  bool trigger_based_{false};  // Set in setup(): events configured, no periodic entries.
  bool event_active_{false};   // An event burst currently owns the advertisement.
  uint32_t last_build_ms_{0};
  bool has_built_{false};
  bool advertising_{false};
  esp_ble_adv_params_t adv_params_;
};

}  // namespace esphome::bthome_broadcaster

#endif  // USE_ESP32
