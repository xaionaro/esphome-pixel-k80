#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <cmath>
#include <string>
#include "yiscaxia.h"

namespace esphome::yiscaxia {

class YiscaxiaNumber final : public number::Number, public Component {
 public:
  explicit YiscaxiaNumber(YiscaxiaController *parent, YiscaxiaSetting setting)
    : parent_(parent), setting_(setting) {}
  float get_setup_priority() const override { return 799.4f; }
  const std::string &configuration_status() const { return this->status_; }

  void setup() override {
    if (configuration_setting_max(this->setting_) == 0) {
      this->status_ = "Unknown configuration setting";
      this->mark_failed();
      return;
    }
    if (this->setting_ == YiscaxiaSetting::RAINBOW_STEP_SPACING)
      this->setup_record<5>();
    else
      this->setup_record<3>();
    this->publish_state(this->parent_->configuration_setting(this->setting_));
  }

 protected:
  static constexpr uint8_t RECORD_VERSION = 1;
  template<size_t Size> struct Record {
    uint8_t bytes[Size] {};
  };
  static_assert(sizeof(Record<3>) == 3);
  static_assert(sizeof(Record<5>) == 5);
  YiscaxiaController *parent_;
  YiscaxiaSetting setting_;
  bool known_saved_{false};
  ESPPreferenceObject preference_;
  std::string status_{"Ready"};

  uint32_t preference_salt() const {
    switch (this->setting_) {
      case YiscaxiaSetting::TRANSMISSION_ATTEMPTS:
        return 0x59504304;
      case YiscaxiaSetting::CHANNEL_SPACING:
        return 0x59504303;
      case YiscaxiaSetting::RAINBOW_STEP_DEGREES:
        return 0x59504305;
      case YiscaxiaSetting::RAINBOW_STEP_SPACING:
        return 0x59504306;
      default:
        return 0;
    }
  }

  template<size_t Size> void setup_record() {
    this->preference_ = this->make_entity_preference<Record<Size>>(this->preference_salt());
    Record<Size> saved{};
    if (this->preference_.load(&saved)) {
      const auto value = decode(saved);
      if (saved.bytes[0] != RECORD_VERSION) {
        this->status_ = "Saved setting invalid: unsupported record version; using configured default";
      } else if (!this->valid(value)) {
        this->status_ = "Saved setting invalid: " + this->invalid_reason(value) + "; using configured default";
      } else if (!this->parent_->set_configuration_setting(this->setting_, value)) {
        this->status_ = "Saved setting not applied; using configured default";
      } else {
        this->known_saved_ = true;
        this->status_ = "Saved";
      }
    }
  }

  bool valid(float value) const {
    return configuration_setting_valid(this->setting_, value);
  }

  std::string invalid_reason(float value) const {
    const auto maximum = configuration_setting_max(this->setting_);
    if (!maximum) return "unknown configuration setting";
    if (!std::isfinite(value)) return "value must be finite";
    if (value != std::floor(value)) return "value must be a whole number";
    return "value must be in 1.." + std::to_string(maximum);
  }

  template<size_t Size> static uint32_t decode(const Record<Size> &record) {
    uint32_t value = 0;
    for (size_t i = 1; i < Size; ++i)
      value |= static_cast<uint32_t>(record.bytes[i]) << ((i - 1) * 8);
    return value;
  }

  template<size_t Size> static Record<Size> encode(uint32_t value) {
    Record<Size> record{};
    record.bytes[0] = RECORD_VERSION;
    for (size_t i = 1; i < Size; ++i)
      record.bytes[i] = static_cast<uint8_t>(value >>((i - 1) * 8));
    return record;
  }

  bool save_value(uint32_t value) {
    // Existing radio settings and degree steps retain uint16 records;
    // only the new long Rainbow interval needs a uint32 record.
    if (this->setting_ == YiscaxiaSetting::RAINBOW_STEP_SPACING) {
      const auto record = encode<5>(value);
      return this->preference_.save(&record);
    }
    const auto record = encode<3>(value);
    return this->preference_.save(&record);
  }

  void control(float value) override {
    const auto previous = this->parent_->configuration_setting(this->setting_);
    if (!this->valid(value)) {
      this->status_ = "Rejected: " + this->invalid_reason(value);
      this->publish_state(previous);
      return;
    }
    if (this->known_saved_ && value == previous) {
      this->status_ = "Saved";
      this->publish_state(previous);
      return;
    }

    const bool saved = this->save_value(static_cast<uint32_t>(value));
    const bool synced = saved && global_preferences->sync();
    if (!saved || !synced) {
      const bool recovery_saved = this->save_value(previous);
      const bool recovery_synced = global_preferences->sync();
      this->known_saved_ = recovery_saved && recovery_synced;
      this->status_ = std::string("Persistence failed: ") + (saved ? "sync" : "save") + " failed; ";
      if (this->known_saved_) {
        this->status_ += "previous runtime and durable setting retained; retry new value";
      } else {
        if (!recovery_saved) this->status_ += "recovery save failed; ";
        if (!recovery_synced) this->status_ += "recovery sync failed; ";
        this->status_ += "previous runtime value retained, durable setting uncertain";
      }
      this->publish_state(previous);
      return;
    }

    if (!this->parent_->set_configuration_setting(this->setting_, value)) {
      this->known_saved_ = false;
      this->status_ = "Saved setting not applied; previous runtime value retained; retry";
      this->publish_state(previous);
      return;
    }
    this->known_saved_ = true;
    this->status_ = "Saved";
    this->publish_state(value);
  }
};

}  // namespace esphome::yiscaxia
