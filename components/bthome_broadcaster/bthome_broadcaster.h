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

#include <cmath>
#include <string>
#include <vector>

namespace esphome::bthome_broadcaster {

using SensorFactory = BTHome::Measurement (*)(float);
using BinarySensorFactory = BTHome::Measurement (*)(bool);

// Wraps an integer-typed bthome-cpp factory so it can be stored as a
// SensorFactory taking the raw float sensor state.
template<typename T, BTHome::Measurement (*F)(T)> inline BTHome::Measurement rounded_factory(float x) {
  return F(static_cast<T>(::lroundf(x)));
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
  static constexpr size_t kMaxNameLen = kMaxAdvBytes - 2;  // Scan response minus AD overhead.

  void on_advertise_();
  void build_next_payload_();
  size_t entry_count_() const;
  bool measurement_for_(size_t index, BTHome::Measurement &out) const;
#ifdef USE_TEXT_SENSOR
  bool add_text_(BTHome::Packet<kPacketCapacity> &packet, size_t base_size);
#endif

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
  std::string local_name_;
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
  esp_power_level_t tx_power_{};
#endif

  uint8_t adv_data_[kMaxAdvBytes];
  size_t adv_size_{0};
  uint8_t scan_rsp_data_[kMaxAdvBytes];
  size_t scan_rsp_size_{0};
  size_t next_index_{0};
  uint8_t packet_id_{0};
  uint32_t last_build_ms_{0};
  bool has_built_{false};
  bool advertising_{false};
  esp_ble_adv_params_t adv_params_;
};

}  // namespace esphome::bthome_broadcaster

#endif  // USE_ESP32
