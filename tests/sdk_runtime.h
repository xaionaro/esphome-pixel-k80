#pragma once
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/transformers.h"
#include "esphome/core/preferences.h"
#include "pixel_k80_light_state.h"

namespace sdk_test {
extern uint32_t clock_ms;
extern int64_t clock_us;
void set_time_ms(uint32_t value);
void set_time_us(int64_t value);
class LightState : public esphome::pixel_k80::PixelK80LightState {
 public:
  explicit LightState(esphome::light::LightOutput *output, uint32_t key = 0)
    : esphome::pixel_k80::PixelK80LightState(output) {
    set_key(key);
    set_restore_mode(esphome::light::LIGHT_RESTORE_DEFAULT_OFF);
    set_default_transition_length(0);
    set_gamma_correct(1);
  }
  void set_key(uint32_t key) { object_id_hash_ = key; }
  void effect(unsigned index) { active_effect_index_ = index; }
  unsigned effect_index() const { return active_effect_index_; }
  void immediately(const esphome::light::LightColorValues &values) { set_immediately_(values, true); }
  void begin_transition(const esphome::light::LightColorValues &target) {
    transformer_ = std::make_unique<esphome::light::LightTransitionTransformer>();
    transformer_->setup(current_values, target, 1000);
    remote_values = target;
  }
  bool has_transformer() const { return transformer_ != nullptr; }
};
}
