#include "yiscaxia_pairs.h"
#include "yiscaxia_number.h"
#include "sdk_runtime.h"
#include "test_check.h"
#include <algorithm>

using namespace esphome;
using namespace esphome::yiscaxia;

namespace {
class Controller : public YiscaxiaController {
 public:
  void arm() { queue_.armed = 1; }
  const k80_controller_pending &pending(size_t endpoint) const { return pending_[endpoint]; }
};
struct Registration { EntityBase *entity; const char *name; uint32_t hash; Device *device; };
std::vector<Registration> registrations;
constexpr uint32_t PAIRS_HASH = 0x12345678;
constexpr uint32_t NUMBER_HASH = 0x76543210;
constexpr uint32_t PAIRS_SALT = 0x59504302;
constexpr uint32_t SPACING_SALT = 0x59504303;
constexpr uint32_t ATTEMPTS_SALT = 0x59504304;
constexpr uint32_t RAINBOW_DEGREES_SALT = 0x59504305;
constexpr uint32_t RAINBOW_SPACING_SALT = 0x59504306;
constexpr uint32_t UNRELATED = 0xaabbccdd;
struct NumberCase {
  YiscaxiaSetting setting;
  uint32_t salt, default_value, maximum;
  size_t record_size;
  const char *argument;
};
const NumberCase NUMBER_CASES[] = {
  {YiscaxiaSetting::TRANSMISSION_ATTEMPTS, ATTEMPTS_SALT, 3, 255, 3, "attempts"},
  {YiscaxiaSetting::CHANNEL_SPACING, SPACING_SALT, 150, 65535, 3, "spacing"},
  {YiscaxiaSetting::RAINBOW_STEP_DEGREES, RAINBOW_DEGREES_SALT, 1, 359, 3, "rainbow-step"},
  {YiscaxiaSetting::RAINBOW_STEP_SPACING, RAINBOW_SPACING_SALT, 1000, 16777215, 5, "rainbow-spacing"},
};
void bind(EntityBase &entity, bool pairs, Device *device = nullptr) {
  registrations = {{&entity, pairs ? "Pairs" : "Transmission", pairs ? PAIRS_HASH : NUMBER_HASH, device}};
  original_setup();
  registrations.clear();
}
void initialize(YiscaxiaPairs &field, Device *device = nullptr) {
  bind(field, true, device);
  field.traits.set_min_length(0);
  field.traits.set_max_length(255);
  field.setup();
}
void initialize(YiscaxiaNumber &field, YiscaxiaSetting setting, Device *device = nullptr) {
  bind(field, false, device);
  field.traits.set_min_value(1);
  field.traits.set_max_value(configuration_setting_max(setting));
  field.traits.set_step(1);
  field.setup();
}
void command(YiscaxiaPairs &field, const std::string &value) { field.make_call().set_value(value).perform(); }
void command(YiscaxiaNumber &field, float value) { field.make_call().set_value(value).perform(); }
void attach(ESPPreferences &prefs) {
  global_preferences = &prefs;
  host::fault_preferences = &prefs;
  prefs.durable[UNRELATED] = {42, 99};
}
std::vector<uint8_t> pair_record(const std::string &value) {
  std::vector<uint8_t> record(257, 0);
  record[0] = 2;
  std::copy(value.begin(), value.end(), record.begin() + 1);
  return record;
}
std::vector<uint8_t> number_record(uint32_t value, size_t size = 3) {
  std::vector<uint8_t> record = {1, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  if (size == 5) {
    record.push_back(static_cast<uint8_t>(value >> 16));
    record.push_back(static_cast<uint8_t>(value >> 24));
  }
  return record;
}
void failure(ESPPreferences &prefs, const std::string &scenario) {
  if (scenario == "save-false") prefs.save_results = {false, true};
  else if (scenario == "sync-before") prefs.sync_results = {1, 0};
  else if (scenario == "sync-after") prefs.sync_results = {2, 0};
  else if (scenario == "recovery-failed") prefs.sync_results = {2, 1};
  else if (scenario == "recovery-save-failed") {
    prefs.save_results = {true, false};
    prefs.sync_results = {2, 0};
  } else if (scenario == "recovery-both-failed") {
    prefs.save_results = {true, false};
    prefs.sync_results = {2, 1};
  }
}
bool uncertain_durable(const std::string &scenario) {
  return scenario == "recovery-failed" || scenario == "recovery-save-failed" || scenario == "recovery-both-failed";
}
int isolation() {
  ESPPreferences prefs;
  attach(prefs);
  Device first_device, second_device;
  first_device.set_device_id(0x11111111);
  second_device.set_device_id(0x22222222);
  YiscaxiaController first, second;
  first.setup();
  second.setup();
  YiscaxiaPairs a(&first), b(&second);
  initialize(a, &first_device);
  initialize(b, &second_device);
  CHECK(a.get_object_id_hash() == b.get_object_id_hash());
  CHECK(prefs.requests[0].first != prefs.requests[1].first);
  CHECK(prefs.requests[0].first == (PAIRS_HASH ^ first_device.get_device_id() ^ PAIRS_SALT));
  CHECK(prefs.requests[1].first == (PAIRS_HASH ^ second_device.get_device_id() ^ PAIRS_SALT));
  command(a, "2A,-,2C");
  command(b, "3A,3B");
  YiscaxiaNumber ar(&first, YiscaxiaSetting::TRANSMISSION_ATTEMPTS), br(&second, YiscaxiaSetting::TRANSMISSION_ATTEMPTS);
  YiscaxiaNumber as(&first, YiscaxiaSetting::CHANNEL_SPACING), bs(&second, YiscaxiaSetting::CHANNEL_SPACING);
  initialize(ar, YiscaxiaSetting::TRANSMISSION_ATTEMPTS, &first_device);
  initialize(br, YiscaxiaSetting::TRANSMISSION_ATTEMPTS, &second_device);
  initialize(as, YiscaxiaSetting::CHANNEL_SPACING, &first_device);
  initialize(bs, YiscaxiaSetting::CHANNEL_SPACING, &second_device);
  CHECK(prefs.requests[2].first == (NUMBER_HASH ^ first_device.get_device_id() ^ ATTEMPTS_SALT));
  CHECK(prefs.requests[3].first == (NUMBER_HASH ^ second_device.get_device_id() ^ ATTEMPTS_SALT));
  CHECK(prefs.requests[4].first == (NUMBER_HASH ^ first_device.get_device_id() ^ SPACING_SALT));
  CHECK(prefs.requests[5].first == (NUMBER_HASH ^ second_device.get_device_id() ^ SPACING_SALT));
  command(ar, 7);
  command(br, 9);
  command(as, 1234);
  command(bs, 2345);
  CHECK(first.pairs() == "2A,-,2C" && second.pairs() == "3A,3B");
  CHECK(first.configuration_setting(YiscaxiaSetting::TRANSMISSION_ATTEMPTS) == 7 &&
        second.configuration_setting(YiscaxiaSetting::TRANSMISSION_ATTEMPTS) == 9);
  CHECK(first.configuration_setting(YiscaxiaSetting::CHANNEL_SPACING) == 1234 &&
        second.configuration_setting(YiscaxiaSetting::CHANNEL_SPACING) == 2345);
  for (const auto &spec : NUMBER_CASES) {
    if (spec.setting != YiscaxiaSetting::RAINBOW_STEP_DEGREES &&
        spec.setting != YiscaxiaSetting::RAINBOW_STEP_SPACING) continue;
    const uint32_t first_value = spec.record_size == 5 ? 234567 : 31;
    const uint32_t second_value = spec.record_size == 5 ? 345678 : 57;
    const auto request = prefs.requests.size();
    YiscaxiaNumber first_field(&first, spec.setting), second_field(&second, spec.setting);
    initialize(first_field, spec.setting, &first_device);
    initialize(second_field, spec.setting, &second_device);
    const uint32_t first_key = NUMBER_HASH ^ first_device.get_device_id() ^ spec.salt;
    const uint32_t second_key = NUMBER_HASH ^ second_device.get_device_id() ^ spec.salt;
    CHECK((prefs.requests[request] == std::pair<uint32_t, size_t> {first_key, spec.record_size}));
    CHECK((prefs.requests[request + 1] == std::pair<uint32_t, size_t> {second_key, spec.record_size}));
    command(first_field, first_value);
    command(second_field, second_value);
    CHECK(first.configuration_setting(spec.setting) == first_value &&
          second.configuration_setting(spec.setting) == second_value);
    CHECK(prefs.durable[first_key] == number_record(first_value, spec.record_size));
    CHECK(prefs.durable[second_key] == number_record(second_value, spec.record_size));
  }
  const auto saves = prefs.saves;
  prefs.reboot();
  YiscaxiaController restored_first, restored_second;
  restored_first.setup();
  restored_second.setup();
  YiscaxiaPairs ra(&restored_first), rb(&restored_second);
  initialize(ra, &first_device);
  initialize(rb, &second_device);
  YiscaxiaNumber rar(&restored_first, YiscaxiaSetting::TRANSMISSION_ATTEMPTS), rbr(&restored_second,
      YiscaxiaSetting::TRANSMISSION_ATTEMPTS);
  YiscaxiaNumber ras(&restored_first, YiscaxiaSetting::CHANNEL_SPACING), rbs(&restored_second,
      YiscaxiaSetting::CHANNEL_SPACING);
  initialize(rar, YiscaxiaSetting::TRANSMISSION_ATTEMPTS, &first_device);
  initialize(rbr, YiscaxiaSetting::TRANSMISSION_ATTEMPTS, &second_device);
  initialize(ras, YiscaxiaSetting::CHANNEL_SPACING, &first_device);
  initialize(rbs, YiscaxiaSetting::CHANNEL_SPACING, &second_device);
  CHECK(ra.state == "2A,-,2C" && rb.state == "3A,3B");
  CHECK(rar.state == 7 && rbr.state == 9 && ras.state == 1234 && rbs.state == 2345);
  for (const auto &spec : NUMBER_CASES) {
    if (spec.setting != YiscaxiaSetting::RAINBOW_STEP_DEGREES &&
        spec.setting != YiscaxiaSetting::RAINBOW_STEP_SPACING) continue;
    YiscaxiaNumber first_field(&restored_first, spec.setting), second_field(&restored_second, spec.setting);
    initialize(first_field, spec.setting, &first_device);
    initialize(second_field, spec.setting, &second_device);
    CHECK(first_field.state == (spec.record_size == 5 ? 234567 : 31));
    CHECK(second_field.state == (spec.record_size == 5 ? 345678 : 57));
    CHECK(first_field.configuration_status() == "Saved" && second_field.configuration_status() == "Saved");
  }
  CHECK(ra.configuration_status() == "Saved" && rb.configuration_status() == "Saved");
  CHECK(prefs.saves == saves);
  const auto requests = prefs.requests.size();
  YiscaxiaNumber unknown(&first, static_cast<YiscaxiaSetting>(99));
  unknown.setup();
  CHECK(unknown.is_failed() && unknown.configuration_status() == "Unknown configuration setting");
  CHECK(prefs.requests.size() == requests && prefs.saves == saves);
  CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t> {42, 99}));
  return 0;
}
int pairs_case(const std::string &scenario) {
  ESPPreferences prefs;
  attach(prefs);
  const uint32_t key = PAIRS_HASH ^ PAIRS_SALT;
  Controller parent;
  parent.setup();
  YiscaxiaPairs field(&parent);
  initialize(field);
  CHECK((prefs.requests == std::vector<std::pair<uint32_t, size_t>> {{key, 257}}));
  CHECK(prefs.loads == prefs.requests && field.configuration_status() == "Ready");
  const auto original = field.state;
  const std::string accepted = "2A,-,2C";
  command(field, accepted + ",-,-");
  CHECK(field.state == accepted && parent.pairs() == accepted && field.configuration_status() == "Saved");
  CHECK(prefs.durable[key] == pair_record(accepted) && prefs.pending.empty());
  const auto old_record = prefs.durable[key];
  if (scenario == "invalid") {
    const auto saves = prefs.saves;
    for (const std::string value : {"49A", "0A", "1G", "1A,1A", "bad", "1A,1B,1C,1D,1E,1F,2A,2B,2C,2D,2E,2F,3A"}) {
      command(field, value);
      CHECK(field.state == accepted && parent.pairs() == accepted);
      CHECK(prefs.durable[key] == old_record && prefs.saves == saves);
    }
    for (const auto &[value, error] : std::vector<std::pair<std::string, std::string>> {
    {"49A", "Position 1: channel must be a canonical decimal in 1..48"},
    {"1G", "Position 1: group must be uppercase A..F"},
    {"1A,1A", "Position 2: duplicate address already used by position 1"}
  }) {
      command(field, value);
      CHECK(field.configuration_status() == "Rejected: " + error);
    }
  } else if (scenario == "canonical") {
    for (const std::string value : {"", "-,-", "1A,-", "1A,-,2B,-"}) {
      command(field, value);
      const std::string canonical = value == "-,-" ? "" : value == "1A,-" ? "1A" : value == "1A,-,2B,-" ? "1A,-,2B" : "";
      CHECK(field.state == canonical && parent.pairs() == canonical);
      CHECK(prefs.durable[key] == pair_record(canonical));
      const auto saves = prefs.saves, syncs = prefs.syncs;
      command(field, value);
      CHECK(prefs.saves == saves && prefs.syncs == syncs);
    }
  } else if (scenario == "corrupt") {
    for (unsigned mutation = 0; mutation < 8; ++mutation) {
      auto record = pair_record(accepted);
      if (mutation == 0) record[0] = 9;
      if (mutation == 1) record.pop_back();
      if (mutation == 2) std::fill(record.begin() + 1, record.end(), '9');
      if (mutation == 3) record.back() = 'X';
      if (mutation == 4) record = pair_record("1A,1A");
      if (mutation == 5) record = pair_record("49A");
      if (mutation == 6) record = pair_record("1A,1B,1C,1D,1E,1F,2A,2B,2C,2D,2E,2F,3A");
      if (mutation == 7) record.clear();
      prefs.durable[key] = record;
      prefs.reboot();
      const auto saves = prefs.saves;
      YiscaxiaController restored;
      restored.setup();
      YiscaxiaPairs rebooted(&restored);
      initialize(rebooted);
      CHECK(rebooted.state == original && restored.pairs() == original);
      CHECK(prefs.durable[key] == record);
      CHECK(prefs.saves == saves);
      CHECK(mutation == 1 ||
            mutation == 7 ? rebooted.configuration_status() == "Ready" : rebooted.configuration_status().find("invalid") !=
            std::string::npos);
      const std::string errors[] = {
        "unsupported record version", "", "missing NUL terminator", "nonzero bytes after NUL terminator",
        "Position 2: duplicate address already used by position 1",
        "Position 1: channel must be a canonical decimal in 1..48",
        "Pair table exceeds capacity of 12 positions", ""
      };
      if (!errors[mutation].empty())
        CHECK(rebooted.configuration_status() == "Saved table invalid: " + errors[mutation] + "; using configured defaults");
    }
    CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t> {42, 99}));
    return 0;
  } else if (scenario != "happy") {
    parent.arm();
    const auto on = k80_control_values{K80_MODE_HSI, 50, 1, 120, 100, 1};
    CHECK(parent.request_state(0, on, false));
    const auto pending = parent.pending(0);
    failure(prefs, scenario);
    command(field, "3A");
    CHECK(field.state == accepted && parent.pairs() == accepted);
    CHECK(std::memcmp(&pending, &parent.pending(0), sizeof(pending)) == 0);
    const std::string expected_status = std::string("Persistence failed: ") +
                                        (scenario == "save-false" ? "save" : "sync") + " failed; " +
                                        (scenario == "recovery-save-failed" ?
                                         "recovery save failed; runtime table unchanged; durable table uncertain; retry" :
                                         scenario == "recovery-failed" ?
                                         "recovery sync failed; runtime table unchanged; durable table uncertain; retry" :
                                         "previous runtime and durable table retained; retry");
    CHECK(field.configuration_status() == expected_status);
    CHECK(uncertain_durable(scenario) ? prefs.durable[key] == pair_record("3A") : prefs.durable[key] == old_record);
    // A failed rollback can leave the new durable table while runtime keeps the old one.
    prefs.reboot();
    YiscaxiaController after_failure;
    after_failure.setup();
    YiscaxiaPairs observed(&after_failure);
    initialize(observed);
    CHECK(observed.state == (uncertain_durable(scenario) ? "3A" : accepted));
    CHECK(observed.configuration_status() == "Saved");
    const auto saves = prefs.saves;
    command(field, accepted);
    CHECK((uncertain_durable(scenario) ? prefs.saves > saves : prefs.saves == saves) &&
          field.configuration_status() == "Saved");
    CHECK(prefs.durable[key] == old_record);
  } else {
    const auto saves = prefs.saves, syncs = prefs.syncs;
    command(field, accepted);
    CHECK(prefs.saves == saves && prefs.syncs == syncs);
  }
  const auto final_value = field.state;
  const auto saves = prefs.saves;
  prefs.reboot();
  YiscaxiaController restored;
  restored.setup();
  YiscaxiaPairs rebooted(&restored);
  initialize(rebooted);
  CHECK(rebooted.state == final_value && restored.pairs() == final_value);
  CHECK(rebooted.configuration_status() == "Saved");
  CHECK(prefs.saves == saves);
  for (const auto &saved : prefs.saved_sizes) CHECK((saved == std::pair<uint32_t, size_t> {key, 257}));
  CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t> {42, 99}));
  return 0;
}
int number_case(YiscaxiaSetting setting, const std::string &scenario) {
  const auto *spec = std::find_if(std::begin(NUMBER_CASES), std::end(NUMBER_CASES),
  [setting](const NumberCase & entry) { return entry.setting == setting; });
  CHECK(spec != std::end(NUMBER_CASES));
  CHECK(configuration_setting_max(setting) == spec->maximum);
  ESPPreferences prefs;
  attach(prefs);
  const uint32_t key = NUMBER_HASH ^ spec->salt;
  YiscaxiaController parent;
  parent.setup();
  YiscaxiaNumber field(&parent, setting);
  initialize(field, setting);
  const uint32_t original = spec->default_value;
  const uint32_t candidate = spec->maximum;
  CHECK(field.state == original && field.configuration_status() == "Ready");
  CHECK((prefs.requests == std::vector<std::pair<uint32_t, size_t>> {{key, spec->record_size}}));
  CHECK(prefs.loads == prefs.requests);
  command(field, original);
  CHECK(prefs.durable[key] == number_record(original, spec->record_size));
  if (scenario == "invalid") {
    const auto saves = prefs.saves;
    for (float value : {0.f, -1.f, 1.5f, NAN, INFINITY, -INFINITY, static_cast<float>(candidate + 1)}) {
      const auto previous_status = field.configuration_status();
      command(field, value);
      CHECK(field.state == original && parent.configuration_setting(setting) == original);
      CHECK(prefs.saves == saves && prefs.durable[key] == number_record(original, spec->record_size));
      // NumberCall rejects nonfinite/out-of-trait-range inputs before control().
      CHECK(field.configuration_status() == (value == 1.5f ? "Rejected: value must be a whole number" : previous_status));
    }
  } else if (scenario == "corrupt") {
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
      auto record = number_record(original, spec->record_size);
      if (mutation == 0) record[0] = 2;
      if (mutation == 1) record.pop_back();
      if (mutation == 2) record[1] = record[2] = 0;
      if (mutation == 3) record = number_record(candidate + 1, spec->record_size);
      if (mutation == 4) record.clear();
      prefs.durable[key] = record;
      prefs.reboot();
      const auto saves = prefs.saves;
      YiscaxiaController restored;
      restored.setup();
      YiscaxiaNumber rebooted(&restored, setting);
      initialize(rebooted, setting);
      CHECK(rebooted.state == original && restored.configuration_setting(setting) == original);
      CHECK(prefs.durable[key] == record);
      CHECK(prefs.saves == saves);
      CHECK(mutation == 1 ||
            mutation == 4 ? rebooted.configuration_status() == "Ready" : rebooted.configuration_status().find("invalid") !=
            std::string::npos);
      if (mutation == 0) {
        CHECK(rebooted.configuration_status() == "Saved setting invalid: unsupported record version; using configured default");
      } else if (mutation == 2 || mutation == 3) {
        CHECK(rebooted.configuration_status() == "Saved setting invalid: value must be in 1.." +
              std::to_string(candidate) + "; using configured default");
      }
    }
    CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t> {42, 99}));
    return 0;
  } else {
    failure(prefs, scenario);
    command(field, candidate);
    if (scenario == "happy") {
      CHECK(field.state == candidate && parent.configuration_setting(setting) == candidate);
      CHECK(prefs.durable[key] == number_record(candidate, spec->record_size));
      const auto saves = prefs.saves, syncs = prefs.syncs;
      command(field, candidate);
      CHECK(prefs.saves == saves && prefs.syncs == syncs);
    } else {
      CHECK(field.state == original && parent.configuration_setting(setting) == original);
      const std::string initial = std::string("Persistence failed: ") +
                                  (scenario == "save-false" ? "save" : "sync") + " failed; ";
      const std::string expected_status = initial + (scenario == "recovery-both-failed" ?
                                          "recovery save failed; recovery sync failed; previous runtime value retained, durable setting uncertain" :
                                          scenario == "recovery-save-failed" ?
                                          "recovery save failed; previous runtime value retained, durable setting uncertain" :
                                          scenario == "recovery-failed" ?
                                          "recovery sync failed; previous runtime value retained, durable setting uncertain" :
                                          "previous runtime and durable setting retained; retry new value");
      CHECK(field.configuration_status() == expected_status);
      CHECK(prefs.durable[key] == number_record(uncertain_durable(scenario) ? candidate : original, spec->record_size));
      prefs.reboot();
      YiscaxiaController after_failure;
      after_failure.setup();
      YiscaxiaNumber observed(&after_failure, setting);
      initialize(observed, setting);
      CHECK(observed.state == (uncertain_durable(scenario) ? candidate : original));
      CHECK(observed.configuration_status() == "Saved");
      const auto saves = prefs.saves;
      command(field, original);
      CHECK((uncertain_durable(scenario) ? prefs.saves > saves : prefs.saves == saves) &&
            field.configuration_status() == "Saved");
    }
  }
  const auto accepted = field.state;
  const auto saves = prefs.saves;
  prefs.reboot();
  YiscaxiaController restored;
  restored.setup();
  YiscaxiaNumber rebooted(&restored, setting);
  initialize(rebooted, setting);
  CHECK(rebooted.state == accepted && restored.configuration_setting(setting) == accepted);
  CHECK(rebooted.configuration_status() == "Saved");
  CHECK(prefs.saves == saves);
  for (const auto &saved : prefs.saved_sizes) CHECK((saved == std::pair<uint32_t, size_t> {key, spec->record_size}));
  CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t> {42, 99}));
  return 0;
}
}

// This is the SDK's codegen/testing friend, so the production wrappers remain final.
void original_setup() {
  for (const auto &registration : registrations) {
    registration.entity->configure_entity_(registration.name, registration.hash, 0);
    registration.entity->set_device_(registration.device);
  }
}

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "isolation") return isolation();
  if (argc == 3) {
    const std::string field(argv[1]);
    if (field == "pairs") return pairs_case(argv[2]);
    for (const auto &spec : NUMBER_CASES)
      if (field == spec.argument) return number_case(spec.setting, argv[2]);
    CHECK(false);
  }
  CHECK(isolation() == 0);
  for (const std::string scenario : {"happy", "canonical", "invalid", "corrupt", "save-false", "sync-before", "sync-after", "recovery-failed", "recovery-save-failed"})
    CHECK(pairs_case(scenario) == 0);
  for (const auto &spec : NUMBER_CASES)
    for (const std::string scenario : {"happy", "invalid", "corrupt", "save-false", "sync-before", "sync-after", "recovery-failed", "recovery-save-failed", "recovery-both-failed"})
      CHECK(number_case(spec.setting, scenario) == 0);
  return 0;
}
