#pragma once

#include <algorithm>

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "pixel_k80_state_conversion.h"
#include "pixel_k80.h"

namespace esphome::pixel_k80 {

class PixelK80LightOutput;

class PixelK80CustomRainbowEffect final : public light::LightEffect {
 public:
  explicit PixelK80CustomRainbowEffect(PixelK80LightOutput *output)
    : LightEffect("Custom Rainbow"), output_(output) {}
  void start() override;
  void apply() override;
  uint16_t hue() const { return this->hue_; }
  void initialize_hue_if_pending(int hue) {
    if (this->hue_initialization_pending_) {
      this->hue_ = static_cast<uint16_t>(hue % RAINBOW_HUE_PERIOD);
      this->hue_initialization_pending_ = false;
    }
  }
  void set_value(float value) { this->value_ = value; }

 protected:
  bool hue_initialization_pending_{false};
  PixelK80LightOutput *output_;
  uint32_t last_phase_check_ms_{0};
  uint16_t hue_{0};
  float value_{1.0f};
};

class PixelK80NativeEffect final : public light::LightEffect {
 public:
  explicit PixelK80NativeEffect(const char *name) : light::LightEffect(name) {}
  // The lamp runs the selected effect; output writes send its native selector.
  void apply() override {}
};

class PixelK80LightOutput final : public light::LightOutput {
 public:
  explicit PixelK80LightOutput(PixelK80Controller *parent, int endpoint)
    : parent_(parent), endpoint_(endpoint), rainbow_(this) {}

  void update_state(light::LightState *state) override {
    this->initial_restore_write_ = static_cast<PixelK80LightState *>(state)->consume_initial_restore_update();
    this->automatic_phase_ = this->generating_phase_;
    this->manual_pending_ = !this->generating_phase_;
    this->generating_phase_ = false;
  }

  void setup_state(light::LightState *state) override {
    this->parent_->register_light(this->endpoint_, static_cast<PixelK80LightState *>(state));
    if (!this->effects_registered_) {
      state->add_effects({&this->sos_, &this->lightning_1_, &this->lightning_2_, &this->tv_, &this->police_,
                          &this->ambulance_, &this->fire_, &this->circle_1_, &this->circle_2_, &this->rainbow_});
      this->effects_registered_ = true;
    }
  }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    if (this->effects_registered_) {
      traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLOR_TEMPERATURE});
      traits.set_min_mireds(1000000.0f / static_cast<float>(K80_CT_KELVIN_MAX));
      traits.set_max_mireds(1000000.0f / static_cast<float>(K80_CT_KELVIN_MIN));
    }
    return traits;
  }

  void write_state(light::LightState *state) override {
    const bool automatic = this->automatic_phase_;
    const bool initial_off = this->initial_restore_write_ && !state->current_values.is_on();
    this->initial_restore_write_ = false;
    this->automatic_phase_ = false;
    this->manual_pending_ = false;

    // Remapping consumes the logical write, including remembered color, but
    // cancels its radio command even when the old position was disabled.
    const bool remapping = this->parent_->endpoint_remapping(this->endpoint_);
    if (!remapping && !this->parent_->endpoint_enabled(this->endpoint_)) {
      if (state->current_values.get_state() != 0 || state->remote_values.get_state() != 0 ||
          state->get_current_effect_index() != 0 || state->is_transformer_active()) {
        ESP_LOGW("pixel_k80", "Position %d disabled; enable its pair in the configuration field", this->endpoint_ + 1);
        state->make_call().set_state(false).set_transition_length(0).perform();
      }
      return;
    }

    const auto &values = state->current_values;
    if (!unit_value_valid(values.get_state()) || !unit_value_valid(values.get_brightness())) {
      ESP_LOGW("pixel_k80", "Position %d rejected nonfinite/out-of-range state or brightness", this->endpoint_ + 1);
      return;
    }
    float intensity = values.get_state() * values.get_brightness();
    k80_control_values next = this->desired_;
    const auto effect = state->get_current_effect_index();
    if (effect > CUSTOM_RAINBOW_EFFECT_INDEX) {
      ESP_LOGW("pixel_k80", "Position %d rejected unknown native effect", this->endpoint_ + 1);
      return;
    }
    if (effect == CUSTOM_RAINBOW_EFFECT_INDEX) {
      if (values.get_color_mode() == light::ColorMode::RGB) {
        if (!unit_value_valid(values.get_color_brightness()) ||
            !rgb_to_state(values.get_red(), values.get_green(), values.get_blue(),
                          intensity * values.get_color_brightness(), this->desired_, &next))
          return;
        this->rainbow_.set_value(std::max({values.get_red(), values.get_green(), values.get_blue()}));
      } else {
        if (!brightness_to_level(intensity, &next.level)) return;
        next.hue = 0;
        next.saturation = K80_SATURATION_MAX;
        this->rainbow_.set_value(1.0f);
      }
      next.mode = K80_MODE_HSI;
      this->rainbow_.initialize_hue_if_pending(next.hue);
      next.hue = this->rainbow_.hue();
    } else if (effect) {
      if (!brightness_to_level(intensity, &next.level)) return;
      next.mode = K80_MODE_FLS;
      next.effect = static_cast<int>(effect);
    } else {
      if (values.get_color_mode() == light::ColorMode::RGB) {
        if (!unit_value_valid(values.get_color_brightness())) {
          ESP_LOGW("pixel_k80", "Position %d rejected invalid color brightness", this->endpoint_ + 1);
          return;
        }
        intensity *= values.get_color_brightness();
        if (!rgb_to_state(values.get_red(), values.get_green(), values.get_blue(), intensity,
                          this->desired_, &next)) {
          ESP_LOGW("pixel_k80", "Position %d rejected invalid RGB state", this->endpoint_ + 1);
          return;
        }
      } else if (values.get_color_mode() == light::ColorMode::COLOR_TEMPERATURE) {
        if (!ct_to_state(values.get_color_temperature(), intensity, this->desired_, &next)) {
          ESP_LOGW("pixel_k80", "Position %d rejected color temperature outside 2600..10000 K", this->endpoint_ + 1);
          return;
        }
      } else {
        next.mode = this->last_non_fls_mode_;
        if (!brightness_to_level(intensity, &next.level)) return;
      }
    }

    if (!remapping && !initial_off && !this->parent_->request_state(this->endpoint_, next, automatic)) {
      ESP_LOGW("pixel_k80", "Position %d native state request rejected", this->endpoint_ + 1);
      return;
    }
    // Desired color survives OFF and native effects; only the wire OFF is canonical.
    this->desired_ = next;
    if (!effect || effect == CUSTOM_RAINBOW_EFFECT_INDEX) this->last_non_fls_mode_ = next.mode;
  }

 protected:
  friend class PixelK80CustomRainbowEffect;
  // Host-generated Rainbow follows the lamp's native effect selectors.
  static constexpr uint32_t CUSTOM_RAINBOW_EFFECT_INDEX = K80_NATIVE_EFFECT_MAX + 1;
  PixelK80Controller *parent_;
  const int endpoint_;
  int last_non_fls_mode_{K80_MODE_CCT};
  bool effects_registered_{false};
  bool initial_restore_write_{false};
  bool automatic_phase_{false};
  bool generating_phase_{false};
  bool manual_pending_{false};
  k80_control_values desired_{k80_default_controls()};
  PixelK80NativeEffect sos_{"SOS"}, lightning_1_{"Lightning 1"}, lightning_2_{"Lightning 2"},
                       tv_{"TV Screen"}, police_{"Police"}, ambulance_{"Ambulance"}, fire_{"Fire Engine"},
                       circle_1_{"RGB Circle 1"}, circle_2_{"RGB Circle 2"};
  PixelK80CustomRainbowEffect rainbow_;
};

