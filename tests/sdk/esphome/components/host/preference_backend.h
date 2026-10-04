#pragma once
#include <cstddef>
#include <cstdint>
namespace esphome::host {
class HostPreferenceBackend {
 public:
  explicit HostPreferenceBackend(uint32_t key) : key_(key) {}
  bool save(const uint8_t *, size_t) const;
  bool load(uint8_t *, size_t) const;
  uint32_t key_;
};
}
namespace esphome { using PreferenceBackend = host::HostPreferenceBackend; }
