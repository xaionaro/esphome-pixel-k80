#include "sdk_runtime.h"
#include "test_check.h"
#include "pixel_k80_light.h"
#define protected public
#include "pixel_k80_pairs.h"
#undef protected
#include "esphome/core/preferences.h"
#include "esphome/core/controller_registry.h"

#include <array>
#include <cstring>
#include <memory>
#include <functional>
#include <vector>

using namespace esphome;
using namespace esphome::light;
using namespace esphome::pixel_k80;

class TestController : public PixelK80Controller {
 public:
  const k80_controller_pending &pending(size_t i) const { return pending_[i]; }
  const k80_controller_queue &queue() const { return queue_; }
  void abort(size_t i) { k80_controller_abort(&queue_, i); }
};

// Only the hardware boundary is substituted. TestController, output, LightCall,
// deferred LightState ordering and preference restore execute production code.
class Transport : public PixelK80Transport {
 public:
  struct Transmission { pixel_k80_tx_packet packet; int slot; };
  bool available{true};
  uint64_t now{};
  pixel_k80_tx_result result{};
  std::vector<Transmission> transmissions;
  Transport() { result.restored = 1; result.trigger_attempted = 1; }
  bool ready() const override { return available; }
  uint64_t now_us() const override { return now; }
  pixel_k80_tx_result transmit(const pixel_k80_tx_packet &packet, int slot) override {
    transmissions.push_back({packet, slot});
    return result;
  }
  void advance(uint64_t value) { now = value; result.started_us = value; }
};

class Publications : public LightRemoteValuesListener {
 public:
  unsigned count{};
  void on_light_remote_values_update() override { ++count; }
};

static void initialize(TestController &controller, Transport &transport,
                       const char *pairs = "1A,1B,1C,1D,1E,1F") {
  controller.set_transport(&transport);
  controller.set_initial_pairs(pairs);
  controller.setup();
}

static void configure_light(sdk_test::LightState &state, uint32_t key) {
  state.set_key(key);
  state.set_default_transition_length(0);
  state.set_gamma_correct(1);
  state.set_restore_mode(LIGHT_RESTORE_DEFAULT_OFF);
  state.setup();
}

static int semantic_and_effects() {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  TestController controller;
  Transport transport;
  initialize(controller, transport);
  PixelK80LightOutput output(&controller, 0), disabled_output(&controller, 11);
  sdk_test::LightState state(&output), disabled(&disabled_output);
  configure_light(state, 0x8100);
  configure_light(disabled, 0x810b);
  CHECK(!state.remote_values.is_on());
  CHECK(state.get_effects().size() == 10);
  const char *names[] = {"SOS", "Lightning 1", "Lightning 2", "TV Screen", "Police",
                         "Ambulance", "Fire Engine", "RGB Circle 1", "RGB Circle 2", "Custom Rainbow"
                        };
  for (size_t i = 0; i < 10; ++i) CHECK(state.get_effects()[i]->get_name() == names[i]);
  output.setup_state(&state);
  CHECK(state.get_effects().size() == 10);
  CHECK(state.get_traits().supports_color_mode(ColorMode::RGB));
  CHECK(state.get_traits().supports_color_mode(ColorMode::COLOR_TEMPERATURE));
  controller.loop();
  CHECK(controller.queue().armed && transport.transmissions.empty());
  state.make_call().set_state(true).set_rgb(0, 1, 0).set_brightness(.25f)
  .set_color_brightness(.5f).set_transition_length(0).perform();
  CHECK(!controller.endpoint_pending(0));
  state.loop();
  CHECK(controller.pending(0).state.mode == K80_MODE_HSI);
  CHECK(controller.pending(0).state.hue == 120 && controller.pending(0).state.saturation == 100);
  CHECK(controller.pending(0).state.level == 13 && controller.pending(0).remaining == 3);
  for (uint32_t i = 1; i <= 9; ++i) {
    controller.abort(0);
    state.make_call().set_effect(i).set_transition_length(0).perform();
    state.loop();
    CHECK(controller.pending(0).state.mode == K80_MODE_FLS);
    CHECK(controller.pending(0).state.effect == static_cast<int>(i));
    CHECK(controller.pending(0).state.level == 25 && controller.pending(0).remaining == 3);
    controller.abort(0);
    sdk_test::set_time_ms(i * 1000);
    state.loop();
    CHECK(!controller.endpoint_pending(0)); // Autonomous lamp effect adds no host train.
  }
  state.make_call().set_effect("None").set_rgb(.5f, .5f, .5f)
  .set_transition_length(0).perform();
  state.loop();
  CHECK(state.get_current_effect_index() == 0);
  CHECK(controller.pending(0).state.mode == K80_MODE_HSI);
  CHECK(controller.pending(0).state.saturation == 0 && controller.pending(0).state.level == 13);
  state.make_call().set_color_mode(ColorMode::COLOR_TEMPERATURE)
  .set_color_temperature(1000000.0f / 2700).set_transition_length(0).perform();
  state.loop();
  CHECK(controller.pending(0).state.mode == K80_MODE_CCT);
  CHECK(controller.pending(0).state.ct_index == 1 && controller.pending(0).state.level == 25);
  const auto valid = controller.pending(0);
  state.current_values.set_color_temperature(99);
  output.write_state(&state);
  CHECK(std::memcmp(&valid, &controller.pending(0), sizeof(valid)) == 0);
  state.current_values.set_color_temperature(1000000.0f / 2700);
  state.effect(11);
  output.write_state(&state);
  CHECK(std::memcmp(&valid, &controller.pending(0), sizeof(valid)) == 0);
  state.effect(0);
  state.make_call().set_rgb(0, 0, 1).set_transition_length(0).perform();
  state.loop();
  controller.abort(0);
  state.make_call().set_state(false).set_transition_length(0).perform();
  CHECK(!state.current_values.is_on() && !state.remote_values.is_on());
  CHECK(!state.is_idle() && !controller.endpoint_pending(0));
  CHECK(controller.replace_pairs("2A,1B,1C,1D,1E,1F"));
  CHECK(!state.current_values.is_on() && !state.remote_values.is_on() && !controller.endpoint_pending(0));
  CHECK(state.is_idle()); // Deferred old-address OFF was consumed, not redirected.
  state.loop();
  const auto before_remap_loop = transport.transmissions.size();
  controller.loop();
  CHECK(transport.transmissions.size() == before_remap_loop);
  CHECK(controller.replace_pairs("1A,1B,1C,1D,1E,1F"));
  controller.abort(0);
  state.make_call().set_state(true).set_transition_length(0).perform();
  state.loop();
  CHECK(controller.pending(0).state.hue == 240 && controller.pending(0).state.saturation == 100);
  disabled.make_call().set_state(true).set_effect("SOS").set_transition_length(0).perform();
  disabled.loop();
  disabled.loop();
  CHECK(!disabled.current_values.is_on() && !disabled.remote_values.is_on());
  CHECK(disabled.get_current_effect_index() == 0 && !controller.endpoint_pending(11));
  return 0;
}

