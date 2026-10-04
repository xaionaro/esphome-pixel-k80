#include "sdk_runtime.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/application.h"
#include <cstdlib>
#include <mutex>
#include <strings.h>

namespace sdk_test {
uint32_t clock_ms{};
int64_t clock_us{};
void set_time_ms(uint32_t value) { clock_ms = value; clock_us = int64_t(value) * 1000; }
void set_time_us(int64_t value) { clock_us = value; clock_ms = uint32_t(value / 1000); }
}
extern "C" int64_t esp_timer_get_time() { return sdk_test::clock_us; }

namespace esphome::light {
LightState::LightState(LightOutput *output) : output_(output) {}
LightTraits LightState::get_traits() { return output_->get_traits(); }
LightCall LightState::make_call() { return LightCall(this); }
void LightState::start_flash_(const LightColorValues &, uint32_t, bool) { std::abort(); }
void LightState::start_transition_(const LightColorValues &, uint32_t, bool) { std::abort(); }
#include "sdk-light-ordering.h"
#include "sdk-light-restore.h"
void LightState::dump_config() {}
void LightState::current_values_as_brightness(float *brightness) { current_values.as_brightness(brightness); }
void LightState::add_effects(const std::initializer_list<LightEffect *> &effects) { effects_ = effects; }
std::unique_ptr<LightTransformer> LightOutput::create_default_transition() { return {}; }
}
namespace esphome {
ESPPreferences *global_preferences;
Application App;
#include "sdk-hsv.h"
bool str_equals_case_insensitive(StringRef a, StringRef b) {
  return a.size() == b.size() && strncasecmp(a.c_str(), b.c_str(), a.size()) == 0;
}
uint32_t millis() { return sdk_test::clock_ms; }
uint32_t micros() { return uint32_t(sdk_test::clock_us); }
void delay(uint32_t ms) { sdk_test::set_time_us(sdk_test::clock_us + int64_t(ms) * 1000); }
void delayMicroseconds(uint32_t us) { sdk_test::set_time_us(sdk_test::clock_us + us); }
void Component::setup() {}
void Component::loop() {}
void Component::dump_config() {}
float Component::get_setup_priority() const { return 0; }
bool Component::can_proceed() { return true; }
void Component::call_setup() {}
void Component::enable_loop_slow_path_() { set_component_state_(COMPONENT_STATE_LOOP); }
void Component::disable_loop() { set_component_state_(COMPONENT_STATE_LOOP_DONE); }
void Component::status_set_warning(const char *) {}
void Component::status_set_error() {}
void Component::status_clear_warning_slow_path_() {}
void Component::mark_failed() { set_component_state_(COMPONENT_STATE_FAILED); }
// Application owns an unused scheduler in the host fixture. Keep its mutex real.
Mutex::Mutex() { handle_ = new std::mutex(); }
Mutex::~Mutex() { delete static_cast<std::mutex *>(handle_); }
void Mutex::lock() { static_cast<std::mutex *>(handle_)->lock(); }
bool Mutex::try_lock() { return static_cast<std::mutex *>(handle_)->try_lock(); }
void Mutex::unlock() { static_cast<std::mutex *>(handle_)->unlock(); }
void log_pin(const char *, const char *, GPIOPin *) {}
}
