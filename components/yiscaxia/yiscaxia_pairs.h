#pragma once

#include "esphome/components/text/text.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <cstring>
#include <utility>
#include <vector>
#include "yiscaxia.h"


namespace esphome::yiscaxia {

class YiscaxiaPairs final : public text::Text, public Component {
 public:
  explicit YiscaxiaPairs(YiscaxiaController *parent) : parent_(parent) {}
  const std::string &configuration_status() const { return status_; }
  std::string format_description() const {
    return "Format: 1A,2B,2D; channel1-48 + groupA-F. Up to " +
           std::to_string(parent_->capacity()) +
           " lights in position order. '-' keeps an interior position disabled; trailing '-' entries are trimmed. "
           "Omitted positions disabled; empty disables all. ON remapping is allowed; old pending commands are "
           "canceled, not redirected. Nested edits queue in order; the outermost call completes them before returning.";
  }
  float get_setup_priority() const override { return 799.25f; }

  void setup() override {
    preference_ = this->make_entity_preference<SavedPairs>(0x59504302);
    SavedPairs saved{};
    if (preference_.load(&saved)) {
      const auto *terminator = static_cast<const char *>(
                                 std::memchr(saved.value, '\0', sizeof(saved.value)));
      std::string error;
      if (saved.version != RECORD_VERSION) {
        error = "unsupported record version";
      } else if (!terminator) {
        error = "missing NUL terminator";
      } else {
        for (const auto *tail = terminator; tail != saved.value + sizeof(saved.value); ++tail)
          if (*tail != '\0') {
            error = "nonzero bytes after NUL terminator";
            break;
          }
      }
      if (error.empty())
        error = parent_->replace_pairs(std::string(saved.value, terminator - saved.value)).error;
      if (!error.empty()) {
        ESP_LOGW("k80.pairs", "Saved RF pairs invalid: %s; using defaults", error.c_str());
        status_ = "Saved table invalid: " + error + "; using configured defaults";
      } else {
        known_saved_ = true;
        status_ = "Saved";
      }
    }
    this->publish_state(parent_->pairs());
  }

 protected:
  static constexpr uint8_t RECORD_VERSION = 2;
  struct SavedPairs { uint8_t version{RECORD_VERSION}; char value[256] {}; };
  YiscaxiaController *parent_;
  ESPPreferenceObject preference_;
  std::string status_{"Ready"};
  bool known_saved_{false};
  std::vector<std::string> pending_edits_;
  size_t next_edit_{0};
  bool draining_edits_{false};

  void control(const std::string &value) override {
    this->pending_edits_.push_back(value);
    if (this->draining_edits_) {
      this->mark_pending();
      return;
    }
    this->draining_edits_ = true;
    while (this->next_edit_ < this->pending_edits_.size()) {
      // A local value survives vector reallocation caused by nested edits.
      auto next = std::move(this->pending_edits_[this->next_edit_++]);
      if (this->next_edit_ >= this->pending_edits_.size() - this->next_edit_) {
        this->pending_edits_.erase(this->pending_edits_.begin(), this->pending_edits_.begin() + this->next_edit_);
        this->next_edit_ = 0;
      }
      this->status_ = "Applying";
      this->apply_edit(next);
    }
    std::vector<std::string>().swap(this->pending_edits_);
    this->next_edit_ = 0;
    this->draining_edits_ = false;
  }

  void mark_pending() {
    if (this->status_.find("; pair edit pending") == std::string::npos)
      this->status_ += "; pair edit pending";
  }

  void complete_edit(const std::string &value, std::string outcome, bool warning) {
    this->status_ = std::move(outcome);
    if (this->next_edit_ < this->pending_edits_.size()) this->mark_pending();
    if (warning) this->status_set_warning(this->status_.c_str());
    else this->status_clear_warning();
    this->publish_state(value);
  }

  std::string recover_record(const std::string &previous) {
    SavedPairs saved{};
    std::memcpy(saved.value, previous.c_str(), previous.size());
    const bool recovered_save = this->preference_.save(&saved);
    const bool recovered_sync = global_preferences->sync();
    this->known_saved_ = recovered_save && recovered_sync;
    if (this->known_saved_) return "previous runtime and durable table retained; retry";
    std::string failures;
    if (!recovered_save) failures = "recovery save failed";
    if (!recovered_sync) {
      if (!failures.empty()) failures += "; ";
      failures += "recovery sync failed";
    }
    return failures + "; runtime table unchanged; durable table uncertain; retry";
  }

  void apply_edit(const std::string &value) {
    const auto previous = this->parent_->pairs();
    k80_controller_config::Positions proposed;
    const auto result = this->parent_->prepare_pairs(value, &proposed);
    if (!result) {
      ESP_LOGW("k80.pairs", "Pairs rejected: %s", result.error.c_str());
      this->complete_edit(previous, "Rejected: " + result.error, true);
      return;
    }
    const auto accepted = proposed.text();
    SavedPairs saved{};
    if (this->known_saved_ && accepted == previous) {
      this->complete_edit(accepted, "Saved", false);
      return;
    }
    std::memcpy(saved.value, accepted.c_str(), accepted.size());
    const bool saved_ok = this->preference_.save(&saved);
    const bool synced_ok = saved_ok && global_preferences->sync();
    if (!saved_ok || !synced_ok) {
      const auto outcome = std::string("Persistence failed: ") + (saved_ok ? "sync" : "save") +
                           " failed; " + this->recover_record(previous);
      ESP_LOGW("k80.pairs", "%s", outcome.c_str());
      this->complete_edit(previous, outcome, true);
      return;
    }
    const auto applied = this->parent_->apply_pairs(proposed);
    if (!applied) {
      const auto outcome = "Application failed: " + applied.error + "; " + this->recover_record(previous);
      ESP_LOGE("k80.pairs", "%s", outcome.c_str());
      this->complete_edit(this->parent_->pairs(), outcome, true);
      return;
    }
    this->known_saved_ = true;
    this->complete_edit(accepted, "Saved", false);
  }
};

}  // namespace esphome::yiscaxia
