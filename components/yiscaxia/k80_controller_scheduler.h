#pragma once
#include "k80_controller_protocol.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  K80_CONTROLLER_ENDPOINT_LIMIT = (int)K80_CONTROLLER_RF_SLOT_COUNT * (int)K80_GROUP_COUNT,
  K80_CONTROLLER_DEFAULT_ATTEMPTS = 3,
  K80_CONTROLLER_DEFAULT_SPACING_MS = 150,
};

typedef enum {
  K80_CONTROLLER_SEMANTIC_FIELD_NONE = 0,
  K80_CONTROLLER_SEMANTIC_FIELD_BRIGHTNESS = 1U << 0,
  K80_CONTROLLER_SEMANTIC_FIELD_RGB = 1U << 1,
  K80_CONTROLLER_SEMANTIC_FIELD_COLOR_TEMPERATURE = 1U << 2,
  K80_CONTROLLER_SEMANTIC_FIELD_EFFECT = 1U << 3,
  K80_CONTROLLER_SEMANTIC_FIELD_MODE = 1U << 4,
} k80_controller_semantic_field;

typedef struct {
  int rf_slot;
  int rf_group;
  int semantic_profile;
} k80_controller_endpoint;
typedef struct {
  uint8_t slot, group, semantic_profile, remaining, enabled;
  k80_control_values state;
} k80_controller_pending;
typedef struct {
  k80_controller_pending *pending;
  size_t count;
  int armed, draining;
  uint8_t attempts, selected_slot;
  uint16_t spacing_ms;
  size_t next_slot, next_position[K80_CONTROLLER_RF_SLOT_COUNT];
  uint64_t started_slots, slot_started_us[K80_CONTROLLER_RF_SLOT_COUNT];
} k80_controller_queue;

static inline int k80_controller_setting_valid(int spacing, float value) {
  return isfinite(value) && value == floorf(value) && value >= 1 &&
      value <= (spacing ? 65535 : 255);
}
static inline int k80_controller_set_attempts(k80_controller_queue *q, float value) {
  if (!q || !k80_controller_setting_valid(0, value)) return 0;
  q->attempts = (uint8_t)value;
  return 1;
}
static inline int k80_controller_set_spacing(k80_controller_queue *q, float value) {
  if (!q || !k80_controller_setting_valid(1, value)) return 0;
  q->spacing_ms = (uint16_t)value;
  return 1;
}

static inline uint32_t k80_controller_profile_fields(int semantic_profile) {
  if (k80_controller_profile_is_native(semantic_profile))
    return K80_CONTROLLER_SEMANTIC_FIELD_BRIGHTNESS |
        K80_CONTROLLER_SEMANTIC_FIELD_RGB |
        K80_CONTROLLER_SEMANTIC_FIELD_COLOR_TEMPERATURE |
        K80_CONTROLLER_SEMANTIC_FIELD_EFFECT | K80_CONTROLLER_SEMANTIC_FIELD_MODE;
  switch (semantic_profile) {
    case K80_CONTROLLER_SEMANTIC_PROFILE_BRIGHTNESS:
    case K80_CONTROLLER_SEMANTIC_PROFILE_CCT_2700:
      return K80_CONTROLLER_SEMANTIC_FIELD_BRIGHTNESS;
    default:
      return K80_CONTROLLER_SEMANTIC_FIELD_NONE;
  }
}

static inline int k80_controller_profile_supports_field(int semantic_profile, uint32_t field) {
  if (field == 0 || (field & (field - 1U)) != 0) return 0;
  return (k80_controller_profile_fields(semantic_profile) & field) != 0;
}

