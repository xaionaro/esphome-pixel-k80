#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "k80_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  K80_CONTROLLER_RF_SLOT_COUNT = 48,
  K80_CONTROLLER_MEASURED_RF_SLOT_MAX = 3,
};

typedef enum {
  K80_CONTROLLER_SEMANTIC_PROFILE_NONE = 0,
  K80_CONTROLLER_SEMANTIC_PROFILE_BRIGHTNESS = 1,
  // Exact captured CCT2700 state at raw slot0/group0; only byte3 varies.
  K80_CONTROLLER_SEMANTIC_PROFILE_CCT_2700 = 2,
  K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE = 3,
  // Explicit experimental opt-in on measured carriers; no fixture ACK implied.
  K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_MEASURED_FREQUENCY = 4,
  // Explicit experimental raw-address opt-in; no UI mapping or fixture ACK implied.
  K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_RAW_FREQUENCY = 5,
} k80_controller_semantic_profile;

#ifdef __cplusplus
#define K80_CONTROLLER_CONSTEXPR constexpr
#else
#define K80_CONTROLLER_CONSTEXPR
#endif

static inline K80_CONTROLLER_CONSTEXPR int k80_controller_profile_is_native(int profile) {
  return profile == K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE ||
      profile == K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_MEASURED_FREQUENCY ||
      profile == K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_RAW_FREQUENCY;
}

static inline K80_CONTROLLER_CONSTEXPR int k80_controller_raw_address_valid(int slot, int group) {
  return slot >= 0 && slot < K80_CONTROLLER_RF_SLOT_COUNT &&
      group >= 0 && group < K80_GROUP_COUNT;
}

// Live admission, not the pure codec's syntactic raw-address domain.
static inline K80_CONTROLLER_CONSTEXPR int k80_controller_profile_address_valid(int profile, int slot, int group) {
  if (!k80_controller_raw_address_valid(slot, group)) return 0;
  if (profile == K80_CONTROLLER_SEMANTIC_PROFILE_CCT_2700) return slot == 0 && group == 0;
  if (profile == K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE) return slot == 0 && group == 0;
  if (profile == K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_MEASURED_FREQUENCY)
    return slot <= K80_CONTROLLER_MEASURED_RF_SLOT_MAX;
  if (profile == K80_CONTROLLER_SEMANTIC_PROFILE_NATIVE_RAW_FREQUENCY) return 1;
  return profile == K80_CONTROLLER_SEMANTIC_PROFILE_BRIGHTNESS;
}

#undef K80_CONTROLLER_CONSTEXPR

// Build only the FIFO body, without preamble, RF ID, or hardware CRC.
// These RF group indices are the captured CH03 group values. Their A-F label
// mapping is observed only at CH03; the opaque captured settings are preserved
// per group, and this does not learn subsequent remote state.
// Return 0 on success, -1 on invalid input without changing output.
static inline int k80_controller_build_packet(
    int group, int level, uint8_t output[K80_FRAME_SIZE]) {
  // Captured zero-level prefixes, sequences 14, 20, 25, 48, 4, 9 in
  // k80-raw-frames-20260919T1817/index.jsonl (2026-09-19).
  static const uint8_t seeds[K80_GROUP_COUNT][9] = {
      {0x36, 0x00, 0x00, 0x00, 0x00, 0xB4, 0x00, 0x64, 0x08},
      {0x36, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, 0x64, 0x08},
      {0x36, 0x02, 0x00, 0x00, 0x00, 0x37, 0x01, 0x64, 0x08},
      {0x36, 0x03, 0x00, 0x00, 0x00, 0x78, 0x00, 0x64, 0x01},
      {0x36, 0x04, 0x00, 0x00, 0x00, 0x78, 0x00, 0x64, 0x01},
      {0x36, 0x05, 0x00, 0x00, 0x00, 0x78, 0x00, 0x64, 0x01},
  };
  if (output == NULL || group < 0 || group >= K80_GROUP_COUNT ||
      level < 0 || level > K80_LEVEL_MAX)
    return -1;

  memcpy(output, seeds[group], sizeof(seeds[group]));
  output[3] = (uint8_t)level;
  k80_finalize_frame(output);
  return 0;
}