static int queue_and_transport() {
  TestController controller;
  Transport transport;
  initialize(controller, transport, "1A,1B,2A");
  controller.loop();
  k80_control_values first{K80_MODE_HSI, 30, 1, 120, 100, 1};
  CHECK(controller.request_state(0, first, false));
  controller.loop();
  CHECK(transport.transmissions.size() == 1 && controller.pending(0).remaining == 2);
  CHECK(controller.queue().started_slots == 1 && controller.queue().slot_started_us[0] == 0);
  CHECK(controller.request_state(0, first, false));
  CHECK(controller.pending(0).remaining == 2); // Identical request preserves spent budget.
  CHECK(controller.set_transmission_setting(K80_CONTROLLER_SETTING_ATTEMPTS, 7));
  CHECK(controller.pending(0).remaining == 2);
  first.hue = 240;
  CHECK(controller.request_state(0, first, false));
  CHECK(controller.pending(0).remaining == 7);
  CHECK(controller.request_state(1, first, false));
  transport.advance(149999);
  controller.loop();
  CHECK(transport.transmissions.size() == 1);
  CHECK(controller.request_state(2, first, false));
  controller.loop();
  CHECK(transport.transmissions.size() == 2 && transport.transmissions.back().slot == 1);
  transport.advance(150000);
  controller.loop();
  CHECK(transport.transmissions.size() == 3 && transport.transmissions.back().slot == 0);
  CHECK(transport.transmissions.back().packet.bytes[5] == 240);
  controller.abort(0);
  controller.abort(1);
  controller.abort(2);
  transport.result.trigger_attempted = 0;
  transport.result.error = -1;
  transport.advance(300000);
  CHECK(controller.request_state(0, first, false));
  const auto stamped = controller.queue().slot_started_us[0];
  controller.loop();
  CHECK(!controller.endpoint_pending(0) && controller.queue().slot_started_us[0] == stamped);
  CHECK(controller.queue().armed); // Recoverable pretrigger failure does not halt.
  transport.result.trigger_attempted = 1;
  transport.result.restored = 0;
  transport.advance(450000);
  CHECK(controller.request_state(0, first, false));
  controller.loop();
  CHECK(!controller.queue().armed && !controller.endpoint_pending(0));
  CHECK(controller.queue().slot_started_us[0] == 450000);
  const auto count = transport.transmissions.size();
  controller.loop();
  CHECK(transport.transmissions.size() == count);
  TestController other;
  Transport other_transport;
  initialize(other, other_transport);
  other.loop();
  CHECK(other.transmission_setting(K80_CONTROLLER_SETTING_ATTEMPTS) == 3 &&
        other.transmission_setting(K80_CONTROLLER_SETTING_CHANNEL_SPACING) == 150);
  CHECK(other.request_state(0, first, false));
  other.loop();
  CHECK(other_transport.transmissions.size() == 1 && other.queue().slot_started_us[0] == 0);
  CHECK(controller.transmission_setting(K80_CONTROLLER_SETTING_ATTEMPTS) == 7 && !controller.queue().armed);
  return 0;
}

static int rainbow_startup() {
  struct Case { float red, green, blue; int hue; };
  const Case cases[] = {{1, .01f, 0, 1}, {.5f, .5f, .5f, 120},
    {0, 0, 0, 120}, {1, 0, .001f, 0}
  };
  for (const auto &input : cases) {
    ESPPreferences preferences;
    global_preferences = &preferences;
    host::fault_preferences = &preferences;
    sdk_test::set_time_ms(0);
    TestController controller;
    Transport transport;
    initialize(controller, transport);
    PixelK80LightOutput output(&controller, 0);
    sdk_test::LightState state(&output);
    configure_light(state, 0x8400);
    controller.loop();
    state.make_call().set_state(true).set_rgb(0, 1, 0).set_brightness(.25f)
    .set_color_brightness(.5f).set_transition_length(0).perform();
    state.loop();
    controller.abort(0);
    state.make_call().set_rgb(input.red, input.green, input.blue)
    .set_effect("Custom Rainbow").set_transition_length(0).perform();
    state.loop();
    CHECK(controller.pending(0).state.hue == input.hue);
    CHECK(controller.pending(0).state.level == 13 && controller.pending(0).remaining == 3);
    const float value = std::max({state.current_values.get_red(), state.current_values.get_green(),
                                  state.current_values.get_blue()});
    const auto saves = preferences.saves;
    controller.abort(0);
    sdk_test::set_time_ms(1000);
    state.loop();
    CHECK(controller.pending(0).state.hue == (input.hue + 1) % 360);
    CHECK(controller.pending(0).remaining == 1 && preferences.saves == saves);
    CHECK(std::max({state.current_values.get_red(), state.current_values.get_green(),
                    state.current_values.get_blue()}) == value);
  }
  return 0;
}

static int rainbow_color_modes() {
  for (float color_brightness : {.25f, .5f, 1.f}) {
    ESPPreferences preferences;
    global_preferences = &preferences;
    host::fault_preferences = &preferences;
    sdk_test::set_time_ms(0);
    TestController controller;
    Transport transport;
    initialize(controller, transport);
    PixelK80LightOutput output(&controller, 0);
    sdk_test::LightState state(&output);
    configure_light(state, 0x8500);
    controller.loop();
    state.make_call().set_state(true).set_rgb(0, 1, 0).set_brightness(.5f)
    .set_color_brightness(color_brightness).set_transition_length(0).perform();
    state.loop();
    controller.abort(0);
    state.make_call().set_color_mode(ColorMode::COLOR_TEMPERATURE)
    .set_color_temperature(1000000.f / 2700).set_transition_length(0).perform();
    state.loop();
    controller.abort(0);
    state.make_call().set_effect("Custom Rainbow").set_transition_length(0).perform();
    state.loop();
    CHECK(controller.pending(0).state.level == 50 && controller.pending(0).state.hue == 0);
    const auto saves = preferences.saves;
    Publications publications;
    state.add_remote_values_listener(&publications);
    for (uint32_t phase = 1; phase <= 2; ++phase) {
      controller.abort(0);
      sdk_test::set_time_ms(phase * 1000);
      state.loop();
      CHECK(controller.pending(0).state.level == 50 && controller.pending(0).remaining == 1);
      CHECK(controller.pending(0).state.hue == static_cast<int>(phase));
      CHECK(state.remote_values.get_color_mode() == ColorMode::COLOR_TEMPERATURE);
      CHECK(state.remote_values.get_color_brightness() == color_brightness);
      CHECK(preferences.saves == saves && publications.count == 0);
    }
    // Returning to RGB reactivates its own dimmer; the effect must preserve it.
    state.make_call().set_effect("None").set_rgb(0, 1, 0)
    .set_transition_length(0).perform();
    state.loop();
    controller.abort(0);
    state.make_call().set_effect("Custom Rainbow").set_transition_length(0).perform();
    state.loop();
    const int rgb_level = k80_quantize_brightness(.5f * color_brightness);
    CHECK(controller.pending(0).state.level == rgb_level);
    controller.abort(0);
    sdk_test::set_time_ms(3000);
    state.loop();
    CHECK(controller.pending(0).state.level == rgb_level && controller.pending(0).remaining == 1);
  }
  return 0;
}

