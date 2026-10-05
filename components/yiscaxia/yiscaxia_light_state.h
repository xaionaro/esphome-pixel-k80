#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_output.h"

namespace esphome::yiscaxia {

class YiscaxiaController;

class YiscaxiaLightState : public light::LightState {
 public:
  explicit YiscaxiaLightState(light::LightOutput *output) : LightState(output) {}
  void setup() override {
    this->initial_restore_update_pending_ = true;
    light::LightState::setup();
    this->initial_restore_update_pending_ = false;
  }
  bool consume_initial_restore_update() {
    // Initial-state callbacks may issue ordinary commands before native restore.
    if (!this->initial_restore_update_pending_ || this->initial_state_callback_) return false;
    this->initial_restore_update_pending_ = false;
    return true;
  }

 protected:
  friend class YiscaxiaController;
  void cancel_pending_output_for_remap() {
    if (!this->transformer_ && !this->next_write_) return;
    const auto target = this->transformer_ ? this->transformer_->get_target_values() : this->current_values;
    this->set_immediately_(target, false);
    this->next_write_ = false;
    this->output_->write_state(this);
    this->disable_loop_if_idle_();
  }
  bool initial_restore_update_pending_{false};
};

}  // namespace esphome::yiscaxia