inline void PixelK80CustomRainbowEffect::start() {
  this->hue_initialization_pending_ = true;
  this->hue_ = static_cast<uint16_t>(this->output_->desired_.hue % RAINBOW_HUE_PERIOD);
  this->last_phase_check_ms_ = millis();
}

inline void PixelK80CustomRainbowEffect::apply() {
  const uint32_t now = millis();
  const auto step_spacing_ms = this->output_->parent_->configuration_setting(PixelK80Setting::RAINBOW_STEP_SPACING);
  if (now - this->last_phase_check_ms_ < step_spacing_ms) return;
  this->last_phase_check_ms_ = now;
  if (!this->state_ ||
      this->state_->get_current_effect_index() != PixelK80LightOutput::CUSTOM_RAINBOW_EFFECT_INDEX ||
      this->state_->remote_values.get_state() == 0 ||
      !this->output_->parent_->endpoint_enabled(this->output_->endpoint_) ||
      this->output_->parent_->endpoint_pending(this->output_->endpoint_) || this->output_->manual_pending_)
    return;

  const auto step_degrees = this->output_->parent_->configuration_setting(PixelK80Setting::RAINBOW_STEP_DEGREES);
  this->hue_ = static_cast<uint16_t>((this->hue_ + step_degrees) % RAINBOW_HUE_PERIOD);
  float red, green, blue;
  hsv_to_rgb(this->hue_, this->output_->desired_.saturation / static_cast<float>(K80_SATURATION_MAX),
             this->value_, red, green, blue);
  this->output_->generating_phase_ = true;
  this->state_->make_call()
  .set_rgb(red, green, blue)
  // Synthetic RGB must not activate a remembered dimmer from an inactive mode.
  .set_color_brightness(this->state_->remote_values.get_color_mode() == light::ColorMode::RGB ?
                        this->state_->remote_values.get_color_brightness() : 1.0f)
  .set_transition_length(0)
  .set_publish(false)
  .set_save(false)
  .perform();
  this->output_->generating_phase_ = false;
}

}  // namespace esphome::pixel_k80
