#pragma once

#include "k80_controller_scheduler.h"
#include <string>
#include <functional>

namespace k80_controller_config {

constexpr int decimal(const char *text, int maximum) {
  if (maximum < 0 || !text || !*text || (*text == '0' && text[1])) return -1;
  int value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return -1;
    const int digit = *text - '0';
    if (digit > maximum || value > (maximum - digit) / 10) return -1;
    value = value * 10 + digit;
  }
  return value;
}

constexpr bool equal(const char *left, const char *right) {
  while (*left && *left == *right) { ++left; ++right; }
  return *left == *right;
}

constexpr int profile(const char *text) {
  if (!text) return K80_CONTROLLER_SEMANTIC_PROFILE_NONE;
  return equal(text, "brightness") ? K80_CONTROLLER_SEMANTIC_PROFILE_BRIGHTNESS :
      equal(text, "cct_2700") ? K80_CONTROLLER_SEMANTIC_PROFILE_CCT_2700 :
      equal(text, "native") ? K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE :
      equal(text, "native_measured_frequency") ? K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_MEASURED_FREQUENCY :
      equal(text, "native_raw_frequency") ? K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_RAW_FREQUENCY :
      K80_CONTROLLER_SEMANTIC_PROFILE_NONE;
}

constexpr k80_controller_endpoint endpoint(const char *slot, const char *group,
    const char *semantic_profile) {
  return {decimal(slot, K80_CONTROLLER_RF_SLOT_COUNT - 1),
      decimal(group, K80_CONTROLLER_GROUP_COUNT - 1), profile(semantic_profile)};
}

template <size_t Count>
constexpr bool valid(const k80_controller_endpoint (&endpoints)[Count]) {
  if (Count == 0 || Count > K80_CONTROLLER_ENDPOINT_LIMIT) return false;
  for (size_t i = 0; i < Count; ++i) {
    const auto &item = endpoints[i];
    if (!k80_controller_profile_address_valid(item.semantic_profile,
            item.rf_slot, item.rf_group)) return false;
    for (size_t j = 0; j < i; ++j)
      if (item.rf_slot == endpoints[j].rf_slot && item.rf_group == endpoints[j].rf_group)
        return false;
  }
  return true;
}

// A complete table permits swapping addresses without a transient duplicate.
inline bool replace_addresses(k80_controller_queue *queue, const std::string &text,
    std::function<bool(size_t)> light_idle = nullptr) {
  if (!queue || !queue->pending || queue->count == 0 ||
      queue->count > K80_CONTROLLER_ENDPOINT_LIMIT ||
      text.find('\0') != std::string::npos) return false;
  k80_controller_endpoint proposed[K80_CONTROLLER_ENDPOINT_LIMIT];
  size_t cursor = 0;
  for (size_t i = 0; i < queue->count; ++i) {
    const size_t colon = text.find(':', cursor);
    const size_t comma = text.find(',', cursor);
    const size_t end = comma == std::string::npos ? text.size() : comma;
    if ((i + 1 == queue->count) != (comma == std::string::npos)) return false;
    if (text.substr(cursor, end - cursor) == "-") {
      proposed[i] = {-1, -1, queue->pending[i].semantic_profile};
      cursor = end + 1;
      continue;
    }
    if (colon == std::string::npos || colon >= end) return false;
    const auto slot = text.substr(cursor, colon - cursor);
    const auto group = text.substr(colon + 1, end - colon - 1);
    proposed[i] = endpoint(slot.c_str(), group.c_str(), "brightness");
    proposed[i].semantic_profile = queue->pending[i].semantic_profile;
    if (!k80_controller_profile_address_valid(proposed[i].semantic_profile,
            proposed[i].rf_slot, proposed[i].rf_group)) return false;
    for (size_t j = 0; j < i; ++j)
      if (proposed[i].rf_slot == proposed[j].rf_slot &&
          proposed[i].rf_group == proposed[j].rf_group) return false;
    cursor = end + 1;
  }
  bool changed = false;
  // OFF must finish at the old address before a light can be retargeted.
  for (size_t i = 0; i < queue->count; ++i) {
    const bool entry_changed = queue->pending[i].enabled != (proposed[i].rf_slot >= 0) ||
        (proposed[i].rf_slot >= 0 && (queue->pending[i].slot != proposed[i].rf_slot ||
        queue->pending[i].group != proposed[i].rf_group));
    changed |= entry_changed;
    if (entry_changed && (queue->pending[i].state.level != 0 || queue->pending[i].remaining != 0))
      return false;
    if (entry_changed && light_idle && !light_idle(i)) return false;
  }
  if (!changed) return true;
  for (size_t i = 0; i < queue->count; ++i) {
    queue->pending[i].enabled = proposed[i].rf_slot >= 0;
    if (queue->pending[i].enabled) {
      queue->pending[i].slot = static_cast<uint8_t>(proposed[i].rf_slot);
      queue->pending[i].group = static_cast<uint8_t>(proposed[i].rf_group);
    }
  }
  return true;
}

inline std::string addresses(const k80_controller_queue &queue) {
  std::string result;
  for (size_t i = 0; i < queue.count; ++i) {
    if (i) result += ',';
    if (!queue.pending[i].enabled) {
      result += '-';
      continue;
    }
    result += std::to_string(queue.pending[i].slot) + ':' +
        std::to_string(queue.pending[i].group);
  }
  return result;
}

// Public channel numbers are one-based; the RF codec remains zero-based.
// CH1..5 -> slots0..4 is measured; higher channels use the same model, not
// a claim of physical validation on every lamp.
inline bool replace_positions(k80_controller_queue *queue, const std::string &text,
    std::function<bool(size_t)> light_idle = nullptr) {
  if (!queue || text.size() > 255 || text.find('\0') != std::string::npos) return false;
  std::string legacy;
  size_t count = 0, cursor = 0;
  while (cursor < text.size()) {
    if (count >= queue->count) return false;
    const size_t comma = text.find(',', cursor);
    const size_t end = comma == std::string::npos ? text.size() : comma;
    const auto entry = text.substr(cursor, end - cursor);
    if (count++) legacy += ',';
    if (entry == "-") legacy += '-';
    else {
      if (entry.size() < 2 || entry.back() < 'A' || entry.back() > 'F') return false;
      const auto channel_text = entry.substr(0, entry.size() - 1);
      const int channel = decimal(channel_text.c_str(), K80_CONTROLLER_RF_SLOT_COUNT);
      if (channel < 1) return false;
      legacy += std::to_string(channel - 1) + ':' + std::to_string(entry.back() - 'A');
    }
    if (comma == std::string::npos) break;
    cursor = comma + 1;
    if (cursor == text.size()) return false;
  }
  while (count < queue->count) {
    if (count++) legacy += ',';
    legacy += '-';
  }
  return replace_addresses(queue, legacy, light_idle);
}

inline std::string positions(const k80_controller_queue &queue) {
  size_t count = queue.count;
  while (count && !queue.pending[count - 1].enabled) --count;
  std::string result;
  for (size_t i = 0; i < count; ++i) {
    if (i) result += ',';
    if (!queue.pending[i].enabled) result += '-';
    else {
      result += std::to_string(queue.pending[i].slot + 1);
      result += static_cast<char>('A' + queue.pending[i].group);
    }
  }
  return result;
}

}  // namespace k80_controller_config
