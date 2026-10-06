#pragma once

#include "k80_controller_scheduler.h"
#include <string>
#include <functional>
#include <vector>
#include <utility>

namespace k80_controller_config {

struct Result {
  std::string error;
  explicit operator bool() const { return this->error.empty(); }
};

inline void append_position(std::string &text, int slot, int group) {
  if (!text.empty()) text += ',';
  if (slot < 0) text += '-';
  else {
    text += std::to_string(slot + 1);
    text += static_cast<char>('A' + group);
  }
}

// A parsed table owns its addresses across validation, persistence and application.
// Canonical text is derived, never a second independently mutable representation.
struct Positions {
  std::vector<k80_controller_endpoint> endpoints;
  std::string text() const {
    size_t count = this->endpoints.size();
    while (count && this->endpoints[count - 1].slot < 0) --count;
    std::string result;
    for (size_t i = 0; i < count; ++i)
      append_position(result, this->endpoints[i].slot, this->endpoints[i].group);
    return result;
  }
};

inline Result entry_error(size_t position, const std::string &reason) {
  return {"Position " + std::to_string(position + 1) + ": " + reason};
}

inline bool queue_valid(const k80_controller_queue *queue) {
  return queue && queue->pending && queue->count > 0 && queue->count <= K80_CONTROLLER_ENDPOINT_LIMIT;
}

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

// Validate the complete table before applying it, permitting address swaps
// without a transient duplicate. Disabled entries do not reserve an address.
inline Result validate_addresses(const k80_controller_queue *queue, const k80_controller_endpoint *proposed) {
  if (!queue_valid(queue) || !proposed) return {"Pair storage is not initialized"};
  for (size_t i = 0; i < queue->count; ++i) {
    if (proposed[i].profile != queue->pending[i].profile)
      return entry_error(i, "address remapping cannot change the profile");
    if (proposed[i].slot < 0) continue;
    if (!k80_controller_profile_address_valid(proposed[i].profile,
        proposed[i].slot, proposed[i].group)) return entry_error(i, "address is not supported by this profile");
    for (size_t j = 0; j < i; ++j)
      if (proposed[i].slot == proposed[j].slot &&
          proposed[i].group == proposed[j].group)
        return entry_error(i, "duplicate address already used by position " + std::to_string(j + 1));
  }
  return {};
}

inline Result apply_addresses(k80_controller_queue *queue,
                              const k80_controller_endpoint *proposed,
                              const std::function<void(size_t)> &before_remap = nullptr) {
  const auto result = validate_addresses(queue, proposed);
  if (!result) return result;
  // A queued command belongs to its old address, not to the position's next one.
  for (size_t i = 0; i < queue->count; ++i) {
    const bool entry_changed = queue->pending[i].enabled != (proposed[i].slot >= 0) ||
                               (proposed[i].slot >= 0 && (queue->pending[i].slot != proposed[i].slot ||
                                   queue->pending[i].group != proposed[i].group));
    if (!entry_changed) continue;
    if (before_remap) before_remap(i);
    queue->pending[i].remaining = 0;
  }
  for (size_t i = 0; i < queue->count; ++i) {
    queue->pending[i].enabled = proposed[i].slot >= 0;
    if (queue->pending[i].enabled) {
      queue->pending[i].slot = static_cast<uint8_t>(proposed[i].slot);
      queue->pending[i].group = static_cast<uint8_t>(proposed[i].group);
    }
  }
  return {};
}

inline Result replace_addresses(k80_controller_queue *queue, const std::string &text,
                                std::function<void(size_t)> before_remap = nullptr) {
  if (!queue_valid(queue)) return {"Pair storage is not initialized"};
  if (text.find('\0') != std::string::npos) return {"Pair table contains a NUL character"};
  k80_controller_endpoint proposed[K80_CONTROLLER_ENDPOINT_LIMIT];
  size_t cursor = 0;
  for (size_t i = 0; i < queue->count; ++i) {
    const size_t colon = text.find(':', cursor);
    const size_t comma = text.find(',', cursor);
    const size_t end = comma == std::string::npos ? text.size() : comma;
    if ((i + 1 == queue->count) != (comma == std::string::npos))
      return {"Address table must contain exactly " + std::to_string(queue->count) + " positions"};
    proposed[i] = {-1, -1, queue->pending[i].profile};
    if (text.substr(cursor, end - cursor) != "-") {
      if (colon == std::string::npos || colon >= end) return entry_error(i, "expected slot:group or '-'");
      const auto slot = text.substr(cursor, colon - cursor);
      const auto group = text.substr(colon + 1, end - colon - 1);
      proposed[i].slot = decimal(slot.c_str(), K80_CONTROLLER_RF_SLOT_COUNT - 1);
      proposed[i].group = decimal(group.c_str(), K80_GROUP_COUNT - 1);
      if (proposed[i].slot < 0) return entry_error(i, "slot must be a canonical decimal in 0..47");
      if (proposed[i].group < 0) return entry_error(i, "group must be a canonical decimal in 0..5");
    }
    cursor = end + 1;
  }
  return apply_addresses(queue, proposed, before_remap);
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
inline Result parse_positions(const k80_controller_queue *queue, const std::string &text,
                              k80_controller_endpoint *proposed) {
  if (!queue_valid(queue) || !proposed) return {"Pair storage is not initialized"};
  if (text.size() > 255) return {"Pair table exceeds 255 characters"};
  if (text.find('\0') != std::string::npos) return {"Pair table contains a NUL character"};
  size_t count = 0, cursor = 0;
  while (cursor < text.size()) {
    if (count >= queue->count) return {"Pair table exceeds capacity of " + std::to_string(queue->count) + " positions"};
    const size_t comma = text.find(',', cursor);
    const size_t end = comma == std::string::npos ? text.size() : comma;
    const auto entry = text.substr(cursor, end - cursor);
    proposed[count] = {-1, -1, queue->pending[count].profile};
    if (entry != "-") {
      if (entry.size() < 2) return entry_error(count, "expected channel+group such as 1A, or '-'");
      if (entry.back() < 'A' || entry.back() > 'F') return entry_error(count, "group must be uppercase A..F");
      const auto channel_text = entry.substr(0, entry.size() - 1);
      const int channel = decimal(channel_text.c_str(), K80_CONTROLLER_RF_SLOT_COUNT);
      if (channel < 1) return entry_error(count, "channel must be a canonical decimal in 1..48");
      proposed[count].slot = channel - 1;
      proposed[count].group = entry.back() - 'A';
    }
    ++count;
    if (comma == std::string::npos) break;
    cursor = comma + 1;
    if (cursor == text.size()) return entry_error(count, "trailing comma leaves an empty position");
  }
  while (count < queue->count) {
    proposed[count] = {-1, -1, queue->pending[count].profile};
    ++count;
  }
  return validate_addresses(queue, proposed);
}

// Validation and canonicalization do not consume pending light or radio writes.
inline Result prepare_positions(const k80_controller_queue *queue, const std::string &text, Positions *output) {
  if (!queue_valid(queue) || !output) return {"Pair storage is not initialized"};
  Positions proposed;
  proposed.endpoints.resize(queue->count);
  const auto result = parse_positions(queue, text, proposed.endpoints.data());
  if (result) *output = std::move(proposed);
  return result;
}

inline Result apply_positions(k80_controller_queue *queue, const Positions &proposed,
                              const std::function<void(size_t)> &before_remap = nullptr) {
  if (!queue_valid(queue)) return {"Pair storage is not initialized"};
  if (proposed.endpoints.size() != queue->count) return {"Prepared pair table does not match the position capacity"};
  return apply_addresses(queue, proposed.endpoints.data(), before_remap);
}

inline Result replace_positions(k80_controller_queue *queue, const std::string &text,
                                std::function<void(size_t)> before_remap = nullptr) {
  Positions proposed;
  const auto result = prepare_positions(queue, text, &proposed);
  if (!result) return result;
  return apply_positions(queue, proposed, before_remap);
}

inline std::string positions(const k80_controller_queue &queue) {
  size_t count = queue.count;
  while (count && !queue.pending[count - 1].enabled) --count;
  std::string result;
  for (size_t i = 0; i < count; ++i)
    append_position(result, queue.pending[i].enabled ? queue.pending[i].slot : -1, queue.pending[i].group);
  return result;
}

}  // namespace k80_controller_config