// Captured profiles preserve their distinct opaque seed fields. Profile
// admission refuses unobserved addresses before writing any output bytes.
static inline int k80_controller_build_profile_packet(int profile, int slot,
    int group, int level, uint8_t output[K80_FRAME_SIZE]) {
  if (!output || level < 0 || level > K80_LEVEL_MAX ||
      !k80_controller_profile_address_valid(profile, slot, group)) return -1;
  if (profile == K80_CONTROLLER_SEMANTIC_PROFILE_BRIGHTNESS)
    return k80_controller_build_packet(group, level, output);
  if (profile != K80_CONTROLLER_SEMANTIC_PROFILE_CCT_2700) return -1;
  static const uint8_t cct_2700[9] = {0x36,0,0,0,1,0xB4,0,0x64,8};
  memcpy(output, cct_2700, sizeof(cct_2700));
  output[3] = (uint8_t)level;
  k80_finalize_frame(output);
  return 0;
}

// Desired controls start OFF with valid remembered color/effect selections.
static inline k80_control_values k80_default_controls(void) {
  const k80_control_values controls = {K80_MODE_CCT, 0, 1, 0, K80_SATURATION_MAX, K80_NATIVE_EFFECT_MIN};
  return controls;
}

// Quantize only an already-admitted finite value in the normalized 0..1 domain.
static inline int k80_quantize_brightness(float value) {
  int level = (int)floorf(value * (float)K80_LEVEL_MAX + 0.5f);
  if (value > 0 && level == 0) level = 1;
  return level;
}

static inline int k80_controller_state_valid(int profile, int slot, int group,
    const k80_control_values *state) {
  return state && k80_controller_profile_is_native(profile) &&
      k80_controller_raw_address_valid(slot, group) &&
      state->mode >= K80_MODE_CCT && state->mode <= K80_MODE_FLS &&
      state->level >= 0 && state->level <= K80_LEVEL_MAX &&
      state->ct_index >= 0 && state->ct_index <= K80_CT_INDEX_MAX &&
      state->hue >= 0 && state->hue <= K80_HUE_MAX &&
      state->saturation >= 0 && state->saturation <= K80_SATURATION_MAX &&
      state->effect >= K80_NATIVE_EFFECT_MIN && state->effect <= K80_NATIVE_EFFECT_MAX;
}

// Parameter ranges are provisional host domains, not proof of every device setting.
// OFF uses the captured CCT2700 zero format with the requested raw group.
// Pure raw-domain construction does not authorize a live endpoint or UI map.
static inline int k80_controller_build_state_packet(int profile, int slot, int group,
    const k80_control_values *state, uint8_t output[K80_FRAME_SIZE]) {
  if (!output || !k80_controller_state_valid(profile, slot, group, state)) return -1;
  uint8_t body[K80_FRAME_SIZE] = {0x36, 0, 0, 0, 1, 0xB4, 0, 100, 8, 0, 0, 0};
  body[1] = (uint8_t)group;
  body[3] = (uint8_t)state->level;
  if (state->level != 0) {
    body[2] = (uint8_t)state->mode;
    if (state->mode == K80_MODE_CCT) body[4] = (uint8_t)state->ct_index;
    else {
      body[4] = 0;
      body[5] = body[6] = 0;
      body[8] = (uint8_t)state->effect;
      if (state->mode == K80_MODE_HSI) {
        body[5] = (uint8_t)state->hue;
        body[6] = (uint8_t)(state->hue >> 8);
        body[7] = (uint8_t)state->saturation;
        body[8] = 1;
      }
    }
  }
  k80_finalize_frame(body);
  memcpy(output, body, sizeof(body));
  return 0;
}

// Pure decoding requires the expected group and a canonical body. Integrity
// or raw-domain admission does not establish empirical live-device support.
static inline int k80_controller_decode_state_packet(int profile, int slot, int group,
    const uint8_t body[K80_FRAME_SIZE], k80_control_values *output) {
  if (!body || !output || !k80_controller_raw_address_valid(slot, group) || body[1] != group)
    return -1;
  const k80_frame_checks checks = k80_check_frame(body, K80_FRAME_SIZE);
  if (!checks.sum_ok || !checks.crc_ok) return -1;
  k80_control_values state = k80_default_controls();
  state.mode = body[2];
  state.level = body[3];
  if (state.mode == K80_MODE_CCT) state.ct_index = body[4];
  else if (state.mode == K80_MODE_HSI) {
    state.hue = body[5] | ((int)body[6] << 8);
    state.saturation = body[7];
  } else if (state.mode == K80_MODE_FLS) state.effect = body[8];
  uint8_t canonical[K80_FRAME_SIZE];
  if (k80_controller_build_state_packet(profile, slot, group, &state, canonical) != 0 ||
      memcmp(body, canonical, sizeof(canonical)) != 0) return -1;
  *output = state;
  return 0;
}

#ifdef __cplusplus
}
#endif
