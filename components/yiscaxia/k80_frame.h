#pragma once

#include <stddef.h>
#include <stdint.h>

// K80 body checks and receive semantics are independent of the radio bus.
enum { K80_FRAME_SIZE = 12 };
enum { K80_MODE_CCT = 0, K80_MODE_HSI = 1, K80_MODE_FLS = 2 };

typedef struct {
  int group, mode, level, ct_index, hue, saturation, effect;
} k80_received_state;

typedef struct {
  int length_ok;
  int sum_ok;
  int crc_ok;
} k80_frame_checks;

static inline uint16_t k80_crc16(const uint8_t *bytes, size_t length) {
  uint16_t crc = 0x1D0F;
  for (size_t i = 0; i < length; ++i) {
    crc ^= (uint16_t)((uint16_t)bytes[i] << 8);
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
  }
  return crc;
}

// Call only with a complete body. The additive check protects bytes 0..8.
static inline uint8_t k80_sum(const uint8_t *bytes) {
  unsigned sum = 0;
  for (size_t i = 0; i < 9; ++i) sum += bytes[i];
  return (uint8_t)sum;
}

static inline k80_frame_checks k80_check_frame(
    const uint8_t *bytes, size_t length) {
  k80_frame_checks checks = {0, 0, 0};
  if (bytes == NULL || length != K80_FRAME_SIZE) return checks;
  checks.length_ok = 1;
  checks.sum_ok = k80_sum(bytes) == bytes[9];
  const uint16_t supplied_crc = (uint16_t)(((uint16_t)bytes[10] << 8) | bytes[11]);
  checks.crc_ok = k80_crc16(bytes, 10) == supplied_crc;
  return checks;
}

// Official receive bodies preserve inactive controls, including when OFF.
// Decode only active fields; keep original bytes and output on rejection.
static inline int k80_decode_received(const uint8_t *bytes, size_t length,
    k80_received_state *output) {
  if (output == NULL) return -1;
  const k80_frame_checks checks = k80_check_frame(bytes, length);
  if (!checks.length_ok || !checks.sum_ok || !checks.crc_ok ||
      bytes[0] != 0x36 || bytes[1] > 5 || bytes[2] > K80_MODE_FLS || bytes[3] > 100)
    return -1;
  k80_received_state state = {bytes[1], bytes[2], bytes[3], 0, 0, 0, 0};
  if (state.mode == K80_MODE_CCT) {
    state.ct_index = bytes[4];
    if (state.ct_index > 74) return -1;
  } else if (state.mode == K80_MODE_HSI) {
    state.hue = bytes[5] | ((int)bytes[6] << 8);
    state.saturation = bytes[7];
    if (state.hue > 360 || state.saturation > 100) return -1;
  } else {
    state.effect = bytes[8];
    if (state.effect < 1 || state.effect > 9) return -1;
  }
  *output = state;
  return 0;
}
