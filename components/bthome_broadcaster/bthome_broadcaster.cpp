#include "bthome_broadcaster.h"

#ifdef USE_ESP32

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_BTHOME_ENCRYPTION
#include "esphome/core/helpers.h"

#include <esp_mac.h>
#endif

#include <algorithm>
#include <cinttypes>
#include <cstring>

namespace esphome::bthome_broadcaster {

static const char *const TAG = "bthome_broadcaster";

void BTHomeBroadcaster::setup() {
  // A device that only sends events advertises the BTHome trigger-based flag,
  // so receivers know that radio silence is normal and not an outage. Mixed
  // devices keep broadcasting periodically and stay normally monitorable.
  this->trigger_based_ = this->has_events_ && this->entry_count_() == 0;

  // By default the device name goes into the scan response (its own 31 bytes),
  // keeping the full advertisement budget for data. With name_placement:
  // advertisement it goes into the advertisement instead, visible to passive
  // scanners at the cost of measurement space.
  if (this->name_enabled_) {
    std::string name = this->local_name_.empty() ? App.get_name() : this->local_name_;
    if (this->name_in_advertisement_) {
      if (name.size() > kMaxNameLenAdv) {
        name.resize(kMaxNameLenAdv);
        this->adv_name_complete_ = false;
      }
      this->adv_name_ = name;
    } else if (!name.empty()) {
      bool complete = true;
      if (name.size() > kMaxNameLenScanRsp) {
        name.resize(kMaxNameLenScanRsp);
        complete = false;
      }
      this->scan_rsp_data_[0] = static_cast<uint8_t>(1 + name.size());
      this->scan_rsp_data_[1] = complete ? 0x09 : 0x08;  // (Shortened) Local Name AD type.
      memcpy(&this->scan_rsp_data_[2], name.data(), name.size());
      this->scan_rsp_size_ = 2 + name.size();
    }
  }

#ifdef USE_BTHOME_ENCRYPTION
  if (this->encrypted_) {
    // The nonce must contain the same MAC the BLE stack advertises with
    // (BLE_ADDR_TYPE_PUBLIC below), otherwise receivers cannot decrypt.
    uint8_t mac[BTHome::Encryptor::kMacBytes] = {};
    esp_read_mac(mac, ESP_MAC_BT);
    this->encryptor_.setMac(mac);

    // Restore the counter with the safety margin and persist the new base.
    this->counter_pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("bthome_broadcaster_counter"), true);
    uint32_t counter = 0;
    this->counter_pref_.load(&counter);  // Stays 0 on first boot.
    counter += kCounterMargin;
    this->encryptor_.setCounter(counter);
    this->counter_saved_ = counter;
    this->counter_pref_.save(&counter);
    global_preferences->sync();
  }
#endif

  this->adv_params_ = {
      .adv_int_min = static_cast<uint16_t>(this->min_interval_ / 0.625f),
      .adv_int_max = static_cast<uint16_t>(this->max_interval_ / 0.625f),
      // A scan response is only sent for scannable advertisements.
      .adv_type = (this->scan_rsp_size_ > 0) ? ADV_TYPE_SCAN_IND : ADV_TYPE_NONCONN_IND,
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .peer_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .channel_map = ADV_CHNL_ALL,
      .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
  };

  esp32_ble::global_ble->advertising_register_raw_advertisement_callback([this](bool advertise) {
    this->advertising_ = advertise;
    if (advertise) {
      this->on_advertise_();
    }
  });
}

void BTHomeBroadcaster::dump_config() {
  size_t num_sensors = 0;
  size_t num_binary_sensors = 0;
  size_t num_text_sensors = 0;
#ifdef USE_SENSOR
  num_sensors = this->sensors_.size();
#endif
#ifdef USE_BINARY_SENSOR
  num_binary_sensors = this->binary_sensors_.size();
#endif
#ifdef USE_TEXT_SENSOR
  num_text_sensors = (this->text_sensor_ != nullptr) ? 1 : 0;
#endif
  bool encrypted = false;
#ifdef USE_BTHOME_ENCRYPTION
  encrypted = this->encrypted_;
#endif
  ESP_LOGCONFIG(TAG,
                "BTHome Broadcaster:\n"
                "  Interval: %" PRIu32 "ms\n"
                "  Sensors: %u, Binary Sensors: %u, Text Sensors: %u\n"
                "  Name (%s): %s\n"
                "  Encryption: %s\n"
                "  Events: %s",
                this->advertise_interval_, static_cast<unsigned>(num_sensors),
                static_cast<unsigned>(num_binary_sensors), static_cast<unsigned>(num_text_sensors),
                this->name_in_advertisement_ ? "advertisement" : "scan response",
                this->name_enabled_ ? (this->local_name_.empty() ? "(device name)" : this->local_name_.c_str())
                                    : "no",
                encrypted ? "AES-CCM" : "no",
                this->has_events_ ? (this->trigger_based_ ? "yes (trigger-based device)" : "yes") : "no");
}

