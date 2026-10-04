#pragma once

// Substitute storage only. Calls, entity identity and light restore use the SDK.
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <vector>
#include "esphome/core/preference_backend.h"

namespace esphome::host {
class HostPreferences : public PreferencesMixin<HostPreferences> {
 public:
  using PreferencesMixin<HostPreferences>::make_preference;
  std::map<uint32_t, HostPreferenceBackend> backends;
  std::map<uint32_t, size_t> lengths;
  std::map<uint32_t, std::vector<uint8_t>> durable, pending;
  std::vector<std::pair<uint32_t, size_t>> requests, loads, saved_sizes;
  std::vector<uint32_t> load_keys, save_keys;
  std::deque<bool> save_results;
  // 0 succeeds; 1 fails before committing; 2 commits then reports failure.
  std::deque<int> sync_results;
  unsigned saves{}, syncs{};
  ESPPreferenceObject make_preference(size_t length, uint32_t key, bool = false) {
    lengths[key] = length;
    requests.emplace_back(key, length);
    return ESPPreferenceObject(&backends.try_emplace(key, key).first->second);
  }
  std::vector<uint8_t> &record(uint32_t key, bool queued) {
    return queued ? pending[key] : durable[key];
  }
  bool sync() {
    ++syncs;
    const int result = sync_results.empty() ? 0 : sync_results.front();
    if (!sync_results.empty()) sync_results.pop_front();
    if (result != 1)
      for (const auto &[key, value] : pending)
        if (!value.empty()) durable[key] = value;
    pending.clear();
    return result == 0;
  }
  void reboot() { pending.clear(); }
  bool reset() { pending.clear(); durable.clear(); return true; }
};
inline HostPreferences *fault_preferences;
inline bool HostPreferenceBackend::save(const uint8_t *data, size_t size) const {
  auto *owner = fault_preferences;
  owner->save_keys.push_back(key_);
  owner->saved_sizes.emplace_back(key_, size);
  if (owner->lengths.at(key_) != size) return false;
  ++owner->saves;
  const bool result = owner->save_results.empty() || owner->save_results.front();
  if (!owner->save_results.empty()) owner->save_results.pop_front();
  if (result) owner->pending[key_].assign(data, data + size);
  return result;
}
inline bool HostPreferenceBackend::load(uint8_t *value, size_t size) const {
  auto *owner = fault_preferences;
  owner->load_keys.push_back(key_);
  owner->loads.emplace_back(key_, size);
  if (owner->lengths.at(key_) != size) return false;
  const auto &queued = owner->record(key_, true);
  const auto &record = queued.empty() ? owner->record(key_, false) : queued;
  if (record.size() != size) return false;
  std::memcpy(value, record.data(), size);
  return true;
}
}
DECLARE_PREFERENCE_ALIASES(esphome::host::HostPreferences)