static int rainbow_configuration() {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  TestController controller;
  Transport transport;
  initialize(controller, transport);
  CHECK(controller.configuration_setting(PixelK80Setting::RAINBOW_STEP_DEGREES) == 1);
  CHECK(controller.configuration_setting(PixelK80Setting::RAINBOW_STEP_SPACING) == 1000);
  CHECK(controller.set_configuration_setting(PixelK80Setting::RAINBOW_STEP_DEGREES, 30));
  CHECK(controller.set_configuration_setting(PixelK80Setting::RAINBOW_STEP_SPACING, 200));
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x8600);
  controller.loop();
  state.make_call().set_state(true).set_rgb(0, 1, 0).set_brightness(.5f)
  .set_effect("Custom Rainbow").set_transition_length(0).perform();
  state.loop();
  controller.abort(0);
  const auto saves = preferences.saves;
  Publications publications;
  state.add_remote_values_listener(&publications);
  sdk_test::set_time_ms(199);
  state.loop();
  CHECK(!controller.endpoint_pending(0));
  sdk_test::set_time_ms(200);
  state.loop();
  CHECK(controller.pending(0).state.hue == 150 && controller.pending(0).remaining == 1);
  controller.abort(0);
  CHECK(controller.set_configuration_setting(PixelK80Setting::RAINBOW_STEP_DEGREES, 240));
  CHECK(controller.set_configuration_setting(PixelK80Setting::RAINBOW_STEP_SPACING, 300));
  sdk_test::set_time_ms(499);
  state.loop();
  CHECK(!controller.endpoint_pending(0));
  sdk_test::set_time_ms(500);
  state.loop();
  CHECK(controller.pending(0).state.hue == 30 && controller.pending(0).remaining == 1);
  CHECK(preferences.saves == saves && publications.count == 0);
  CHECK(controller.pending(0).state.level == 50 && state.remote_values.get_green() == 1);
  return 0;
}

static int rainbow_and_restore() {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  TestController controller;
  Transport transport;
  initialize(controller, transport);
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x8200);
  controller.loop();
  Publications publications;
  state.add_remote_values_listener(&publications);
  state.make_call().set_state(true).set_rgb(0, 1, 0).set_brightness(.25f)
  .set_color_brightness(.5f).set_effect("Custom Rainbow").set_transition_length(0).perform();
  state.loop();
  CHECK(controller.pending(0).state.hue == 120 && controller.pending(0).remaining == 3);
  CHECK(preferences.sync());
  const auto saved = preferences.durable.at(0x8200);
  const auto saves = preferences.saves;
  const auto published = publications.count;
  sdk_test::set_time_ms(1000);
  state.loop();
  CHECK(controller.pending(0).state.hue == 120 && controller.pending(0).remaining == 3);
  controller.abort(0);
  sdk_test::set_time_ms(1999);
  state.loop();
  CHECK(!controller.endpoint_pending(0));
  sdk_test::set_time_ms(2000);
  state.loop();
  CHECK(controller.pending(0).state.hue == 121 && controller.pending(0).remaining == 1);
  CHECK(!state.is_transformer_active());
  CHECK(preferences.saves == saves && publications.count == published);
  CHECK(preferences.durable.at(0x8200) == saved);
  CHECK(controller.set_transmission_setting(K80_CONTROLLER_SETTING_ATTEMPTS, 255));
  controller.abort(0);
  sdk_test::set_time_ms(3000);
  state.make_call().set_brightness(.4f).set_transition_length(0)
  .set_publish(false).set_save(false).perform();
  state.loop();
  CHECK(controller.pending(0).remaining == 255 && controller.pending(0).state.hue == 121);
  controller.abort(0);
  sdk_test::set_time_ms(4000);
  state.loop();
  CHECK(controller.pending(0).remaining == 1 && controller.pending(0).state.level == 13);
  controller.abort(0);
  for (uint32_t i = 0; i < 238; ++i) {
    sdk_test::set_time_ms(5000 + i * 1000);
    state.loop();
    CHECK(controller.pending(0).remaining == 1);
    controller.abort(0);
  }
  auto *rainbow = static_cast<PixelK80CustomRainbowEffect *>(state.get_effects()[9]);
  CHECK(rainbow->hue() == 0); // 120 + 2 + 238 degrees wraps exactly.
  struct NestedManual : LightTargetStateReachedListener {
    LightState *state{};
    bool armed{true};
    void on_light_target_state_reached() override {
      if (!armed) return;
      armed = false;
      state->make_call().set_brightness(.3f).set_transition_length(0)
      .set_publish(false).set_save(false).perform();
    }
  } nested;
  nested.state = &state;
  state.add_target_state_reached_listener(&nested);
  sdk_test::set_time_ms(243000);
  state.loop();
  CHECK(!nested.armed && controller.pending(0).remaining == 255);
  controller.abort(0);
  TestController reboot;
  Transport reboot_transport;
  reboot_transport.available = false;
  initialize(reboot, reboot_transport);
  CHECK(reboot.set_transmission_setting(K80_CONTROLLER_SETTING_ATTEMPTS, 7));
  PixelK80LightOutput reboot_output(&reboot, 0);
  sdk_test::LightState restored(&reboot_output);
  configure_light(restored, 0x8200);
  CHECK(restored.remote_values.is_on() && restored.get_current_effect_index() == 10);
  CHECK(restored.remote_values.get_green() == 1 && restored.remote_values.get_brightness() == .25f);
  reboot.loop();
  CHECK(!reboot.queue().armed && reboot_transport.transmissions.empty());
  CHECK(!reboot.endpoint_pending(0));
  const auto before_replay = preferences.saves;
  reboot_transport.available = true;
  reboot.loop();
  CHECK(reboot.queue().armed && reboot_transport.transmissions.size() == 1);
  CHECK(reboot.pending(0).remaining == 6 && reboot.pending(0).state.hue == 120);
  CHECK(preferences.saves == before_replay);
  CHECK(reboot_transport.transmissions[0].packet.bytes[2] == K80_MODE_HSI);
  restored.make_call().set_state(false).set_transition_length(0).perform();
  restored.loop();
  CHECK(restored.get_current_effect_index() == 0 && reboot.pending(0).remaining == 7);
  CHECK(preferences.sync());
  TestController off_reboot;
  Transport off_transport;
  initialize(off_reboot, off_transport);
  PixelK80LightOutput off_output(&off_reboot, 0);
  sdk_test::LightState off_state(&off_output);
  configure_light(off_state, 0x8200);
  off_reboot.loop();
  CHECK(!off_state.remote_values.is_on() && off_state.get_current_effect_index() == 0);
  CHECK(off_transport.transmissions.empty() && !off_reboot.endpoint_pending(0));
  return 0;
}

