#pragma once

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "yiscaxia_state_conversion.h"
#include "yiscaxia.h"

namespace esphome::yiscaxia {

class YiscaxiaLightOutput;

class YiscaxiaSlowRainbowEffect final : public light::LightEffect {
 public:
  explicit YiscaxiaSlowRainbowEffect(YiscaxiaLightOutput *output)
      : LightEffect("Slow Rainbow"), output_(output) {}
  void start() override;
  void apply() override;
  uint16_t hue() const { return this->hue_; }
  void initialize_hue(int hue) {
    if (this->initialize_) {
      this->hue_ = static_cast<uint16_t>(hue % 360);
      this->initialize_ = false;
    }
  }
  void set_value(float value) { this->value_ = value; }

 protected:
  bool initialize_{false};
  YiscaxiaLightOutput *output_;
  uint32_t last_step_ms_{0};
  uint16_t hue_{0};
  float value_{1.0f};
};

class YiscaxiaNativeEffect final : public light::LightEffect {
 public:
  explicit YiscaxiaNativeEffect(const char *name) : light::LightEffect(name) {}
  // The lamp runs the selected effect; output writes send its native selector.
  void apply() override {}
};

class YiscaxiaLightOutput final : public light::LightOutput {
 public:
  explicit YiscaxiaLightOutput(YiscaxiaController *parent, int endpoint)
      : parent_(parent), endpoint_(endpoint), rainbow_(this) {}

  void update_state(light::LightState *) override {
    this->automatic_phase_ = this->generating_phase_;
    this->manual_pending_ = !this->generating_phase_;
    this->generating_phase_ = false;
  }

