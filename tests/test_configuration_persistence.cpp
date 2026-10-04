#include "yiscaxia_pairs.h"
#include "yiscaxia_number.h"
#include "sdk_runtime.h"
#include "test_check.h"
#include <algorithm>

using namespace esphome;
using namespace esphome::yiscaxia;

namespace {
struct Registration { EntityBase *entity; const char *name; uint32_t hash; Device *device; };
std::vector<Registration> registrations;
constexpr uint32_t PAIRS_HASH = 0x12345678;
constexpr uint32_t NUMBER_HASH = 0x76543210;
constexpr uint32_t PAIRS_SALT = 0x59504302;
constexpr uint32_t SPACING_SALT = 0x59504303;
constexpr uint32_t REPEATS_SALT = 0x59504304;
constexpr uint32_t UNRELATED = 0xaabbccdd;
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
void initialize(YiscaxiaNumber &field, bool spacing, Device *device = nullptr) {
  bind(field, false, device);
  field.traits.set_min_value(1);
  field.traits.set_max_value(spacing ? 65535 : 255);
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
std::vector<uint8_t> number_record(uint16_t value) {
  return {1, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
}
void failure(ESPPreferences &prefs, const std::string &scenario) {
  if (scenario == "save-false") prefs.save_results = {false, true};
  else if (scenario == "sync-before") prefs.sync_results = {1, 0};
  else if (scenario == "sync-after") prefs.sync_results = {2, 0};
  else if (scenario == "recovery-failed") prefs.sync_results = {2, 1};
  else if (scenario == "recovery-save-failed") {
    prefs.save_results = {true, false};
    prefs.sync_results = {2, 0};
  }
}
bool uncertain_durable(const std::string &scenario) {
  return scenario == "recovery-failed" || scenario == "recovery-save-failed";
}
int isolation() {
  ESPPreferences prefs;
  attach(prefs);
  Device first_device, second_device;
  first_device.set_device_id(0x11111111);
  second_device.set_device_id(0x22222222);
  YiscaxiaController first, second;
  first.setup(); second.setup();
  YiscaxiaPairs a(&first), b(&second);
  initialize(a, &first_device); initialize(b, &second_device);
  CHECK(a.get_object_id_hash() == b.get_object_id_hash());
  CHECK(prefs.requests[0].first != prefs.requests[1].first);
  CHECK(prefs.requests[0].first == (PAIRS_HASH ^ first_device.get_device_id() ^ PAIRS_SALT));
  CHECK(prefs.requests[1].first == (PAIRS_HASH ^ second_device.get_device_id() ^ PAIRS_SALT));
  command(a, "2A,-,2C"); command(b, "3A,3B");
  YiscaxiaNumber ar(&first, false), br(&second, false), as(&first, true), bs(&second, true);
  initialize(ar, false, &first_device); initialize(br, false, &second_device);
  initialize(as, true, &first_device); initialize(bs, true, &second_device);
  CHECK(prefs.requests[2].first == (NUMBER_HASH ^ first_device.get_device_id() ^ REPEATS_SALT));
  CHECK(prefs.requests[3].first == (NUMBER_HASH ^ second_device.get_device_id() ^ REPEATS_SALT));
  CHECK(prefs.requests[4].first == (NUMBER_HASH ^ first_device.get_device_id() ^ SPACING_SALT));
  CHECK(prefs.requests[5].first == (NUMBER_HASH ^ second_device.get_device_id() ^ SPACING_SALT));
  command(ar, 7); command(br, 9); command(as, 1234); command(bs, 2345);
  CHECK(first.pairs() == "2A,-,2C" && second.pairs() == "3A,3B");
  CHECK(first.transmission_setting(false) == 7 && second.transmission_setting(false) == 9);
  CHECK(first.transmission_setting(true) == 1234 && second.transmission_setting(true) == 2345);
  const auto saves = prefs.saves;
  prefs.reboot();
  YiscaxiaController restored_first, restored_second;
  restored_first.setup(); restored_second.setup();
  YiscaxiaPairs ra(&restored_first), rb(&restored_second);
  initialize(ra, &first_device); initialize(rb, &second_device);
  YiscaxiaNumber rar(&restored_first, false), rbr(&restored_second, false);
  YiscaxiaNumber ras(&restored_first, true), rbs(&restored_second, true);
  initialize(rar, false, &first_device); initialize(rbr, false, &second_device);
  initialize(ras, true, &first_device); initialize(rbs, true, &second_device);
  CHECK(ra.state == "2A,-,2C" && rb.state == "3A,3B");
  CHECK(rar.state == 7 && rbr.state == 9 && ras.state == 1234 && rbs.state == 2345);
  CHECK(ra.configuration_status() == "Saved" && rb.configuration_status() == "Saved");
  CHECK(prefs.saves == saves);
  CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t>{42,99}));
  return 0;
}
int pairs_case(const std::string &scenario) {
  ESPPreferences prefs;
  attach(prefs);
  const uint32_t key = PAIRS_HASH ^ PAIRS_SALT;
  YiscaxiaController parent; parent.setup();
  YiscaxiaPairs field(&parent); initialize(field);
  CHECK((prefs.requests == std::vector<std::pair<uint32_t,size_t>>{{key,257}}));
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
      if (mutation == 2) std::fill(record.begin()+1, record.end(), '9');
      if (mutation == 3) record.back() = 'X';
      if (mutation == 4) record = pair_record("1A,1A");
      if (mutation == 5) record = pair_record("49A");
      if (mutation == 6) record = pair_record("1A,1B,1C,1D,1E,1F,2A,2B,2C,2D,2E,2F,3A");
      if (mutation == 7) record.clear();
      prefs.durable[key] = record; prefs.reboot();
      const auto saves = prefs.saves;
      YiscaxiaController restored; restored.setup();
      YiscaxiaPairs rebooted(&restored); initialize(rebooted);
      CHECK(rebooted.state == original && restored.pairs() == original);
      CHECK(prefs.durable[key] == record);
      CHECK(prefs.saves == saves);
      CHECK(mutation == 1 || mutation == 7 ? rebooted.configuration_status() == "Ready" : rebooted.configuration_status().find("invalid") != std::string::npos);
    }
    CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t>{42,99}));
    return 0;
  } else if (scenario != "happy") {
    failure(prefs, scenario);
    command(field, "3A");
    CHECK(field.state == accepted && parent.pairs() == accepted);
    CHECK(field.configuration_status().find("uncertain") != std::string::npos);
    CHECK(uncertain_durable(scenario) ? prefs.durable[key] == pair_record("3A") : prefs.durable[key] == old_record);
    // A failed rollback can leave the new durable table while runtime keeps the old one.
    prefs.reboot();
    YiscaxiaController after_failure; after_failure.setup();
    YiscaxiaPairs observed(&after_failure); initialize(observed);
    CHECK(observed.state == (uncertain_durable(scenario) ? "3A" : accepted));
    CHECK(observed.configuration_status() == "Saved");
    const auto saves = prefs.saves;
    command(field, accepted);
    CHECK(prefs.saves > saves && field.configuration_status() == "Saved");
    CHECK(prefs.durable[key] == old_record);
  } else {
    const auto saves = prefs.saves, syncs = prefs.syncs;
    command(field, accepted);
    CHECK(prefs.saves == saves && prefs.syncs == syncs);
  }
  const auto final_value = field.state;
  const auto saves = prefs.saves;
  prefs.reboot();
  YiscaxiaController restored; restored.setup();
  YiscaxiaPairs rebooted(&restored); initialize(rebooted);
  CHECK(rebooted.state == final_value && restored.pairs() == final_value);
  CHECK(rebooted.configuration_status() == "Saved");
  CHECK(prefs.saves == saves);
  for (const auto &saved : prefs.saved_sizes) CHECK((saved == std::pair<uint32_t,size_t>{key,257}));
  CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t>{42,99}));
  return 0;
}
int number_case(bool spacing, const std::string &scenario) {
  ESPPreferences prefs; attach(prefs);
  const uint32_t key = NUMBER_HASH ^ (spacing ? SPACING_SALT : REPEATS_SALT);
  YiscaxiaController parent; parent.setup();
  YiscaxiaNumber field(&parent, spacing); initialize(field, spacing);
  const uint16_t original = spacing ? 150 : 3;
  const uint16_t candidate = spacing ? 65535 : 255;
  CHECK(field.state == original && field.configuration_status() == "Ready");
  CHECK((prefs.requests == std::vector<std::pair<uint32_t,size_t>>{{key,3}}));
  CHECK(prefs.loads == prefs.requests);
  command(field, original);
  CHECK(prefs.durable[key] == number_record(original));
  if (scenario == "invalid") {
    const auto saves = prefs.saves;
    for (float value : {0.f,-1.f,1.5f,NAN,INFINITY,-INFINITY, spacing ? 65536.f : 256.f}) {
      command(field, value);
      CHECK(field.state == original && parent.transmission_setting(spacing) == original);
      CHECK(prefs.saves == saves && prefs.durable[key] == number_record(original));
    }
  } else if (scenario == "corrupt") {
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
      auto record = number_record(original);
      if (mutation == 0) record[0] = 2;
      if (mutation == 1) record.pop_back();
      if (mutation == 2) record[1] = record[2] = 0;
      if (mutation == 3) { if (spacing) record[0] = 255; else { record[1] = 0; record[2] = 1; } }
      if (mutation == 4) record.clear();
      prefs.durable[key] = record; prefs.reboot();
      const auto saves = prefs.saves;
      YiscaxiaController restored; restored.setup();
      YiscaxiaNumber rebooted(&restored, spacing); initialize(rebooted, spacing);
      CHECK(rebooted.state == original && restored.transmission_setting(spacing) == original);
      CHECK(prefs.durable[key] == record);
      CHECK(prefs.saves == saves);
      CHECK(mutation == 1 || mutation == 4 ? rebooted.configuration_status() == "Ready" : rebooted.configuration_status().find("invalid") != std::string::npos);
    }
    CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t>{42,99}));
    return 0;
  } else {
    failure(prefs, scenario);
    command(field, candidate);
    if (scenario == "happy") {
      CHECK(field.state == candidate && parent.transmission_setting(spacing) == candidate);
      CHECK(prefs.durable[key] == number_record(candidate));
      const auto saves = prefs.saves, syncs = prefs.syncs;
      command(field, candidate);
      CHECK(prefs.saves == saves && prefs.syncs == syncs);
    } else {
      CHECK(field.state == original && parent.transmission_setting(spacing) == original);
      CHECK(field.configuration_status().find("uncertain") != std::string::npos);
      CHECK(prefs.durable[key] == number_record(uncertain_durable(scenario) ? candidate : original));
      prefs.reboot();
      YiscaxiaController after_failure; after_failure.setup();
      YiscaxiaNumber observed(&after_failure, spacing); initialize(observed, spacing);
      CHECK(observed.state == (uncertain_durable(scenario) ? candidate : original));
      CHECK(observed.configuration_status() == "Saved");
      const auto saves = prefs.saves;
      command(field, original);
      CHECK(prefs.saves > saves && field.configuration_status() == "Saved");
    }
  }
  const auto accepted = field.state;
  const auto saves = prefs.saves;
  prefs.reboot();
  YiscaxiaController restored; restored.setup();
  YiscaxiaNumber rebooted(&restored, spacing); initialize(rebooted, spacing);
  CHECK(rebooted.state == accepted && restored.transmission_setting(spacing) == accepted);
  CHECK(rebooted.configuration_status() == "Saved");
  CHECK(prefs.saves == saves);
  for (const auto &saved : prefs.saved_sizes) CHECK((saved == std::pair<uint32_t,size_t>{key,3}));
  CHECK((prefs.durable[UNRELATED] == std::vector<uint8_t>{42,99}));
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
    if (field == "repeats" || field == "spacing") return number_case(field == "spacing", argv[2]);
    CHECK(false);
  }
  CHECK(isolation() == 0);
  for (const std::string scenario : {"happy", "canonical", "invalid", "corrupt", "save-false", "sync-before", "sync-after", "recovery-failed", "recovery-save-failed"})
    CHECK(pairs_case(scenario) == 0);
  for (bool spacing : {false, true})
    for (const std::string scenario : {"happy", "invalid", "corrupt", "save-false", "sync-before", "sync-after", "recovery-failed", "recovery-save-failed"})
      CHECK(number_case(spacing, scenario) == 0);
  return 0;
}