static int registry_and_static_restore() {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  const char *table = "1A,1B,1C,1D,1E,1F,2A,2B,2C,2D,2E,2F";
  TestController controller;
  Transport transport;
  initialize(controller, transport, table);
  std::array<std::unique_ptr<PixelK80LightOutput>, 12> outputs;
  std::array<std::unique_ptr<sdk_test::LightState>, 12> lights;
  for (size_t i = 0; i < lights.size(); ++i) {
    outputs[i] = std::make_unique<PixelK80LightOutput>(&controller, i);
    lights[i] = std::make_unique<sdk_test::LightState>(outputs[i].get());
    configure_light(*lights[i], 0x8300 + i);
    CHECK(lights[i]->get_effects().size() == 10);
    for (size_t j = 0; j < i; ++j)
      CHECK(lights[i]->get_effects()[9] != lights[j]->get_effects()[9]);
  }
  controller.loop();
  CHECK(transport.transmissions.empty());
  for (size_t tail : {size_t{6}, size_t{11}}) {
    auto &state = *lights[tail];
    state.make_call().set_state(true).set_rgb(0, 1, 0).set_transition_length(0).perform();
    state.loop();
    controller.abort(tail);
    state.make_call().set_rgb(1, 0, 0).set_transition_length(0).perform();
    const auto before = controller.queue();
    std::array<k80_controller_pending, 12> pending;
    for (size_t i = 0; i < pending.size(); ++i) pending[i] = controller.pending(i);
    CHECK(!controller.replace_pairs("1A,1A"));
    std::string changed = table;
    changed[tail * 3] = '3';
    CHECK(controller.pairs() == table && controller.queue().next_slot == before.next_slot);
    for (size_t i = 0; i < pending.size(); ++i)
      CHECK(std::memcmp(&controller.pending(i), &pending[i], sizeof(pending[i])) == 0);
    CHECK(controller.replace_pairs(changed));
    CHECK(state.remote_values.is_on() && state.current_values.is_on());
    CHECK(!controller.endpoint_pending(tail));
    state.loop();
    CHECK(!controller.endpoint_pending(tail));
    const auto transmissions = transport.transmissions.size();
    controller.loop();
    CHECK(transport.transmissions.size() == transmissions);
    // An explicit command after remapping belongs to the new address.
    state.make_call().set_rgb(.5f, .5f, .5f).set_brightness(.5f).set_transition_length(0).perform();
    state.loop();
    CHECK(controller.endpoint_pending(tail));
    CHECK(controller.pending(tail).slot == 2 && controller.pending(tail).state.level == 50);
    CHECK(controller.pending(tail).state.hue == 0 && controller.pending(tail).state.saturation == 0);
    CHECK(controller.replace_pairs(table));
    CHECK(!controller.endpoint_pending(tail));
  }
  // A deferred ON of a disabled position is not redirected when it is enabled.
  CHECK(controller.replace_pairs("1A"));
  auto &newly_enabled = *lights[11];
  newly_enabled.make_call().set_state(true).set_transition_length(0).perform();
  const auto saves_before_enable = preferences.saves;
  Publications enable_publications;
  newly_enabled.add_remote_values_listener(&enable_publications);
  CHECK(controller.replace_pairs(table));
  CHECK(newly_enabled.remote_values.is_on() && newly_enabled.current_values.is_on());
  CHECK(!controller.endpoint_pending(11) && preferences.saves == saves_before_enable);
  CHECK(enable_publications.count == 0);
  newly_enabled.loop();
  CHECK(!controller.endpoint_pending(11));
  // Save and restore actual SDK RGB, CCT, all native selectors and explicit None.
  for (size_t i = 0; i < 11; ++i) {
    auto &state = *lights[i];
    auto call = state.make_call().set_state(true).set_brightness(.25f)
                .set_color_brightness(.5f).set_transition_length(0);
    if (i == 1)
      call.set_color_mode(ColorMode::COLOR_TEMPERATURE).set_color_temperature(1000000.0f / 2700)
      .set_effect(uint32_t{0});
    else
      call.set_color_mode(ColorMode::RGB).set_rgb(0, 1, 0)
      .set_effect(static_cast<uint32_t>(i < 2 ? 0 : i - 1));
    call.perform();
    state.loop();
    CHECK(preferences.sync());
    TestController restored_controller;
    Transport restored_transport;
    initialize(restored_controller, restored_transport, table);
    PixelK80LightOutput restored_output(&restored_controller, i);
    sdk_test::LightState restored(&restored_output);
    configure_light(restored, 0x8300 + i);
    CHECK(restored.remote_values.is_on() && restored.get_current_effect_index() == (i < 2 ? 0 : i - 1));
    CHECK(!restored_controller.endpoint_pending(i));
    const auto before = preferences.saves;
    restored_controller.loop();
    CHECK(preferences.saves == before && restored_transport.transmissions.size() == 1);
    CHECK(restored_controller.pending(i).remaining == 2);
    const auto &wire = restored_transport.transmissions[0];
    CHECK(wire.slot == static_cast<int>(i / 6));
    CHECK(wire.packet.bytes[2] == (i == 1 ? K80_MODE_CCT :
                                   i < 2 ? K80_MODE_HSI : K80_MODE_FLS));
    CHECK(wire.packet.bytes[3] == (i == 0 ? 13 : 25));
    if (i == 1) CHECK(wire.packet.bytes[4] == 1);
    if (i >= 2) CHECK(wire.packet.bytes[8] == i - 1);
  }
  lights[10]->make_call().set_effect("None").set_transition_length(0).perform();
  CHECK(preferences.sync());
  TestController none_controller;
  Transport none_transport;
  initialize(none_controller, none_transport, table);
  PixelK80LightOutput none_output(&none_controller, 10);
  sdk_test::LightState none(&none_output);
  configure_light(none, 0x830a);
  none_controller.loop();
  CHECK(none.remote_values.is_on() && none.get_current_effect_index() == 0);
  CHECK(none_transport.transmissions.size() == 1 &&
        none_transport.transmissions[0].packet.bytes[2] == K80_MODE_HSI);
  TestController disabled_controller;
  Transport disabled_transport;
  initialize(disabled_controller, disabled_transport, "1A");
  PixelK80LightOutput disabled_output(&disabled_controller, 10);
  sdk_test::LightState disabled(&disabled_output);
  configure_light(disabled, 0x830a);
  disabled_controller.loop();
  disabled.loop();
  CHECK(!disabled.remote_values.is_on() && !disabled.current_values.is_on());
  CHECK(!disabled_controller.endpoint_pending(10) && disabled_transport.transmissions.empty());
  return 0;
}

static int pairs_failure_preserves_work() {
  for (const std::string failure : {"invalid", "save", "sync-before", "sync-after", "recovery-sync", "recovery-save"}) {
    ESPPreferences preferences;
    global_preferences = &preferences;
    host::fault_preferences = &preferences;
    sdk_test::set_time_ms(0);
    TestController controller;
    Transport transport;
    initialize(controller, transport, "1A");
    PixelK80LightOutput output(&controller, 0);
    sdk_test::LightState state(&output);
    configure_light(state, 0x8871);
    controller.loop();
    state.make_call().set_state(true).set_rgb(0, 1, 0).set_transition_length(0).perform();
    state.loop();
    PixelK80Pairs pairs(&controller);
    pairs.traits.set_min_length(0);
    pairs.traits.set_max_length(255);
    pairs.setup();
    pairs.make_call().set_value("1A").perform();
    auto target = state.remote_values;
    target.set_red(1);
    target.set_green(0);
    state.begin_transition(target);
    struct Targets : LightTargetStateReachedListener {
      unsigned count{};
      void on_light_target_state_reached() override { ++count; }
    } targets;
    state.add_target_state_reached_listener(&targets);
    const auto pending = controller.pending(0);
    const auto queue = controller.queue();
    const auto current = state.current_values;
    const auto transmissions = transport.transmissions.size();
    std::string observed_status;
    pairs.add_on_state_callback([&](const std::string &) { observed_status = pairs.configuration_status(); });
    if (failure == "save") preferences.save_results = {false, true};
    if (failure == "sync-before") preferences.sync_results = {1, 0};
    if (failure == "sync-after") preferences.sync_results = {2, 0};
    if (failure == "recovery-sync") preferences.sync_results = {2, 1};
    if (failure == "recovery-save") {
      preferences.save_results = {true, false};
      preferences.sync_results = {2, 0};
    }
    pairs.make_call().set_value(failure == "invalid" ? "49A" : "2A").perform();
    CHECK(controller.pairs() == "1A" && pairs.state == "1A");
    CHECK(std::memcmp(&pending, &controller.pending(0), sizeof(pending)) == 0);
    CHECK(queue.started_slots == controller.queue().started_slots && queue.next_slot == controller.queue().next_slot);
    CHECK(state.current_values.get_red() == current.get_red() && state.current_values.get_green() == current.get_green());
    CHECK(state.remote_values.get_red() == 1 && state.remote_values.is_on());
    CHECK(state.has_transformer() && targets.count == 0 && transport.transmissions.size() == transmissions);
    CHECK(observed_status == pairs.configuration_status());
  }
  return 0;
}