// Install a private copy once. Validate full-width values before narrowing,
// and validate the entire table before changing any queue/storage state.
static inline int k80_controller_configure(k80_controller_queue *q,
    k80_controller_pending *storage, const k80_controller_endpoint *endpoints, size_t count) {
  if (!q || q->pending || q->armed || !storage || !endpoints || count == 0 ||
      count > K80_CONTROLLER_ENDPOINT_LIMIT)
    return 0;
  for (size_t i = 0; i < count; ++i) {
    if (!k80_controller_profile_address_valid(endpoints[i].semantic_profile,
            endpoints[i].rf_slot, endpoints[i].rf_group) ||
        k80_controller_profile_fields(endpoints[i].semantic_profile) == 0)
      return 0;
    for (size_t j = 0; j < i; ++j)
      if (endpoints[i].rf_slot == endpoints[j].rf_slot && endpoints[i].rf_group == endpoints[j].rf_group)
        return 0;
  }
  for (size_t i = 0; i < count; ++i) {
    storage[i].slot = (uint8_t)endpoints[i].rf_slot;
    storage[i].group = (uint8_t)endpoints[i].rf_group;
    storage[i].semantic_profile = (uint8_t)endpoints[i].semantic_profile;
    storage[i].remaining = 0;
    storage[i].enabled = 1;
    storage[i].state = k80_default_controls();
  }
  q->pending = storage;
  q->count = count;
  q->attempts = K80_CONTROLLER_DEFAULT_ATTEMPTS;
  q->spacing_ms = K80_CONTROLLER_DEFAULT_SPACING_MS;
  return 1;
}
static inline int k80_controller_begin_drain(k80_controller_queue *q) {
  if (!q || !q->pending || q->count == 0 || q->armed || q->draining) return 0;
  q->draining = 1;
  return 1;
}
static inline void k80_controller_end_drain(k80_controller_queue *q) {
  if (!q) return;
  q->draining = 0;
  for (size_t i = 0; i < q->count; ++i) q->pending[i].remaining = 0;
}
static inline int k80_controller_states_equal(const k80_control_values *a,
    const k80_control_values *b) {
  return a->mode == b->mode && a->level == b->level && a->ct_index == b->ct_index &&
      a->hue == b->hue && a->saturation == b->saturation && a->effect == b->effect;
}
// Zero selects the configured total; an explicit total belongs only to this command.
static inline int k80_controller_request_state_attempts(k80_controller_queue *q, int endpoint,
    const k80_control_values *state, int attempts) {
  if (attempts < 0 || attempts > 255) return 0;
  if (!q || (!q->armed && !q->draining) || !q->pending || endpoint < 0 ||
      (size_t)endpoint >= q->count) return 0;
  k80_controller_pending *pending = &q->pending[endpoint];
  if (!pending->enabled) return 0;
  if (!k80_controller_profile_address_valid(pending->semantic_profile, pending->slot, pending->group) ||
      !k80_controller_state_valid(pending->semantic_profile, pending->slot, pending->group, state))
    return 0;
  if (pending->remaining && k80_controller_states_equal(&pending->state, state)) return 1;
  pending->state = *state;
  pending->remaining = attempts ? (uint8_t)attempts : q->attempts;
  return 1;
}
static inline int k80_controller_request_state(k80_controller_queue *q, int endpoint,
    const k80_control_values *state) {
  return k80_controller_request_state_attempts(q, endpoint, state, 0);
}
static inline int k80_controller_request_field(k80_controller_queue *q, int endpoint,
                                                  uint32_t field, float value) {
  if (!q || (!q->armed && !q->draining) || endpoint < 0 || (size_t)endpoint >= q->count ||
      !q->pending ||
      !q->pending[endpoint].enabled ||
      !k80_controller_profile_supports_field(q->pending[endpoint].semantic_profile, field) ||
      field != K80_CONTROLLER_SEMANTIC_FIELD_BRIGHTNESS || !isfinite(value))
    return 0;
  if (value < 0) value = 0;
  if (value > 1) value = 1;
  const int level = k80_quantize_brightness(value);
  k80_controller_pending *pending = &q->pending[endpoint];
  if (k80_controller_profile_is_native(pending->semantic_profile)) {
    k80_control_values state = pending->state;
    state.level = level;
    return k80_controller_request_state(q, endpoint, &state);
  }
  if (pending->remaining && pending->state.level == level) return 1;
  pending->state.level = level;
  pending->remaining = q->attempts;
  return 1;
}
static inline int k80_controller_request(k80_controller_queue *q, int endpoint, float value) {
  return k80_controller_request_field(q, endpoint, K80_CONTROLLER_SEMANTIC_FIELD_BRIGHTNESS, value);
}
static inline int k80_controller_take_state(k80_controller_queue *q, uint64_t now,
    int *endpoint, k80_control_values *state) {
  if (!q || !endpoint || !state || !q->pending || !q->armed) return 0;
  for (size_t i = 0; i < K80_CONTROLLER_RF_SLOT_COUNT; ++i) {
    const size_t slot = (q->next_slot + i) % K80_CONTROLLER_RF_SLOT_COUNT;
    if ((q->started_slots & (UINT64_C(1) << slot)) &&
        now - q->slot_started_us[slot] < (uint64_t)q->spacing_ms * 1000U) continue;
    for (size_t position = 0; position < q->count; ++position) {
      const size_t e = (q->next_position[slot] + position) % q->count;
      if (!q->pending[e].remaining || q->pending[e].slot != slot) continue;
      *endpoint = (int)e;
      *state = q->pending[e].state;
      --q->pending[e].remaining;
      q->next_position[slot] = (e + 1) % q->count;
      q->next_slot = (slot + 1) % K80_CONTROLLER_RF_SLOT_COUNT;
      q->selected_slot = (uint8_t)slot;
      return 1;
    }
  }
  return 0;
}
static inline int k80_controller_take(k80_controller_queue *q, uint64_t now,
    int *endpoint, int *level) {
  if (!level) return 0;
  k80_control_values state;
  if (!k80_controller_take_state(q, now, endpoint, &state)) return 0;
  *level = state.level;
  return 1;
}
static inline void k80_controller_started(k80_controller_queue *q, uint64_t started) {
  q->slot_started_us[q->selected_slot] = started;
  q->started_slots |= UINT64_C(1) << q->selected_slot;
}
static inline void k80_controller_abort(k80_controller_queue *q, int endpoint) {
  if (endpoint >= 0 && (size_t)endpoint < q->count) q->pending[endpoint].remaining = 0;
}
static inline void k80_controller_halt(k80_controller_queue *q) {
  q->armed = 0;
  q->draining = 0;
  for (size_t i = 0; i < q->count; ++i) q->pending[i].remaining = 0;
}