size_t BTHomeBroadcaster::entry_count_() const {
  size_t total = 0;
#ifdef USE_SENSOR
  total += this->sensors_.size();
#endif
#ifdef USE_BINARY_SENSOR
  total += this->binary_sensors_.size();
#endif
#ifdef USE_TEXT_SENSOR
  if (this->text_sensor_ != nullptr) {
    total += 1;
  }
#endif
  return total;
}

bool BTHomeBroadcaster::measurement_for_(size_t index, BTHome::Measurement &out) const {
#ifdef USE_SENSOR
  if (index < this->sensors_.size()) {
    const auto &entry = this->sensors_[index];
    // Not just NaN: an infinite state would reach a float-to-integer
    // conversion in the factory (or inside bthome-cpp's scaling for the float
    // types), which is undefined. There is nothing sensible to broadcast for
    // it either, so skip the entry as if it had no state.
    if (!entry.source->has_state() || !std::isfinite(entry.source->state)) {
      return false;
    }
    out = entry.factory(entry.source->state);
    return true;
  }
  index -= this->sensors_.size();
#endif
#ifdef USE_BINARY_SENSOR
  if (index < this->binary_sensors_.size()) {
    const auto &entry = this->binary_sensors_[index];
    if (!entry.source->has_state()) {
      return false;
    }
    out = entry.factory(entry.source->state);
    return true;
  }
#endif
  return false;
}

template<typename PacketT> bool BTHomeBroadcaster::fill_packet_(PacketT &packet, size_t budget) {
  packet.add(BTHome::packet_id(this->packet_id_));
  const size_t base_size = packet.size();  // Header + packet_id (+ encryption overhead) bytes.

  const size_t total = this->entry_count_();
  size_t index = this->next_index_ % total;
  bool added_any = false;
  for (size_t visited = 0; visited < total; visited++) {
#ifdef USE_TEXT_SENSOR
    // The text sensor occupies the last rotation slot (after sensors and
    // binary sensors, matching measurement_for_'s index order).
    if (this->text_sensor_ != nullptr && index == total - 1) {
      if (this->text_sensor_->has_state()) {
        if (!this->add_text_(packet, budget, base_size)) {
          break;  // Doesn't fit next to the current entries — it always fits in a fresh packet.
        }
        added_any = true;
      }
      index = (index + 1) % total;
      continue;
    }
#endif
    BTHome::Measurement m{};
    if (this->measurement_for_(index, m)) {
      if (packet.size() + 1 + m.len > budget) {
        break;  // Packet full — rotation continues here on the next build.
      }
      packet.add(m);
      added_any = true;
    }
    index = (index + 1) % total;
  }
  this->next_index_ = index;
  return added_any;
}

void BTHomeBroadcaster::build_next_payload_() {
  this->adv_size_ = 0;
  if (this->entry_count_() == 0) {
    return;
  }

#ifdef USE_BTHOME_ENCRYPTION
  if (this->encrypted_) {
    // name_placement: advertisement is rejected at config time with
    // encryption, so the whole budget belongs to the service-data element.
    // EncryptedPacket::size() already includes the 8-byte overhead.
    BTHome::EncryptedPacket<kPacketCapacity> packet;
    if (!this->fill_packet_(packet, kPacketCapacity)) {
      return;  // No source has published a state yet.
    }
    this->packet_id_++;

    const int size = BTHome::build_encrypted_advertising(packet, this->encryptor_, this->adv_data_, sizeof(this->adv_data_));
    if (size < 0) {
      ESP_LOGW(TAG, "Failed to build encrypted advertisement payload");
      return;
    }
    this->adv_size_ = static_cast<size_t>(size);
    this->save_counter_if_due_();
    return;
  }
#endif

  const char *name = this->adv_name_.empty() ? nullptr : this->adv_name_.c_str();
  const size_t name_bytes = (name != nullptr) ? 2 + this->adv_name_.size() : 0;
  // Maximum size of the BTHome service-data AD element for this advertisement.
  const size_t budget = kPacketCapacity - name_bytes;

  BTHome::Packet<kPacketCapacity> packet;
  if (!this->fill_packet_(packet, budget)) {
    return;  // No source has published a state yet.
  }
  this->packet_id_++;

  const int size =
      BTHome::build_advertising(packet, this->adv_data_, sizeof(this->adv_data_), name, this->adv_name_complete_);
  if (size < 0) {
    ESP_LOGW(TAG, "Failed to build advertisement payload");
    return;
  }
  this->adv_size_ = static_cast<size_t>(size);
}