static int pairs_publication_reentry() {
  for (bool invalid : {false, true}) {
    ESPPreferences preferences;
    global_preferences = &preferences;
    host::fault_preferences = &preferences;
    TestController controller;
    Transport transport;
    initialize(controller, transport, "1A");
    controller.loop();
    PixelK80Pairs pairs(&controller);
    pairs.traits.set_min_length(0);
    pairs.traits.set_max_length(255);
    pairs.setup();
    pairs.make_call().set_value("1A").perform();
    bool nested{}, coherent{true}, pending_seen{};
    std::vector<std::string> publications;
    pairs.add_on_state_callback([&](const std::string & value) {
      publications.push_back(value);
      coherent &= value == controller.pairs();
      if (nested) return;
      nested = true;
      coherent &= pairs.configuration_status().find(invalid ? "Rejected:" : "Persistence failed:") == 0;
      pairs.make_call().set_value("3A").perform();
      pending_seen = pairs.configuration_status().find("pending") != std::string::npos;
    });
    pairs.add_on_state_callback([&](const std::string & value) {
      coherent &= value == controller.pairs();
      if (publications.size() == 1) coherent &= pairs.configuration_status().find("pending") != std::string::npos;
    });
    if (!invalid) preferences.save_results = {false, true};
    pairs.make_call().set_value(invalid ? "49A" : "2A").perform();
    CHECK(coherent && pending_seen && controller.pairs() == "3A" && pairs.state == "3A");
    CHECK((publications == std::vector<std::string> {"1A", "3A"}));
    CHECK(pairs.configuration_status() == "Saved" && transport.transmissions.empty());
  }
  return 0;
}

static int pairs_transactions() {
  for (const std::string scenario : {"nested", "multiple", "same", "invalid", "recovered", "uncertain", "publication"}) {
    ESPPreferences preferences;
    global_preferences = &preferences;
    host::fault_preferences = &preferences;
    sdk_test::set_time_ms(0);
    TestController controller;
    Transport transport;
    initialize(controller, transport, "1A");
    PixelK80LightOutput output(&controller, 0);
    sdk_test::LightState state(&output);
    configure_light(state, 0x8791);
    controller.loop();
    state.make_call().set_state(true).set_rgb(0, 1, 0).set_transition_length(0).perform();
    state.loop();
    PixelK80Pairs pairs(&controller);
    pairs.traits.set_min_length(0);
    pairs.traits.set_max_length(255);
    pairs.setup();
    const auto key = pairs.get_object_id_hash() ^ 0x59504302;
    auto durable = [&]() {
      const auto &record = preferences.durable.at(key);
      return std::string(reinterpret_cast<const char *>(record.data() + 1));
    };
    bool queued_seen{}, observations_valid{true}, publication_nested{};
    std::vector<std::string> publications;
    auto enqueue = [&]() {
      if (scenario == "recovered") preferences.save_results = {false, true};
      if (scenario == "uncertain") preferences.sync_results = {2, 1};
      std::string temporary = scenario == "same" ? "2A" : scenario == "invalid" ? "49A" : "3A";
      pairs.make_call().set_value(temporary).perform();
      queued_seen = pairs.configuration_status().find("pending") != std::string::npos;
      temporary = "49A"; // A queued request must own the original string.
      if (scenario == "multiple") {
        pairs.make_call().set_value("4A").perform();
        pairs.make_call().set_value("5A").perform();
      }
    };
    pairs.add_on_state_callback([&](const std::string & value) {
      publications.push_back(value);
      observations_valid &= value == controller.pairs();
      if (scenario != "uncertain") observations_valid &= value == durable();
      if (!publication_nested) {
        publication_nested = true;
        enqueue();
      }
      if (publications.size() == 1 && scenario != "publication")
        observations_valid &= pairs.configuration_status().find("pending") != std::string::npos;
    });
    pairs.add_on_state_callback([&](const std::string & value) {
      observations_valid &= value == controller.pairs();
      if (scenario == "publication" && publications.size() == 1)
        observations_valid &= pairs.configuration_status().find("pending") != std::string::npos;
    });
    struct Targets : LightTargetStateReachedListener {
      unsigned count{};
      void on_light_target_state_reached() override { ++count; }
    } targets;
    state.add_target_state_reached_listener(&targets);
    if (scenario != "publication") {
      auto target = state.remote_values;
      target.set_red(1);
      target.set_green(0);
      state.begin_transition(target);
    }
    const auto saves = preferences.saves;
    const auto transmissions = transport.transmissions.size();
    pairs.make_call().set_value("2A").perform();
    CHECK(targets.count == 0);
    std::printf("Pairs %s: runtime=%s published=%s durable=%s status=%s\n", scenario.c_str(),
                controller.pairs().c_str(), pairs.state.c_str(), durable().c_str(), pairs.configuration_status().c_str());
    CHECK(controller.pairs() == pairs.state && queued_seen && observations_valid);
    const std::string expected = scenario == "multiple" ? "5A" :
                                 (scenario == "same" || scenario == "invalid" || scenario == "recovered" || scenario == "uncertain") ? "2A" : "3A";
    CHECK(controller.pairs() == expected);
    CHECK(durable() == (scenario == "uncertain" ? "3A" : expected));
    CHECK(pairs.configuration_status().find("pending") == std::string::npos);
    if (scenario == "invalid") CHECK(pairs.configuration_status().find("Rejected: Position 1") == 0);
    else if (scenario == "recovered" || scenario == "uncertain")
      CHECK(pairs.configuration_status().find("Persistence failed") == 0);
    else CHECK(pairs.configuration_status() == "Saved");
    if (scenario == "multiple") CHECK((publications == std::vector<std::string> {"2A", "3A", "4A", "5A"}));
    if (scenario == "same") CHECK(preferences.saves - saves == 1);
    CHECK(!controller.endpoint_pending(0) && state.remote_values.is_on() && !state.has_transformer());
    state.loop();
    controller.loop();
    CHECK(transport.transmissions.size() == transmissions);
    state.make_call().set_rgb(.5f, .5f, .5f).set_transition_length(0).perform();
    state.loop();
    CHECK(targets.count == 1);
    CHECK(controller.pending(0).state.hue == (scenario == "publication" ? 120 : 0));
  }
  return 0;
}

struct RegistrySeam : ControllerRegistry { static void clear() { controllers.clear(); } };
int cancelled_old_and_new(bool preconsume, bool new_command, bool new_off) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  transport.available = false;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x9600);
  state.make_call().set_state(true).set_rgb(1, 0, 0).set_transition_length(0).perform();
  if (preconsume) state.loop();
  CHECK(controller.replace_pairs("2A"));
  const auto saves = preferences.saves;
  CHECK(state.current_values.is_on() && state.remote_values.is_on() && !controller.endpoint_pending(0));
  if (new_command) state.make_call().set_state(!new_off).set_rgb(0, 0, 1).set_transition_length(0).perform();
  const auto before_ready_saves = preferences.saves;
  controller.loop();
  CHECK(transport.transmissions.empty());
  transport.available = true;
  controller.loop();
  CHECK(transport.transmissions.size() == (new_command ? 1U : 0U));
  CHECK(preferences.saves == before_ready_saves);
  if (new_command) {
    CHECK(transport.transmissions[0].slot == 1);
    CHECK(controller.pending(0).state.level == (new_off ? 0 : 100));
    if (!new_off) CHECK(controller.pending(0).state.hue == 240);
  } else CHECK(preferences.saves == saves);
  std::printf("startup old-consumed=%d new=%d OFF=%d tx=%zu\n", preconsume, new_command, new_off,
              transport.transmissions.size());
  return 0;
}

