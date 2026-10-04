#include "sdk_runtime.h"
#include "test_check.h"
#include "yiscaxia_light.h"
#include "esphome/core/preferences.h"

#include <array>
#include <cstring>
#include <memory>
#include <vector>

using namespace esphome;
using namespace esphome::light;
using namespace esphome::yiscaxia;

class Controller : public YiscaxiaController {
 public:
  const k80_controller_pending &pending(size_t i) const { return pending_[i]; }
  const k80_controller_queue &queue() const { return queue_; }
  void abort(size_t i) { k80_controller_abort(&queue_, i); }
};

// Only the hardware boundary is substituted. Controller, output, LightCall,
// deferred LightState ordering and preference restore execute production code.
class Transport : public YiscaxiaTransport {
 public:
  struct Transmission { yiscaxia_tx_packet packet; int slot; };
  bool available{true};
  uint64_t now{};
  yiscaxia_tx_result result{};
  std::vector<Transmission> transmissions;
  Transport() { result.restored = 1; result.trigger_attempted = 1; }
  bool ready() const override { return available; }
  uint64_t now_us() const override { return now; }
  yiscaxia_tx_result transmit(const yiscaxia_tx_packet &packet, int slot) override {
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

static void initialize(Controller &controller, Transport &transport,
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
  Controller controller;
  Transport transport;
  initialize(controller, transport);
  YiscaxiaLightOutput output(&controller, 0), disabled_output(&controller, 11);
  sdk_test::LightState state(&output), disabled(&disabled_output);
  configure_light(state, 0x8100);
  configure_light(disabled, 0x810b);
  CHECK(!state.remote_values.is_on());
  CHECK(state.get_effects().size() == 10);
  const char *names[] = {"SOS", "Lightning 1", "Lightning 2", "TV Screen", "Police",
      "Ambulance", "Fire Engine", "RGB Circle 1", "RGB Circle 2", "Slow Rainbow"};
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
  CHECK(controller.pending(0).state.mode == K80_CONTROLLER_MODE_HSI);
  CHECK(controller.pending(0).state.hue == 120 && controller.pending(0).state.saturation == 100);
  CHECK(controller.pending(0).state.level == 13 && controller.pending(0).remaining == 3);
  for (uint32_t i = 1; i <= 9; ++i) {
    controller.abort(0);
    state.make_call().set_effect(i).set_transition_length(0).perform();
    state.loop();
    CHECK(controller.pending(0).state.mode == K80_CONTROLLER_MODE_FLS);
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
  CHECK(controller.pending(0).state.mode == K80_CONTROLLER_MODE_HSI);
  CHECK(controller.pending(0).state.saturation == 0 && controller.pending(0).state.level == 13);
  state.make_call().set_color_mode(ColorMode::COLOR_TEMPERATURE)
      .set_color_temperature(1000000.0f / 2700).set_transition_length(0).perform();
  state.loop();
  CHECK(controller.pending(0).state.mode == K80_CONTROLLER_MODE_CCT);
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
  CHECK(!controller.replace_pairs("2A")); // Deferred OFF is still a write to the old address.
  state.loop();
  CHECK(controller.pending(0).state.level == 0 && controller.pending(0).remaining == 3);
  controller.loop();
  CHECK(transport.transmissions.back().packet.bytes[2] == K80_CONTROLLER_MODE_CCT);
  CHECK(transport.transmissions.back().packet.bytes[3] == 0);
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
  Controller controller;
  Transport transport;
  initialize(controller, transport, "1A,1B,2A");
  controller.loop();
  k80_controller_state first{K80_CONTROLLER_MODE_HSI, 30, 1, 120, 100, 1};
  CHECK(controller.request_state(0, first, false));
  controller.loop();
  CHECK(transport.transmissions.size() == 1 && controller.pending(0).remaining == 2);
  CHECK(controller.queue().started_slots == 1 && controller.queue().slot_started_us[0] == 0);
  CHECK(controller.request_state(0, first, false));
  CHECK(controller.pending(0).remaining == 2); // Identical request preserves spent budget.
  CHECK(controller.set_transmission_setting(false, 7));
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
  controller.abort(0); controller.abort(1); controller.abort(2);
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
  Controller other;
  Transport other_transport;
  initialize(other, other_transport);
  other.loop();
  CHECK(other.transmission_setting(false) == 3 && other.transmission_setting(true) == 150);
  CHECK(other.request_state(0, first, false));
  other.loop();
  CHECK(other_transport.transmissions.size() == 1 && other.queue().slot_started_us[0] == 0);
  CHECK(controller.transmission_setting(false) == 7 && !controller.queue().armed);
  return 0;
}

static int rainbow_and_restore() {
  ESPPreferences preferences;
  global_preferences = &preferences;
  host::fault_preferences = &preferences;
  sdk_test::set_time_ms(0);
  Controller controller;
  Transport transport;
  initialize(controller, transport);
  YiscaxiaLightOutput output(&controller, 0);
  sdk_test::LightState state(&output);
  configure_light(state, 0x8200);
  controller.loop();
  Publications publications;
  state.add_remote_values_listener(&publications);
  state.make_call().set_state(true).set_rgb(0, 1, 0).set_brightness(.25f)
      .set_color_brightness(.5f).set_effect("Slow Rainbow").set_transition_length(0).perform();
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
  CHECK(controller.set_transmission_setting(false, 255));
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
  auto *rainbow = static_cast<YiscaxiaSlowRainbowEffect *>(state.get_effects()[9]);
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
  Controller reboot;
  Transport reboot_transport;
  reboot_transport.available = false;
  initialize(reboot, reboot_transport);
  CHECK(reboot.set_transmission_setting(false, 7));
  YiscaxiaLightOutput reboot_output(&reboot, 0);
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
  CHECK(reboot_transport.transmissions[0].packet.bytes[2] == K80_CONTROLLER_MODE_HSI);
  restored.make_call().set_state(false).set_transition_length(0).perform();
  restored.loop();
  CHECK(restored.get_current_effect_index() == 0 && reboot.pending(0).remaining == 7);
  CHECK(preferences.sync());
  Controller off_reboot;
  Transport off_transport;
  initialize(off_reboot, off_transport);
  YiscaxiaLightOutput off_output(&off_reboot, 0);
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
  Controller controller;
  Transport transport;
  initialize(controller, transport, table);
  std::array<std::unique_ptr<YiscaxiaLightOutput>, 12> outputs;
  std::array<std::unique_ptr<sdk_test::LightState>, 12> lights;
  for (size_t i = 0; i < lights.size(); ++i) {
    outputs[i] = std::make_unique<YiscaxiaLightOutput>(&controller, i);
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
    const auto before = controller.queue();
    std::array<k80_controller_pending, 12> pending;
    for (size_t i = 0; i < pending.size(); ++i) pending[i] = controller.pending(i);
    CHECK(!controller.replace_pairs(""));
    CHECK(!controller.replace_pairs("1A"));
    std::string changed = table;
    changed[tail * 3] = '3';
    CHECK(!controller.replace_pairs(changed));
    CHECK(controller.pairs() == table && controller.queue().next_endpoint == before.next_endpoint);
    for (size_t i = 0; i < pending.size(); ++i)
      CHECK(std::memcmp(&controller.pending(i), &pending[i], sizeof(pending[i])) == 0);
    state.loop();
    state.make_call().set_state(false).set_transition_length(0).perform();
    CHECK(!controller.replace_pairs(changed));
    state.loop();
    CHECK(!controller.replace_pairs(changed));
    controller.abort(tail);
    CHECK(controller.replace_pairs(changed));
    CHECK(controller.replace_pairs(table));
  }
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
    Controller restored_controller;
    Transport restored_transport;
    initialize(restored_controller, restored_transport, table);
    YiscaxiaLightOutput restored_output(&restored_controller, i);
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
    CHECK(wire.packet.bytes[2] == (i == 1 ? K80_CONTROLLER_MODE_CCT :
        i < 2 ? K80_CONTROLLER_MODE_HSI : K80_CONTROLLER_MODE_FLS));
    CHECK(wire.packet.bytes[3] == (i == 0 ? 13 : 25));
    if (i == 1) CHECK(wire.packet.bytes[4] == 1);
    if (i >= 2) CHECK(wire.packet.bytes[8] == i - 1);
  }
  lights[10]->make_call().set_effect("None").set_transition_length(0).perform();
  CHECK(preferences.sync());
  Controller none_controller;
  Transport none_transport;
  initialize(none_controller, none_transport, table);
  YiscaxiaLightOutput none_output(&none_controller, 10);
  sdk_test::LightState none(&none_output);
  configure_light(none, 0x830a);
  none_controller.loop();
  CHECK(none.remote_values.is_on() && none.get_current_effect_index() == 0);
  CHECK(none_transport.transmissions.size() == 1 &&
      none_transport.transmissions[0].packet.bytes[2] == K80_CONTROLLER_MODE_HSI);
  Controller disabled_controller;
  Transport disabled_transport;
  initialize(disabled_controller, disabled_transport, "1A");
  YiscaxiaLightOutput disabled_output(&disabled_controller, 10);
  sdk_test::LightState disabled(&disabled_output);
  configure_light(disabled, 0x830a);
  disabled_controller.loop();
  disabled.loop();
  CHECK(!disabled.remote_values.is_on() && !disabled.current_values.is_on());
  CHECK(!disabled_controller.endpoint_pending(10) && disabled_transport.transmissions.empty());
  return 0;
}

int main() {
  CHECK(semantic_and_effects() == 0);
  CHECK(queue_and_transport() == 0);
  CHECK(rainbow_and_restore() == 0);
  CHECK(registry_and_static_restore() == 0);
  std::puts("production controller + installed SDK: semantics, queue, rainbow and restore passed");
}
