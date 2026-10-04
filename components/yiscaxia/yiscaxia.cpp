#include "yiscaxia.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::yiscaxia {
static const char *const TAG = "yiscaxia";

void YiscaxiaController::setup() {
  this->pending_.resize(this->capacity_);
  this->lights_.resize(this->capacity_, nullptr);
  // Allocate disabled native positions before light and preference setup.
  this->queue_.pending = this->pending_.data();
  this->queue_.count = this->capacity_;
  this->queue_.attempts = K80_CONTROLLER_DEFAULT_ATTEMPTS;
  this->queue_.spacing_ms = K80_CONTROLLER_DEFAULT_SPACING_MS;
  for (auto &position : this->pending_) {
    position.semantic_profile = K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_RAW_FREQUENCY;
    position.state = k80_default_controls();
  }
  if (!this->replace_pairs(this->initial_pairs_)) {
    ESP_LOGE(TAG, "Invalid initial pair table");
    this->mark_failed();
  }
}

bool YiscaxiaController::endpoint_enabled(int endpoint) const {
  return endpoint >= 0 && static_cast<size_t>(endpoint) < this->queue_.count && this->pending_[endpoint].enabled;
}

bool YiscaxiaController::endpoint_pending(int endpoint) const {
  return this->endpoint_enabled(endpoint) && this->pending_[endpoint].remaining != 0;
}

bool YiscaxiaController::light_idle_(size_t endpoint) const {
  const auto *state = this->lights_[endpoint];
  if (!state) return !this->queue_.armed;
  return state->is_idle() && state->current_values.get_state() == 0 &&
      state->remote_values.get_state() == 0 && !state->is_transformer_active() &&
      state->get_current_effect_index() == 0;
}

bool YiscaxiaController::replace_pairs(const std::string &pairs) {
  return k80_controller_config::replace_positions(
      &this->queue_, pairs, [this](size_t endpoint) { return this->light_idle_(endpoint); });
}

bool YiscaxiaController::request_state(int endpoint, const k80_control_values &state, bool once) {
  return k80_controller_request_state_attempts(&this->queue_, endpoint, &state, once ? 1 : 0);
}

bool YiscaxiaController::set_transmission_setting(bool spacing, float value) {
  return spacing ? k80_controller_set_spacing(&this->queue_, value)
                 : k80_controller_set_attempts(&this->queue_, value);
}

void YiscaxiaController::loop() {
  if (this->is_failed()) return;
  if (!this->initialized_) {
    // Flush startup restore while muted, then replay restored ON states once armed.
    if (!this->transport_->ready()) return;
    k80_controller_begin_drain(&this->queue_);
    for (auto *state : this->lights_)
      if (state) state->loop();
    k80_controller_end_drain(&this->queue_);
    this->queue_.armed = 1;
    this->initialized_ = true;
    for (size_t i = 0; i < this->lights_.size(); ++i) {
      auto *state = this->lights_[i];
      if (state && this->endpoint_enabled(i) && state->remote_values.is_on() && state->current_values.is_on()) {
        state->get_output()->update_state(state);
        state->get_output()->write_state(state);
      }
    }
  }

  if (!this->transport_->ready()) {
    k80_controller_halt(&this->queue_);
    this->status_set_error();
    return;
  }

  int endpoint = -1;
  k80_control_values state{};
  if (!k80_controller_take_state(&this->queue_, this->transport_->now_us(), &endpoint, &state)) return;
  const auto &position = this->pending_[endpoint];
  yiscaxia_tx_packet packet{};
  if (k80_controller_build_state_packet(position.semantic_profile, position.slot, position.group, &state,
                                        packet.bytes) != 0) {
    k80_controller_abort(&this->queue_, endpoint);
    return;
  }

  const auto result = this->transport_->transmit(packet, position.slot);
  if (result.trigger_attempted) k80_controller_started(&this->queue_, result.started_us);
  if (result.error) {
    k80_controller_abort(&this->queue_, endpoint);
    ESP_LOGW(TAG, "Transmission failed: %d", result.error);
  }
  if (!result.restored) k80_controller_halt(&this->queue_);
}

void YiscaxiaController::dump_config() {
  ESP_LOGCONFIG(TAG, "Yiscaxia Pixel K80: %u positions, pairs %s", static_cast<unsigned>(this->capacity_),
                this->pairs().c_str());
}

}  // namespace esphome::yiscaxia