int restore_and_unchanged(unsigned effect) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController seed;
  Transport seed_transport;
  initialize(seed, seed_transport, "1A,1B");
  PixelK80LightOutput seed_output(&seed, 1);
  sdk_test::LightState seed_state(&seed_output);
  configure_light(seed_state, 0x9611);
  seed_state.make_call().set_state(true).set_rgb(0, 1, 0).set_effect(effect).set_transition_length(0).perform();
  CHECK(preferences.sync());
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A,1B");
  PixelK80LightOutput output(&controller, 0), unchanged_output(&controller, 1);
  sdk_test::LightState state(&output), unchanged(&unchanged_output);
  configure_light(state, 0x9610);
  configure_light(unchanged, 0x9611);
  state.make_call().set_state(true).set_rgb(1, 0, 0).set_transition_length(0).perform();
  CHECK(controller.replace_pairs("2A,1B"));
  const auto saves = preferences.saves;
  controller.loop();
  CHECK(transport.transmissions.size() == 1 && transport.transmissions[0].slot == 0);
  CHECK(!controller.endpoint_pending(0) && controller.pending(1).remaining == 2);
  CHECK(unchanged.remote_values.is_on() && unchanged.effect_index() == effect && preferences.saves == saves);
  std::printf("startup unchanged restored effect=%u replayed once; changed old-position silent\n", effect);
  return 0;
}

int new_effect_phase() {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x9620);
  state.make_call().set_state(true).set_rgb(1, 0, 0).set_effect(10).set_transition_length(0).perform();
  CHECK(controller.replace_pairs("2A"));
  const auto saves = preferences.saves;
  sdk_test::set_time_ms(1000);
  controller.loop();
  std::printf("startup phase tx=%zu remaining=%u hue=%d\n",
              transport.transmissions.size(), controller.pending(0).remaining, controller.pending(0).state.hue);
  CHECK(state.effect_index() == 10 && state.remote_values.is_on());
  CHECK(transport.transmissions.size() == 1 && transport.transmissions[0].slot == 1);
  CHECK(controller.pending(0).remaining == 0 && controller.pending(0).state.hue == 1);
  CHECK(preferences.saves == saves);
  std::puts("startup new Rainbow phase: new-address, one attempt, no save");
  return 0;
}

int later_public_command(unsigned kind) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x9630 + kind);
  state.make_call().set_state(true).set_rgb(1, 0, 0).set_transition_length(0).perform();
  CHECK(controller.replace_pairs("2A"));
  auto call = state.make_call().set_rgb(0, 0, 1).set_save(false);
  if (kind == 0) call.set_transition_length(0).set_publish(false);
  if (kind == 1) call.set_transition_length(1000);
  if (kind == 2) call.set_flash_length(1000);
  call.perform();
  const auto saves = preferences.saves;
  controller.loop();
  CHECK(transport.transmissions.size() == 1 && transport.transmissions[0].slot == 1);
  CHECK(preferences.saves == saves);
  if (kind == 0) CHECK(controller.pending(0).state.hue == 240 && state.remote_values.get_red() == 1);
  else CHECK(state.has_transformer() && state.remote_values.get_blue() == 1);
  std::printf("startup later-public kind=%u admitted at new address without replay\n", kind);
  return 0;
}


int identical_after_remap(bool roundtrip) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x9660 + roundtrip);
  state.make_call().set_state(true).set_rgb(1, 0, 0).set_transition_length(0).perform();
  CHECK(controller.replace_pairs("2A"));
  if (roundtrip) CHECK(controller.replace_pairs("1A"));
  state.make_call().set_state(true).set_rgb(1, 0, 0).set_transition_length(0).perform();
  const auto saves = preferences.saves;
  controller.loop();
  CHECK(transport.transmissions.size() == 1 && transport.transmissions[0].slot == (roundtrip ? 0 : 1));
  CHECK(controller.pending(0).state.hue == 0 && controller.pending(0).remaining == 2);
  CHECK(preferences.saves == saves);
  std::printf("identical new command roundtrip=%d admitted once\n", roundtrip);
  return 0;
}
int off_policy(bool explicit_off, bool publish, bool consumed) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x9670);
  if (explicit_off) state.make_call().set_state(false).set_transition_length(0).set_publish(publish).perform();
  if (consumed) state.loop();
  const auto saves = preferences.saves;
  controller.loop();
  CHECK(transport.transmissions.size() == (explicit_off ? 1U : 0U));
  CHECK(preferences.saves == saves);
  if (explicit_off) CHECK(controller.pending(0).state.level == 0 && controller.pending(0).remaining == 2);
  std::printf("OFF initial-only=%d explicit-publish=%d consumed=%d tx=%zu\n",
              !explicit_off, publish, consumed, transport.transmissions.size());
  return 0;
}
static sdk_test::LightState *setup_state;
static ::TestController *setup_controller;
static bool setup_remap, setup_pending;
static void initial_state_callback(LightStateRTCState &) {
  setup_state->make_call().set_state(false).set_transition_length(0).set_save(false).perform();
  if (setup_remap) {
    if (!setup_controller->replace_pairs("2A")) std::abort();
  } else {
    setup_state->loop();
    setup_pending = setup_controller->endpoint_pending(0);
  }
}
int initial_callback_origin(bool remap) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  setup_state = &state;
  setup_controller = &controller;
  setup_remap = remap;
  setup_pending = false;
  state.set_initial_state(initial_state_callback);
  configure_light(state, 0x9680);
  controller.loop();
  if (remap) CHECK(transport.transmissions.empty() && controller.pairs() == "2A");
  else CHECK(setup_pending && transport.transmissions.size() == 1);
  std::printf("typed setup-callback remap=%d pending=%d tx=%zu\n", remap, setup_pending, transport.transmissions.size());
  return 0;
}
int setup_listener_replacement(unsigned kind) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  bool armed = true;
  auto replace = [&] {
    if (!armed) return;
    armed = false;
    state.make_call().set_state(false).set_transition_length(0).set_publish(false).set_save(false).perform();
  };
  struct Target : LightTargetStateReachedListener {
    std::function<void()> callback;
    void on_light_target_state_reached() override { callback(); }
  } target;
  struct Remote : LightRemoteValuesListener {
    std::function<void()> callback;
    void on_light_remote_values_update() override { callback(); }
  } remote;
  struct Registry : esphome::Controller {
    std::function<void()> callback;
    void on_light_update(esphome::light::LightState *) override { callback(); }
  } registry;
  target.callback = replace;
  remote.callback = replace;
  registry.callback = replace;
  if (kind == 0) state.add_target_state_reached_listener(&target);
  if (kind == 1) state.add_remote_values_listener(&remote);
  if (kind == 2) ControllerRegistry::register_controller(&registry);
  configure_light(state, 0x9690 + kind);
  RegistrySeam::clear();
  const auto saves = preferences.saves;
  controller.loop();
  CHECK(!armed && transport.transmissions.size() == 1);
  CHECK(controller.pending(0).state.level == 0 && controller.pending(0).remaining == 2 && preferences.saves == saves);
  std::printf("setup native listener kind=%u explicit unpublished OFF replaces restore origin\n", kind);
  return 0;
}
int preregistration_restore() {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController seed;
  Transport seed_transport;
  initialize(seed, seed_transport, "1A");
  PixelK80LightOutput seed_output(&seed, 0);
  sdk_test::LightState seed_state(&seed_output);
  configure_light(seed_state, 0x96a0);
  seed_state.make_call().set_state(true).set_rgb(0, 1, 0).set_transition_length(0).perform();
  CHECK(preferences.sync());
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  CHECK(controller.replace_pairs("2A"));
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x96a0);
  const auto saves = preferences.saves;
  controller.loop();
  CHECK(transport.transmissions.size() == 1 && transport.transmissions[0].slot == 1);
  CHECK(controller.pending(0).state.hue == 120 && controller.pending(0).remaining == 2 && preferences.saves == saves);
  std::puts("pre-registration remap: later restored ON belongs to installed address");
  return 0;
}
int pairs_fifo_startup() {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x96b0);
  PixelK80Pairs pairs(&controller);
  pairs.traits.set_min_length(0);
  pairs.traits.set_max_length(255);
  pairs.setup();
  state.make_call().set_state(true).set_rgb(1, 0, 0).set_transition_length(1000).perform();
  struct Target : LightTargetStateReachedListener {
    unsigned count{};
    void on_light_target_state_reached() override { ++count; }
  } targets;
  state.add_target_state_reached_listener(&targets);
  std::vector<std::string> publications;
  bool pending = false, coherent = true;
  pairs.add_on_state_callback([&](const std::string & value) {
    publications.push_back(value);
    coherent &= value == controller.pairs();
    if (value == "2A") {
      state.make_call().set_rgb(1, 0, 0).set_transition_length(0).set_publish(false).set_save(false).perform();
      std::string nested = "3A";
      pairs.make_call().set_value(nested).perform();
      nested = "49A";
      pending = pairs.configuration_status().find("pending") != std::string::npos;
    } else {
      state.make_call().set_rgb(0, 0, 1).set_transition_length(0).set_publish(false).set_save(false).perform();
    }
  });
  pairs.make_call().set_value("2A").perform();
  CHECK((publications == std::vector<std::string> {"2A", "3A"}));
  CHECK(pending && coherent && controller.pairs() == "3A" && pairs.state == "3A" &&
        pairs.configuration_status() == "Saved");
  const auto key = pairs.get_object_id_hash() ^ 0x59504302;
  const auto &record = preferences.durable.at(key);
  CHECK(std::string(reinterpret_cast<const char *>(record.data() + 1)) == "3A");
  CHECK(targets.count == 2 && !state.has_transformer()); // Two real callback-created instant calls only.
  const auto saves = preferences.saves;
  controller.loop();
  CHECK(transport.transmissions.size() == 1 && transport.transmissions[0].slot == 2);
  CHECK(controller.pending(0).state.hue == 240 && controller.pending(0).remaining == 2 && preferences.saves == saves);
  std::puts("typed PairFIFO: owned nested edit, durable/runtime/publication3A; only later blue delivered");
  return 0;
}
class RegistryCount : public esphome::Controller {
 public:
  unsigned count{};
  void on_light_update(esphome::light::LightState *) override { ++count; }
} registry;
class TargetCount : public LightTargetStateReachedListener {
 public:
  unsigned count{};
  void on_light_target_state_reached() override { ++count; }
};

