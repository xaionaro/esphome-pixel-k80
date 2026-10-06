#include "pixel_k80.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pixel_k80 {
static const char *const TAG = "pixel_k80";

void PixelK80Controller::setup() {
  this->pending_.resize(this->capacity_);
  this->lights_.resize(this->capacity_, nullptr);
  // Allocate disabled native positions before light and preference setup.
  this->queue_.pending = this->pending_.data();
  this->queue_.count = this->capacity_;
  this->queue_.attempts = K80_CONTROLLER_DEFAULT_ATTEMPTS;
  this->queue_.spacing_ms = K80_CONTROLLER_DEFAULT_SPACING_MS;
  for (auto &position : this->pending_) {
    position.profile = K80_CONTROLLER_PROFILE_NATIVE_RAW_FREQUENCY;
    position.state = k80_default_controls();
  }
  const auto result = this->replace_pairs(this->initial_pairs_);
  if (!result) {
    ESP_LOGE(TAG, "Invalid initial pair table: %s", result.error.c_str());
    this->mark_failed();
    return;
  }
  k80_controller_begin_drain(&this->queue_);
}

bool PixelK80Controller::endpoint_enabled(int endpoint) const {
  return endpoint >= 0 && static_cast<size_t>(endpoint) < this->queue_.count && this->pending_[endpoint].enabled;
}

bool PixelK80Controller::endpoint_pending(int endpoint) const {
  return this->endpoint_enabled(endpoint) && this->pending_[endpoint].remaining != 0;
}

k80_controller_config::Result PixelK80Controller::replace_pairs(const std::string &pairs) {
  k80_controller_config::Positions proposed;
  const auto result = this->prepare_pairs(pairs, &proposed);
  if (!result) return result;
  return this->apply_pairs(proposed);
}

k80_controller_config::Result PixelK80Controller::apply_pairs(const k80_controller_config::Positions &pairs) {
  return k80_controller_config::apply_positions(
  &this->queue_, pairs, [this](size_t endpoint) {
    auto *state = this->lights_[endpoint];
    if (!state) return;
    const auto previous_remapping = this->remapping_endpoint_;
    this->remapping_endpoint_ = static_cast<int>(endpoint);
    state->cancel_pending_output_for_remap();
    this->remapping_endpoint_ = previous_remapping;
  });
}

bool PixelK80Controller::request_state(int endpoint, const k80_control_values &state, bool single_attempt) {
  return k80_controller_request_state_attempts(&this->queue_, endpoint, &state, single_attempt ? 1 : 0);
}

bool PixelK80Controller::set_transmission_setting(k80_controller_setting setting, float value) {
  switch (setting) {
    case K80_CONTROLLER_SETTING_ATTEMPTS:
      return k80_controller_set_attempts(&this->queue_, value);
    case K80_CONTROLLER_SETTING_CHANNEL_SPACING:
      return k80_controller_set_spacing(&this->queue_, value);
    default:
      return false;
  }
}

uint32_t PixelK80Controller::configuration_setting(PixelK80Setting setting) const {
  switch (setting) {
    case PixelK80Setting::TRANSMISSION_ATTEMPTS:
      return this->transmission_setting(K80_CONTROLLER_SETTING_ATTEMPTS);
    case PixelK80Setting::CHANNEL_SPACING:
      return this->transmission_setting(K80_CONTROLLER_SETTING_CHANNEL_SPACING);
    case PixelK80Setting::RAINBOW_STEP_DEGREES:
      return this->rainbow_step_degrees_;
    case PixelK80Setting::RAINBOW_STEP_SPACING:
      return this->rainbow_step_spacing_ms_;
    default:
      return 0;
  }
}

bool PixelK80Controller::set_configuration_setting(PixelK80Setting setting, float value) {
  if (!configuration_setting_valid(setting, value)) return false;
  switch (setting) {
    case PixelK80Setting::TRANSMISSION_ATTEMPTS:
      return this->set_transmission_setting(K80_CONTROLLER_SETTING_ATTEMPTS, value);
    case PixelK80Setting::CHANNEL_SPACING:
      return this->set_transmission_setting(K80_CONTROLLER_SETTING_CHANNEL_SPACING, value);
    case PixelK80Setting::RAINBOW_STEP_DEGREES:
      this->rainbow_step_degrees_ = static_cast<uint16_t>(value);
      return true;
    case PixelK80Setting::RAINBOW_STEP_SPACING:
      this->rainbow_step_spacing_ms_ = static_cast<uint32_t>(value);
      return true;
    default:
      return false;
  }
}

void PixelK80Controller::loop() {
  if (this->is_failed()) return;
  if (!this->initialized_) {
    // Retain actual startup commands and their attempt budgets while RF is muted.
    if (!this->transport_->ready()) return;
    for (auto *state : this->lights_)
      if (state) state->loop();
    this->queue_.draining = 0;
    this->queue_.armed = 1;
    this->initialized_ = true;
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
  pixel_k80_tx_packet packet{};
  if (k80_controller_build_state_packet(position.profile, position.slot, position.group, &state,
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

void PixelK80Controller::dump_config() {
  ESP_LOGCONFIG(TAG, "Pixel K80: %u positions, pairs %s", static_cast<unsigned>(this->capacity_),
                this->pairs().c_str());
}

}  // namespace esphome::pixel_k80
