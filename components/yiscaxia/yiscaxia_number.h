#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <string>
#include "yiscaxia.h"

namespace esphome::yiscaxia {

class YiscaxiaNumber final : public number::Number, public Component {
 public:
  explicit YiscaxiaNumber(YiscaxiaController *parent, bool spacing) : parent_(parent), spacing_(spacing) {}
  float get_setup_priority() const override { return 799.4f; }
  const std::string &configuration_status() const { return this->status_; }

  void setup() override {
    this->preference_ = this->make_entity_preference<Record>(this->spacing_ ? 0x59504303 : 0x59504304);
    Record saved{};
    if (this->preference_.load(&saved)) {
      const auto value = decode(saved);
      if (saved.bytes[0] == 1 && this->valid(value) && this->parent_->set_transmission_setting(this->spacing_, value)) {
        this->known_saved_ = true;
        this->status_ = "Saved";
      } else
        this->status_ = "Saved setting invalid; using configured default";
    }
    this->publish_state(this->parent_->transmission_setting(this->spacing_));
  }

 protected:
  struct Record {
    uint8_t bytes[3]{};
  };
  static_assert(sizeof(Record) == 3);
  YiscaxiaController *parent_;
  bool spacing_;
  bool known_saved_{false};
  ESPPreferenceObject preference_;
  std::string status_{"Ready"};

  bool valid(float value) const {
    return k80_controller_setting_valid(this->spacing_, value);
  }

  static uint16_t decode(const Record &record) {
    return static_cast<uint16_t>(record.bytes[1]) | (static_cast<uint16_t>(record.bytes[2]) << 8);
  }

  static Record encode(uint16_t value) {
    return {{1, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)}};
  }

  void control(float value) override {
    const auto previous = this->parent_->transmission_setting(this->spacing_);
    if (!this->valid(value)) {
      this->status_ = "Rejected: finite whole number within range required";
      this->publish_state(previous);
      return;
    }
    if (this->known_saved_ && value == previous) {
      this->status_ = "Saved";
      this->publish_state(previous);
      return;
    }

    const auto saved = encode(static_cast<uint16_t>(value));
    if (!this->preference_.save(&saved) || !global_preferences->sync()) {
      this->known_saved_ = false;
      const auto recovery = encode(previous);
      this->preference_.save(&recovery);
      global_preferences->sync();
      this->status_ = "Persistence failed; previous value retained, durable setting uncertain";
      this->publish_state(previous);
      return;
    }

    this->parent_->set_transmission_setting(this->spacing_, value);
    this->known_saved_ = true;
    this->status_ = "Saved";
    this->publish_state(value);
  }
};

}  // namespace esphome::yiscaxia