int cancel_public_work(bool flash, bool publish, bool expired) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A,1B");
  PixelK80LightOutput output(&controller, 0), output2(&controller, 1);
  sdk_test::LightState state(&output), unchanged(&output2);
  configure_light(state, 0x9100);
  configure_light(unchanged, 0x9101);
  controller.loop();
  state.make_call().set_state(true).set_rgb(0, 1, 0).set_transition_length(0).perform();
  state.loop();
  controller.abort(0);
  unchanged.make_call().set_state(true).set_rgb(0, 0, 1).set_transition_length(1000).perform();
  state.set_flash_transition_length(100);
  auto call = state.make_call().set_rgb(1, 0, 0).set_publish(publish).set_save(false);
  if (flash) call.set_flash_length(1000);
  else call.set_transition_length(1000);
  call.perform(); // Actual public nonzero LightCall constructors, not begin_transition.
  CHECK(state.has_transformer() && unchanged.has_transformer());
  TargetCount targets;
  Publications publications;
  state.add_target_state_reached_listener(&targets);
  state.add_remote_values_listener(&publications);
  const auto before_registry = registry.count;
  const auto before_saves = preferences.saves;
  sdk_test::set_time_ms(expired ? 1200 : 500);
  CHECK(!controller.replace_pairs("2A,2A"));
  CHECK(state.has_transformer() && unchanged.has_transformer());
  CHECK(controller.replace_pairs("1A,1B"));
  CHECK(state.has_transformer() && unchanged.has_transformer());
  CHECK(controller.replace_pairs("2A,1B"));
  std::printf("boundary target=%u remote=%u registry=%u currentRGB=%.0f,%.0f,%.0f\n",
              targets.count, publications.count, registry.count - before_registry,
              state.current_values.get_red(), state.current_values.get_green(), state.current_values.get_blue());
  CHECK(!state.has_transformer() && unchanged.has_transformer());
  CHECK(!controller.endpoint_pending(0) && targets.count == 0 && publications.count == 0);
  CHECK(registry.count == before_registry && preferences.saves == before_saves);
  CHECK(state.current_values.get_red() == 1 && state.current_values.get_green() == 0);
  CHECK(state.remote_values.get_red() == (publish ? 1 : 0));
  state.loop();
  CHECK(!controller.endpoint_pending(0) && targets.count == 0 && publications.count == 0);
  // Gray preserves the red target adopted by cancellation, including publish=false.
  state.make_call().set_rgb(.5f, .5f, .5f).set_transition_length(0).perform();
  state.loop();
  CHECK(targets.count == 1 && publications.count == 1 && registry.count == before_registry + 1);
  CHECK(controller.pending(0).slot == 1 && controller.pending(0).state.hue == 0);
  controller.abort(0);
  // Ordinary post-map public flash retains native remote-before-target completion.
  state.make_call().set_rgb(0, 0, 1).set_flash_length(1000).set_save(false).perform();
  const auto start_targets = targets.count;
  const auto start_publications = publications.count;
  sdk_test::set_time_ms(sdk_test::clock_ms + 1200);
  state.loop();
  sdk_test::set_time_ms(sdk_test::clock_ms + 100);
  state.loop();
  CHECK(!state.has_transformer() && targets.count == start_targets + 1);
  CHECK(publications.count == start_publications + 1);
  std::printf("cancel flash=%d publish=%d expired=%d: no manufactured events/RF; new flash completes normally\n",
              flash, publish, expired);
  return 0;
}

int cancel_effect_work(unsigned effect) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x9200 + effect);
  controller.loop();
  state.make_call().set_state(true).set_rgb(1, 0, 0).set_effect(effect).set_transition_length(0).perform();
  const auto saves = preferences.saves;
  const auto registry_before = registry.count;
  CHECK(controller.replace_pairs("2A"));
  CHECK(state.effect_index() == effect && state.remote_values.is_on() && state.current_values.is_on());
  CHECK(!controller.endpoint_pending(0));
  CHECK(preferences.saves == saves && registry.count == registry_before);
  sdk_test::set_time_ms(1000);
  state.loop();
  CHECK(state.effect_index() == effect && preferences.saves == saves && registry.count == registry_before);
  CHECK(controller.pending(0).remaining == (effect == 10 ? 1 : 0));
  std::printf("active effect=%u retained; next phase attempts=%u\n", effect, controller.pending(0).remaining);
  return 0;
}

