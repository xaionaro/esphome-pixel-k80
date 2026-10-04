#pragma once

#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/light/light_state.h"
#include "k80_controller_config.h"
#include "yiscaxia_transport.h"

namespace esphome::yiscaxia {

class YiscaxiaController : public Component {
 public:
  void set_transport(YiscaxiaTransport *transport) { this->transport_ = transport; }
  void set_position_capacity(size_t capacity) { this->capacity_ = capacity; }
  void set_initial_pairs(const std::string &pairs) { this->initial_pairs_ = pairs; }
  float get_setup_priority() const override { return 800.0f; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void register_light(int endpoint, light::LightState *state) { this->lights_[endpoint] = state; }
  bool endpoint_enabled(int endpoint) const;
  bool endpoint_pending(int endpoint) const;
  bool replace_pairs(const std::string &pairs);
  std::string pairs() const { return k80_controller_config::positions(this->queue_); }
  size_t capacity() const { return this->capacity_; }
  bool request_state(int endpoint, const k80_control_values &state, bool once);
  uint16_t transmission_setting(bool spacing) const {
    return spacing ? this->queue_.spacing_ms : this->queue_.attempts;
  }
  bool set_transmission_setting(bool spacing, float value);

 protected:
  bool light_idle_(size_t endpoint) const;
  size_t capacity_{12};
  std::string initial_pairs_{"1A,1B,1C,1D,1E,1F"};
  YiscaxiaTransport *transport_{nullptr};
  k80_controller_queue queue_{};
  std::vector<k80_controller_pending> pending_;
  std::vector<light::LightState *> lights_;
  bool initialized_{false};
};

}  // namespace esphome::yiscaxia