  void setup_state(light::LightState *state) override {
    this->parent_->register_light(this->endpoint_, state);
    this->profile_ = K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_RAW_FREQUENCY;
    if (k80_controller_profile_is_native(this->profile_) && !this->effects_registered_) {
      state->add_effects({&this->sos_, &this->lightning_1_, &this->lightning_2_, &this->tv_, &this->police_,
                         &this->ambulance_, &this->fire_, &this->circle_1_, &this->circle_2_, &this->rainbow_});
      this->effects_registered_ = true;
    }
  }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    if (k80_controller_profile_is_native(this->profile_)) {
      traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLOR_TEMPERATURE});
      traits.set_min_mireds(1000000.0f / static_cast<float>(K80_CT_KELVIN_MAX));
      traits.set_max_mireds(1000000.0f / static_cast<float>(K80_CT_KELVIN_MIN));
    } else if (this->profile_ != K80_CONTROLLER_SEMANTIC_PROFILE_NONE) {
      traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    }
    return traits;
  }

  void write_state(light::LightState *state) override {
    const bool automatic = this->automatic_phase_;
    this->automatic_phase_ = false;
    this->manual_pending_ = false;

    if (!this->parent_->endpoint_enabled(this->endpoint_)) {
      if (state->current_values.get_state() != 0 || state->remote_values.get_state() != 0 ||
          state->get_current_effect_index() != 0 || state->is_transformer_active()) {
        ESP_LOGW("k80", "Endpoint %d disabled; enable its pair in the configuration field", this->endpoint_);
        state->make_call().set_state(false).set_transition_length(0).perform();
      }
      return;
    }

    const auto &values = state->current_values;
    if (!unit_value(values.get_state()) || !unit_value(values.get_brightness())) {
      ESP_LOGW("k80", "Endpoint %d rejected nonfinite/out-of-range brightness", this->endpoint_);
      return;
    }
    float intensity = values.get_state() * values.get_brightness();
    k80_control_values next = this->desired_;
    const auto effect = state->get_current_effect_index();
    if (effect > SLOW_RAINBOW_EFFECT_INDEX) {
      ESP_LOGW("k80", "Endpoint %d rejected unknown native effect", this->endpoint_);
      return;
    }
    if (effect == SLOW_RAINBOW_EFFECT_INDEX) {
      if (values.get_color_mode() == light::ColorMode::RGB) {
        if (!unit_value(values.get_color_brightness()) ||
            !rgb_to_state(values.get_red(), values.get_green(), values.get_blue(),
                          intensity * values.get_color_brightness(), this->desired_, &next))
          return;
        int initial_hue;
        float initial_saturation, initial_value;
        rgb_to_hsv(values.get_red(), values.get_green(), values.get_blue(), initial_hue, initial_saturation,
                   initial_value);
        this->rainbow_.initialize_hue(initial_hue);
        this->rainbow_.set_value(initial_value);
      } else {
        if (!brightness_to_level(intensity, &next.level)) return;
        next.hue = 0;
        next.saturation = K80_SATURATION_MAX;
        this->rainbow_.set_value(1.0f);
      }
      next.mode = K80_MODE_HSI;
      this->rainbow_.initialize_hue(next.hue);
      next.hue = this->rainbow_.hue();
    } else if (effect) {
      if (!brightness_to_level(intensity, &next.level)) return;
      next.mode = K80_MODE_FLS;
      next.effect = static_cast<int>(effect);
    } else {
      if (values.get_color_mode() == light::ColorMode::RGB) {
        if (!unit_value(values.get_color_brightness())) {
          ESP_LOGW("k80", "Endpoint %d rejected invalid color brightness", this->endpoint_);
          return;
        }
        intensity *= values.get_color_brightness();
        if (!rgb_to_state(values.get_red(), values.get_green(), values.get_blue(), intensity,
                          this->desired_, &next)) {
          ESP_LOGW("k80", "Endpoint %d rejected invalid RGB state", this->endpoint_);
          return;
        }
      } else if (values.get_color_mode() == light::ColorMode::COLOR_TEMPERATURE) {
        if (!ct_to_state(values.get_color_temperature(), intensity, this->desired_, &next)) {
          ESP_LOGW("k80", "Endpoint %d rejected color temperature outside 2600..10000 K", this->endpoint_);
          return;
        }
      } else {
        next.mode = this->last_non_fls_mode_;
        if (!brightness_to_level(intensity, &next.level)) return;
      }
    }

    if (!this->parent_->request_state(this->endpoint_, next, automatic)) {
      ESP_LOGW("k80", "Endpoint %d native state request rejected", this->endpoint_);
      return;
    }
    // Desired color survives OFF and native effects; only the wire OFF is canonical.
    this->desired_ = next;
    if (!effect || effect == SLOW_RAINBOW_EFFECT_INDEX) this->last_non_fls_mode_ = next.mode;
  }

 protected:
  friend class YiscaxiaSlowRainbowEffect;
  // Host-generated Rainbow follows the lamp's native effect selectors.
  static constexpr uint32_t SLOW_RAINBOW_EFFECT_INDEX = K80_NATIVE_EFFECT_MAX + 1;
  YiscaxiaController *parent_;
  const int endpoint_;
  int profile_{K80_CONTROLLER_SEMANTIC_PROFILE_NONE};
  int last_non_fls_mode_{K80_MODE_CCT};
  bool effects_registered_{false};
  bool automatic_phase_{false};
  bool generating_phase_{false};
  bool manual_pending_{false};
  k80_control_values desired_{k80_default_controls()};
  YiscaxiaNativeEffect sos_{"SOS"}, lightning_1_{"Lightning 1"}, lightning_2_{"Lightning 2"},
      tv_{"TV Screen"}, police_{"Police"}, ambulance_{"Ambulance"}, fire_{"Fire Engine"},
      circle_1_{"RGB Circle 1"}, circle_2_{"RGB Circle 2"};
  YiscaxiaSlowRainbowEffect rainbow_;
};

inline void YiscaxiaSlowRainbowEffect::start() {
  this->initialize_ = true;
  this->hue_ = static_cast<uint16_t>(this->output_->desired_.hue % 360);
  this->last_step_ms_ = millis();
}

inline void YiscaxiaSlowRainbowEffect::apply() {
  const uint32_t now = millis();
  if (now - this->last_step_ms_ < 1000) return;
  this->last_step_ms_ = now;
  if (!this->state_ ||
      this->state_->get_current_effect_index() != YiscaxiaLightOutput::SLOW_RAINBOW_EFFECT_INDEX ||
      this->state_->remote_values.get_state() == 0 ||
      !this->output_->parent_->endpoint_enabled(this->output_->endpoint_) ||
      this->output_->parent_->endpoint_pending(this->output_->endpoint_) || this->output_->manual_pending_)
    return;

  this->hue_ = static_cast<uint16_t>((this->hue_ + 1) % 360);
  float red, green, blue;
  hsv_to_rgb(this->hue_, this->output_->desired_.saturation / static_cast<float>(K80_SATURATION_MAX),
             this->value_, red, green, blue);
  this->output_->generating_phase_ = true;
  this->state_->make_call()
      .set_rgb(red, green, blue)
      .set_transition_length(0)
      .set_publish(false)
      .set_save(false)
      .perform();
  this->output_->generating_phase_ = false;
}

}  // namespace esphome::yiscaxia