int ordinary_postmap_reentry(bool transition) {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  ::TestController controller;
  Transport transport;
  initialize(controller, transport, "1A");
  PixelK80LightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x9300);
  controller.loop();
  state.make_call().set_state(true).set_rgb(0, 1, 0).set_transition_length(1000).perform();
  CHECK(controller.replace_pairs("2A"));
  std::vector<char> events;
  struct First : LightTargetStateReachedListener {
    esphome::light::LightState *state{};
    std::vector<char> *events{};
    bool armed{true}, transition{};
    void on_light_target_state_reached() override {
      events->push_back('A');
      if (!armed) return;
      armed = false;
      state->make_call().set_rgb(0, 0, 1).set_transition_length(transition ? 1000 : 0).perform();
    }
  } first;
  struct Second : LightTargetStateReachedListener {
    std::vector<char> *events{};
    void on_light_target_state_reached() override { events->push_back('B'); }
  } second;
  first.state = &state;
  first.events = &events;
  first.transition = transition;
  second.events = &events;
  // Ordinary registration after cancellation is retained, not overwritten by replay.
  state.add_target_state_reached_listener(&first);
  state.add_target_state_reached_listener(&second);
  state.make_call().set_rgb(1, 0, 0).set_transition_length(0).perform();
  CHECK(events == (transition ? std::vector<char> {'A', 'B'} : std::vector<char> {'A', 'A', 'B', 'B'}));
  state.loop();
  if (transition) {
    sdk_test::set_time_ms(1000);
    state.loop();
    CHECK(events == (std::vector<char> {'A', 'B', 'A', 'B'}));
  }
  CHECK(controller.pending(0).slot == 1 && controller.pending(0).state.hue == 240);
  std::printf("normal post-map reentry transition=%d: native listener order retained; blue new-slot1\n", transition);
  return 0;
}
static int pairs_queue_resources() {
  using namespace esphome;
  using namespace esphome::pixel_k80;
  for (const size_t count : {size_t{32}, size_t{257}}) {
    ESPPreferences preferences;
    global_preferences = &preferences;
    host::fault_preferences = &preferences;
    PixelK80Controller parent;
    parent.setup();
    PixelK80Pairs pairs(&parent);
    pairs.traits.set_min_length(0);
    pairs.traits.set_max_length(255);
    pairs.setup();
    pairs.make_call().set_value("1A").perform();
    size_t seen = 0, peak_pending = 0, peak_slots = 0;
    pairs.add_on_state_callback([&](const std::string &) {
      ++seen;
      if (seen < count) pairs.make_call().set_value("1A").perform();
      peak_pending = std::max(peak_pending, pairs.pending_edits_.size() - pairs.next_edit_);
      peak_slots = std::max(peak_slots, pairs.pending_edits_.size());
    });
    pairs.make_call().set_value("1A").perform();
    const size_t retained_bytes = pairs.pending_edits_.capacity() * sizeof(std::string);
    std::printf("edits=%zu peak_pending=%zu peak_slots=%zu idle_size=%zu idle_capacity=%zu retained_bytes=%zu status=%s\n",
                seen, peak_pending, peak_slots, pairs.pending_edits_.size(), pairs.pending_edits_.capacity(),
                retained_bytes, pairs.configuration_status().c_str());
    if (seen != count || peak_pending != 1 || peak_slots != 1 || pairs.pending_edits_.capacity() != 0 ||
        !pairs.pending_edits_.empty() ||
        pairs.configuration_status() != "Saved" || parent.pairs() != "1A") return 1;
  }
  {
    ESPPreferences preferences;
    global_preferences = &preferences;
    host::fault_preferences = &preferences;
    PixelK80Controller parent;
    parent.setup();
    PixelK80Pairs pairs(&parent);
    pairs.traits.set_min_length(0);
    pairs.traits.set_max_length(255);
    pairs.setup();
    pairs.make_call().set_value("1A").perform();
    size_t issued = 1, seen = 0, peak_pending = 0, peak_slots = 0;
    pairs.add_on_state_callback([&](const std::string &) {
      ++seen;
      const size_t burst = seen == 1 ? 4 : 1;
      for (size_t i = 0; i < burst && issued < 257; ++i, ++issued) pairs.make_call().set_value("1A").perform();
      peak_pending = std::max(peak_pending, pairs.pending_edits_.size() - pairs.next_edit_);
      peak_slots = std::max(peak_slots, pairs.pending_edits_.size());
    });
    pairs.make_call().set_value("1A").perform();
    std::printf("backlogged edits=%zu peak_pending=%zu peak_slots=%zu idle_capacity=%zu status=%s\n",
                seen, peak_pending, peak_slots, pairs.pending_edits_.capacity(), pairs.configuration_status().c_str());
    if (seen != 257 || peak_pending != 4 || peak_slots > 2 * peak_pending || pairs.pending_edits_.capacity() != 0 ||
        pairs.configuration_status() != "Saved" || parent.pairs() != "1A") return 1;
  }

  return 0;
}
int main() {
  CHECK(pairs_queue_resources() == 0);
  CHECK(new_effect_phase() == 0);
  for (bool consumed : {false, true}) {
    CHECK(cancelled_old_and_new(consumed, false, false) == 0);
    CHECK(cancelled_old_and_new(consumed, true, false) == 0);
    CHECK(cancelled_old_and_new(consumed, true, true) == 0);
    CHECK(off_policy(false, true, consumed) == 0);
    for (bool publish : {false, true}) CHECK(off_policy(true, publish, consumed) == 0);
  }
  for (unsigned effect : {0U, 1U, 10U}) CHECK(restore_and_unchanged(effect) == 0);
  for (unsigned kind : {0U, 1U, 2U}) CHECK(later_public_command(kind) == 0);
  for (bool roundtrip : {false, true}) CHECK(identical_after_remap(roundtrip) == 0);
  CHECK(initial_callback_origin(false) == 0);
  CHECK(initial_callback_origin(true) == 0);
  for (unsigned kind : {0U, 1U, 2U}) CHECK(setup_listener_replacement(kind) == 0);
  CHECK(preregistration_restore() == 0);
  CHECK(pairs_fifo_startup() == 0);
  ControllerRegistry::register_controller(&registry);
  for (bool flash : {false, true}) for (bool publish : {false, true}) for (bool expired : {false, true})
        CHECK(cancel_public_work(flash, publish, expired) == 0);
  CHECK(cancel_effect_work(1) == 0);
  CHECK(cancel_effect_work(10) == 0);
  CHECK(ordinary_postmap_reentry(false) == 0);
  CHECK(ordinary_postmap_reentry(true) == 0);
  RegistrySeam::clear();
  CHECK(pairs_failure_preserves_work() == 0);
  CHECK(pairs_publication_reentry() == 0);
  CHECK(pairs_transactions() == 0);
  CHECK(semantic_and_effects() == 0);
  CHECK(queue_and_transport() == 0);
  CHECK(rainbow_startup() == 0);
  CHECK(rainbow_color_modes() == 0);
  CHECK(rainbow_configuration() == 0);
  CHECK(rainbow_and_restore() == 0);
  CHECK(registry_and_static_restore() == 0);
  std::puts("production controller + installed SDK: semantics, queue, rainbow and restore passed");
}
