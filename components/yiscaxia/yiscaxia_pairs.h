#pragma once

#include "esphome/components/text/text.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <cstring>
#include "yiscaxia.h"


namespace esphome::yiscaxia {

class YiscaxiaPairs final : public text::Text, public Component {
 public:
  explicit YiscaxiaPairs(YiscaxiaController *parent) : parent_(parent) {}
  const std::string &configuration_status() const { return status_; }
  std::string format_description() const {
    return "Format: 1A,2B,2D; channel1-48 + groupA-F. Up to " +
        std::to_string(parent_->capacity()) + " lights in position order. '-' keeps an interior position disabled; trailing '-' entries are trimmed. Omitted positions disabled; empty disables all. Changed lights must be OFF and idle.";
  }
  float get_setup_priority() const override { return 799.25f; }

  void setup() override {
    preference_ = this->make_entity_preference<SavedPairs>(0x59504302);
    SavedPairs saved{};
    if (preference_.load(&saved)) {
      const auto *terminator = static_cast<const char *>(
          std::memchr(saved.value, '\0', sizeof(saved.value)));
      bool valid = saved.version == 2 && terminator != nullptr;
      if (valid) {
        for (const auto *tail = terminator; tail != saved.value + sizeof(saved.value); ++tail)
          valid &= *tail == '\0';
      }
      if (!valid || !parent_->replace_pairs(
              std::string(saved.value, terminator - saved.value))) {
        ESP_LOGW("k80.pairs", "Saved RF pairs invalid for configured profiles; using defaults");
        status_ = "Saved table invalid; using configured defaults";
      } else {
        known_saved_ = true;
        status_ = "Saved";
      }
    }
    this->publish_state(parent_->pairs());
  }

 protected:
  struct SavedPairs { uint8_t version{2}; char value[256]{}; };
  YiscaxiaController *parent_;
  ESPPreferenceObject preference_;
  std::string status_{"Ready"};
  bool known_saved_{false};

  void control(const std::string &value) override {
    const auto previous = parent_->pairs();
    if (!parent_->replace_pairs(value)) {
      ESP_LOGW("k80.pairs", "Pairs rejected: use unique channel+group entries such as 1A,2B; changed lights must be OFF and idle");
      this->publish_state(parent_->pairs());
      this->status_set_warning("Invalid RF pair table or changed light is busy");
      status_ = "Rejected: unique channel1-48 + groupA-F, comma-separated; capacity exceeded or changed light busy";
      return;
    }
    SavedPairs saved{};
    const auto accepted = parent_->pairs();
    if (known_saved_ && accepted == previous) {
      this->status_clear_warning();
      status_ = "Saved";
      this->publish_state(accepted);
      return;
    }
    std::memcpy(saved.value, accepted.c_str(), accepted.size());
    if (!preference_.save(&saved) || !global_preferences->sync()) {
      known_saved_ = false;
      ESP_LOGW("k80.pairs", "RF pair persistence failed; reverting and retaining previous value");
      parent_->replace_pairs(previous);
      SavedPairs previous_saved{};
      std::memcpy(previous_saved.value, previous.c_str(), previous.size());
      preference_.save(&previous_saved);
      global_preferences->sync();
      this->publish_state(previous);
      this->status_set_warning("RF pair persistence failed; retry the value");
      status_ = "Persistence failed: previous runtime table restored; saved state uncertain; retry";
      return;
    }
    known_saved_ = true;
    this->status_clear_warning();
    status_ = "Saved";
    this->publish_state(accepted);
  }
};

}  // namespace esphome::yiscaxia
