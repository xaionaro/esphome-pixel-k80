#pragma once
#include "esphome/components/light/light_state.h"
#include "esphome/core/preferences.h"

namespace sdk_test {
extern uint32_t clock_ms;
extern int64_t clock_us;
void set_time_ms(uint32_t value);
void set_time_us(int64_t value);
class LightState : public esphome::light::LightState {
 public:
  explicit LightState(esphome::light::LightOutput *output, uint32_t key = 0)
      : esphome::light::LightState(output) {
    set_key(key);
    set_restore_mode(esphome::light::LIGHT_RESTORE_DEFAULT_OFF);
    set_default_transition_length(0);
    set_gamma_correct(1);
  }
  void set_key(uint32_t key) { object_id_hash_ = key; }
  void effect(unsigned index) { active_effect_index_ = index; }
  unsigned effect_index() const { return active_effect_index_; }
  void immediately(const esphome::light::LightColorValues &values) { set_immediately_(values, true); }
};
}
