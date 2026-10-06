#pragma once

#include <cmath>
#include <vector>
#include "esphome/core/component.h"
#include "pixel_k80_light_state.h"
#include "k80_controller_config.h"
#include "pixel_k80_transport.h"

namespace esphome::pixel_k80 {

constexpr uint16_t RAINBOW_HUE_PERIOD = 360;

enum class PixelK80Setting {
  TRANSMISSION_ATTEMPTS,
  CHANNEL_SPACING,
  RAINBOW_STEP_DEGREES,
  RAINBOW_STEP_SPACING,
};

inline uint32_t configuration_setting_max(PixelK80Setting setting) {
  switch (setting) {
    case PixelK80Setting::TRANSMISSION_ATTEMPTS:
      return k80_controller_setting_max(K80_CONTROLLER_SETTING_ATTEMPTS);
    case PixelK80Setting::CHANNEL_SPACING:
      return k80_controller_setting_max(K80_CONTROLLER_SETTING_CHANNEL_SPACING);
    case PixelK80Setting::RAINBOW_STEP_DEGREES:
      return RAINBOW_HUE_PERIOD - 1;  // A full-cycle step would leave the phase unchanged.
    case PixelK80Setting::RAINBOW_STEP_SPACING:
      // Number's float input represents every millisecond and the rejected boundary exactly.
      return (1U << 24) - 1;
    default:
      return 0;
  }
}

inline bool configuration_setting_valid(PixelK80Setting setting, float value) {
  return std::isfinite(value) && value == std::floor(value) && value >= 1 &&
         value <= configuration_setting_max(setting);
}

class PixelK80Controller : public Component {
 public:
  void set_transport(PixelK80Transport *transport) { this->transport_ = transport; }
  void set_position_capacity(size_t capacity) { this->capacity_ = capacity; }
  void set_initial_pairs(const std::string &pairs) { this->initial_pairs_ = pairs; }
  float get_setup_priority() const override { return 800.0f; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void register_light(int endpoint, PixelK80LightState *state) { this->lights_[endpoint] = state; }
  bool endpoint_enabled(int endpoint) const;
  bool endpoint_pending(int endpoint) const;
  bool endpoint_remapping(int endpoint) const { return endpoint == this->remapping_endpoint_; }
  k80_controller_config::Result prepare_pairs(const std::string &pairs, k80_controller_config::Positions *output) const {
    return k80_controller_config::prepare_positions(&this->queue_, pairs, output);
  }
  k80_controller_config::Result apply_pairs(const k80_controller_config::Positions &pairs);
  k80_controller_config::Result replace_pairs(const std::string &pairs);
  std::string pairs() const { return k80_controller_config::positions(this->queue_); }
  size_t capacity() const { return this->capacity_; }
  bool request_state(int endpoint, const k80_control_values &state, bool single_attempt);
  uint16_t transmission_setting(k80_controller_setting setting) const {
    switch (setting) {
      case K80_CONTROLLER_SETTING_ATTEMPTS:
        return this->queue_.attempts;
      case K80_CONTROLLER_SETTING_CHANNEL_SPACING:
        return this->queue_.spacing_ms;
      default:
        return 0;
    }
  }
  bool set_transmission_setting(k80_controller_setting setting, float value);
  uint32_t configuration_setting(PixelK80Setting setting) const;
  bool set_configuration_setting(PixelK80Setting setting, float value);

 protected:
  size_t capacity_{12};
  std::string initial_pairs_{"1A,1B,1C,1D,1E,1F"};
  PixelK80Transport *transport_{nullptr};
  k80_controller_queue queue_{};
  std::vector<k80_controller_pending> pending_;
  std::vector<PixelK80LightState *> lights_;
  bool initialized_{false};
  int remapping_endpoint_{-1};
  uint16_t rainbow_step_degrees_{1};
  uint32_t rainbow_step_spacing_ms_{1000};
};

}  // namespace esphome::pixel_k80