#ifdef USE_TEXT_SENSOR
template<typename PacketT> bool BTHomeBroadcaster::add_text_(PacketT &packet, size_t budget, size_t base_size) {
  // Truncate to what fits in an otherwise-empty packet (never more than the
  // library's 24-byte limit): the text must always be sendable, otherwise the
  // rotation could stall on an entry that can never fit.
  const size_t max_len = std::min<size_t>(BTHome::VarMeasurement::kMaxBytes, budget - base_size - 2);
  const std::string &state = this->text_sensor_->state;
  size_t len = state.size();
  if (len > max_len) {
    len = max_len;
    // Don't cut in the middle of a UTF-8 multi-byte character.
    while (len > 0 && (static_cast<uint8_t>(state[len]) & 0xC0) == 0x80) {
      len--;
    }
    if (!this->text_truncation_warned_) {
      ESP_LOGW(TAG, "Text '%s' exceeds %u bytes; truncating", state.c_str(), static_cast<unsigned>(max_len));
      this->text_truncation_warned_ = true;
    }
  } else {
    this->text_truncation_warned_ = false;
  }

  char buf[BTHome::VarMeasurement::kMaxBytes + 1];
  memcpy(buf, state.c_str(), len);
  buf[len] = '\0';
  const BTHome::VarMeasurement text = BTHome::text(buf);

  if (packet.size() + 2 + text.len > budget) {
    return false;
  }
  packet.add(text);
  return true;
}
#endif

#ifdef USE_BTHOME_ENCRYPTION
void BTHomeBroadcaster::set_encryption_key(const std::vector<uint8_t> &key) {
  uint8_t k[BTHome::Encryptor::kKeyBytes] = {};
  memcpy(k, key.data(), std::min(key.size(), sizeof(k)));
  this->encryptor_.setKey(k);
  this->encrypted_ = true;
}

void BTHomeBroadcaster::save_counter_if_due_() {
  if (this->encryptor_.counter() - this->counter_saved_ < kCounterMargin) {
    return;
  }
  uint32_t counter = this->encryptor_.counter();
  this->counter_saved_ = counter;
  this->counter_pref_.save(&counter);
  global_preferences->sync();
}
#endif

void BTHomeBroadcaster::on_advertise_() {
  const uint32_t now = millis();
  // A pending event is transmitted at the very next grant, so it never waits
  // longer than one rotation of the advertising slot, however long that is.
  // Waiting more than twice the longest rotation observed so far means the
  // rotation itself stopped in between (BLE disabled, for example); putting a
  // long-past button press on air then would be worse than losing it. Checked
  // before the measurement below, so the stalled gap does not mask itself.
  // The condition is halved rather than the period doubled: a period past
  // 24.8 days would overflow 2 * slot_period_ms_ and then compare against a
  // wrapped-around value.
  if (this->event_pending_ && this->slot_period_ms_ != 0 &&
      (now - this->event_queued_ms_) / 2 > this->slot_period_ms_) {
    ESP_LOGW(TAG, "Event waited %" PRIu32 "ms for the advertising slot (rotation is %" PRIu32 "ms); dropped",
             now - this->event_queued_ms_, this->slot_period_ms_);
    this->event_pending_ = false;
    this->event_active_ = false;
    this->adv_size_ = 0;
    this->has_built_ = false;
  }
  if (this->last_grant_ms_ != 0) {
    this->slot_period_ms_ = std::max(this->slot_period_ms_, now - this->last_grant_ms_);
  }
  this->last_grant_ms_ = now;
  // An active event burst owns the advertisement; the sensor payload is
  // rebuilt when the burst ends.
  if (!this->event_active_ && (!this->has_built_ || (now - this->last_build_ms_) >= this->advertise_interval_)) {
    this->build_next_payload_();
    this->has_built_ = true;
    this->last_build_ms_ = now;
  }
  if (this->adv_size_ == 0) {
    ESP_LOGV(TAG, "No sensor values available yet; skipping advertisement");
    return;
  }

  esp_err_t err;
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
  err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, this->tx_power_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_ble_tx_power_set failed: %s", esp_err_to_name(err));
  }
#endif
  // The scan response never changes, so it is configured once, BEFORE the
  // advertising data. Bluedroid processes both sequentially; advertising then
  // starts on the ADV_DATA_RAW_SET_COMPLETE event. (esp32_ble does not
  // forward SCAN_RSP_DATA_RAW_SET_COMPLETE to component handlers — it logs a
  // harmless one-time "Ignoring unexpected GAP event type: 5" instead.)
  if (this->scan_rsp_size_ > 0 && !this->scan_rsp_configured_) {
    err = esp_ble_gap_config_scan_rsp_data_raw(this->scan_rsp_data_, this->scan_rsp_size_);
    if (err == ESP_OK) {
      this->scan_rsp_configured_ = true;
    } else {
      ESP_LOGE(TAG, "esp_ble_gap_config_scan_rsp_data_raw failed: %s", esp_err_to_name(err));
    }
  }
  err = esp_ble_gap_config_adv_data_raw(this->adv_data_, this->adv_size_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gap_config_adv_data_raw failed: %s", esp_err_to_name(err));
    return;  // A pending event stays pending and is retried on the next slot.
  }
  if (this->event_pending_) {
    // The burst is measured from here, where the controller takes the packet:
    // an event that had to wait for the slot must not expire before it was
    // ever handed over.
    this->event_pending_ = false;
    this->set_timeout("event_burst", kEventBurstMs, [this]() { this->end_event_burst_(); });
  }
}

