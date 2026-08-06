#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include "esphome/core/automation.h"

#include "bthome_broadcaster.h"

namespace esphome::bthome_broadcaster {

template<typename... Ts> class ButtonEventAction : public Action<Ts...>, public Parented<BTHomeBroadcaster> {
 public:
  void set_event(BTHome::ButtonEventType event) { this->event_ = event; }
  void set_button_index(uint8_t index) { this->button_index_ = index; }

  void play(Ts... x) override { this->parent_->send_button_event(this->button_index_, this->event_); }

 protected:
  BTHome::ButtonEventType event_{BTHome::ButtonEventType::Press};
  uint8_t button_index_{1};
};

template<typename... Ts> class CommandEventAction : public Action<Ts...>, public Parented<BTHomeBroadcaster> {
 public:
  // Only step_up/step_down carry the argument; the factory ignores it for the
  // other opcodes, so it is always passed on.
  TEMPLATABLE_VALUE(uint8_t, steps)

  void set_command(BTHome::CommandEventType command) { this->command_ = command; }

  void play(Ts... x) override { this->parent_->send_command_event(this->command_, this->steps_.value(x...)); }

 protected:
  BTHome::CommandEventType command_{BTHome::CommandEventType::Toggle};
};

template<typename... Ts> class DimmerEventAction : public Action<Ts...>, public Parented<BTHomeBroadcaster> {
 public:
  TEMPLATABLE_VALUE(uint8_t, steps)

  void set_event(BTHome::DimmerEventType event) { this->event_ = event; }

  void play(Ts... x) override { this->parent_->send_dimmer_event(this->event_, this->steps_.value(x...)); }

 protected:
  BTHome::DimmerEventType event_{BTHome::DimmerEventType::RotateLeft};
};

}  // namespace esphome::bthome_broadcaster

#endif  // USE_ESP32