template<typename AddFn> void BTHomeBroadcaster::send_event_(AddFn &&add_entries) {
  this->packet_id_++;
  int size = -1;
#ifdef USE_BTHOME_ENCRYPTION
  if (this->encrypted_) {
    BTHome::EncryptedPacket<kPacketCapacity> packet;
    packet.setTriggerBased(this->trigger_based_);
    packet.add(BTHome::packet_id(this->packet_id_));
    if (!add_entries(packet)) {
      ESP_LOGW(TAG, "Event does not fit into the encrypted packet; dropped");
      return;
    }
    size = BTHome::build_encrypted_advertising(packet, this->encryptor_, this->adv_data_, sizeof(this->adv_data_));
    this->save_counter_if_due_();
  } else
#endif
  {
    BTHome::Packet<kPacketCapacity> packet;
    packet.setTriggerBased(this->trigger_based_);
    packet.add(BTHome::packet_id(this->packet_id_));
    if (!add_entries(packet)) {
      ESP_LOGW(TAG, "Event does not fit into the packet; dropped");
      return;
    }
    const char *name = this->adv_name_.empty() ? nullptr : this->adv_name_.c_str();
    size = BTHome::build_advertising(packet, this->adv_data_, sizeof(this->adv_data_), name, this->adv_name_complete_);
  }
  if (size < 0) {
    ESP_LOGW(TAG, "Failed to build event advertisement payload");
    return;
  }
  this->adv_size_ = static_cast<size_t>(size);
  this->event_active_ = true;

  // Push the event out immediately when we currently hold the advertising
  // slot; otherwise it goes out when esp32_ble next grants it. The burst
  // timeout is only armed once the controller has the packet — arming it here
  // would let it expire while another advertiser still owns the radio, and
  // the event would be dropped without ever having been transmitted.
  if (this->advertising_) {
    esp_err_t err = esp_ble_gap_config_adv_data_raw(this->adv_data_, this->adv_size_);
    if (err == ESP_OK) {
      this->event_pending_ = false;
      // A follow-up event within the burst replaces packet and timeout.
      this->set_timeout("event_burst", kEventBurstMs, [this]() { this->end_event_burst_(); });
      return;
    }
    ESP_LOGE(TAG, "esp_ble_gap_config_adv_data_raw failed: %s", esp_err_to_name(err));
  }
  this->event_pending_ = true;
  this->event_queued_ms_ = millis();
  // Nothing is on air yet, so a still-running timeout from an earlier burst
  // must not end this one before it started.
  this->cancel_timeout("event_burst");
}

void BTHomeBroadcaster::end_event_burst_() {
  this->event_active_ = false;
  if (this->entry_count_() == 0) {
    // Pure event device: nothing to broadcast between events.
    this->adv_size_ = 0;
    if (this->advertising_) {
      esp_ble_gap_stop_advertising();
    }
    return;
  }
  // Resume the sensor rotation right away instead of repeating the event
  // until the next scheduled build.
  this->has_built_ = false;
  if (this->advertising_) {
    this->on_advertise_();
  }
}

void BTHomeBroadcaster::send_button_event(uint8_t button_index, BTHome::ButtonEventType event) {
  this->send_event_([&](auto &packet) {
    // The k-th 0x3A entry addresses button k: pad earlier buttons with None.
    for (uint8_t i = 1; i < button_index; i++) {
      if (!packet.add(BTHome::button_event(BTHome::ButtonEventType::None))) {
        return false;
      }
    }
    return packet.add(BTHome::button_event(event));
  });
}

void BTHomeBroadcaster::send_dimmer_event(BTHome::DimmerEventType event, uint8_t steps) {
  this->send_event_([&](auto &packet) { return packet.add(BTHome::dimmer_event(event, steps)); });
}

void BTHomeBroadcaster::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (!this->advertising_)
    return;

  esp_err_t err;
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: {
      err = esp_ble_gap_start_advertising(&this->adv_params_);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_start_advertising failed: %s", esp_err_to_name(err));
      }
      break;
    }
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT: {
      err = param->adv_start_cmpl.status;
      if (err != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "BLE adv start failed: %s", esp_err_to_name(err));
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace esphome::bthome_broadcaster

#endif  // USE_ESP32
