#include "k80_controller_protocol.h"

#include <limits.h>
#include <stdio.h>

static int check(int condition, const char *message) {
  if (!condition) fprintf(stderr, "FAIL: %s\n", message);
  return !condition;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static int decode(const char *hex, uint8_t bytes[12]) {
  if (strlen(hex) != 24) return -1;
  for (size_t i = 0; i < 12; ++i) {
    const int high = hex_digit(hex[2 * i]);
    const int low = hex_digit(hex[2 * i + 1]);
    if (high < 0 || low < 0) return -1;
    bytes[i] = (uint8_t)((high << 4) | low);
  }
  return 0;
}

static int check_capture(const char *hex, int valid) {
  uint8_t captured[12], actual[12];
  if (decode(hex, captured) != 0) return check(0, "capture must be exactly 12 hex bytes");
  unsigned sum = 0;
  for (size_t i = 0; i < 9; ++i) sum += captured[i];
  const int checks_ok = (uint8_t)sum == captured[9] &&
                        k80_crc16(captured, sizeof(captured)) == 0;
  int failures = check(checks_ok == valid, "capture validity agrees with index");
  const int built = k80_controller_build_packet(captured[1], captured[3], actual);
  if (valid)
    failures += check(built == 0 && memcmp(actual, captured, sizeof(actual)) == 0,
                      "every valid capture is reproduced byte for byte");
  else
    failures += check(built != 0 || memcmp(actual, captured, sizeof(actual)) != 0,
                      "invalid capture is never reproduced");
  if (failures) fprintf(stderr, "capture: %s\n", hex);
  return failures;
}

// Independent bytewise polynomial remainder, also used to forge intact unknown bodies.
static uint16_t independent_crc(const uint8_t *body, size_t length) {
  unsigned remainder = 0x1D0F;
  for (size_t i = 0; i < length; ++i) {
    remainder = (remainder << 8) ^ ((unsigned)body[i] << 16);
    for (int bit = 23; bit >= 16; --bit)
      if (remainder & (1u << bit)) remainder ^= 0x11021u << (bit - 16);
  }
  return (uint16_t)remainder;
}

static void seal(uint8_t body[12]) {
  unsigned sum = 0;
  for (int i = 0; i < 9; ++i) sum += body[i];
  body[9] = (uint8_t)sum;
  const uint16_t crc = independent_crc(body, 10);
  body[10] = (uint8_t)(crc >> 8);
  body[11] = (uint8_t)crc;
}

static int check_raw_native_addresses(void) {
  // Synthetic raw-domain bodies, not admission or support for another fixture.
  const int profile = K80_CONTROLLER_PROFILE_NATIVE;
  const char *const group_one[] = {
    "3601000001B4006408584699", "3601000101B400640859EED9"
  };
  int failures = 0;
  for (int level = 0; level <= 1; ++level) {
    const k80_control_values expected = {0, level, 1, 0, 100, 1};
    uint8_t golden[12], actual[12];
    decode(group_one[level], golden);
    failures += check(k80_controller_build_state_packet(profile, 47, 1, &expected, actual) == 0 &&
                      memcmp(actual, golden, 12) == 0, "synthetic group1 ON/OFF exact independent golden");
    for (int slot = 0; slot < 48; ++slot) {
      k80_control_values decoded = {2, 100, 74, 360, 50, 9};
      failures += check(k80_controller_build_state_packet(profile, slot, 1, &expected, actual) == 0 &&
                        memcmp(actual, golden, 12) == 0, "carrier does not alter payload address");
      failures += check(k80_controller_decode_state_packet(profile, slot, 1, golden, &decoded) == 0 &&
                        decoded.mode == 0 && decoded.level == level && decoded.ct_index == 1,
                        "expected group admits synthetic canonical state");
      const k80_control_values before = decoded;
      failures += check(k80_controller_decode_state_packet(profile, slot, 0, golden, &decoded) == -1 &&
                        memcmp(&before, &decoded, sizeof(before)) == 0,
                        "valid-integrity wrong group preserves decode output");
    }
  }
  const int invalid[] = {INT_MIN, -1, 48, 256, INT_MAX};
  const k80_control_values expected = {2, 1, 1, 0, 100, 9};
  uint8_t actual[12], sentinel[12];
  memset(sentinel, 0xA5, 12);
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    memcpy(actual, sentinel, 12);
    failures += check(k80_controller_build_state_packet(profile, invalid[i], 1, &expected, actual) == -1 &&
                      memcmp(actual, sentinel, 12) == 0, "raw slot full-width bounds preserve output");
    failures += check(k80_controller_build_state_packet(profile, 0, invalid[i], &expected, actual) == -1 &&
                      memcmp(actual, sentinel, 12) == 0, "raw group full-width bounds preserve output");
    k80_control_values decoded = expected;
    uint8_t golden[12];
    decode(group_one[1], golden);
    failures += check(k80_controller_decode_state_packet(profile, invalid[i], 1, golden, &decoded) == -1 &&
                      k80_controller_decode_state_packet(profile, 0, invalid[i], golden, &decoded) == -1 &&
                      memcmp(&expected, &decoded, sizeof(decoded)) == 0, "invalid expected address preserves decoded state");
  }
  failures += check(k80_controller_profile_is_native(profile) &&
                    !k80_controller_profile_is_native(K80_CONTROLLER_PROFILE_CCT_2700),
                    "native profile uses the capability predicate");
  for (int slot = 0; slot < 48; ++slot) {
    for (int group = 0; group < 6; ++group) {
      failures += check(k80_controller_build_state_packet(profile, slot, group, &expected, actual) == 0 &&
                        actual[1] == group && independent_crc(actual, 12) == 0,
                        "all bounded synthetic ON addresses retain group and integrity");
      k80_control_values off = expected;
      off.level = 0;
      failures += check(k80_controller_build_state_packet(profile, slot, group, &off, actual) == 0 &&
                        actual[1] == group && actual[2] == 0 && actual[3] == 0 && independent_crc(actual, 12) == 0,
                        "all bounded synthetic OFF addresses retain group and canonical mode");
    }
  }
  return failures;
}

static int check_raw_frequency_profile(void) {
  int failures = 0;
  const uint8_t prefixes[3][9] = {
    {0x36, 0, 0, 1, 74, 0xB4, 0, 100, 8},
    {0x36, 0, 1, 1, 0, 0x2C, 1, 50, 1},
    {0x36, 0, 2, 1, 0, 0, 0, 100, 9}
  };
  for (int slot = 0; slot < 48; ++slot) for (int group = 0; group < 6; ++group) {
      for (int mode = 0; mode < 3; ++mode) {
        k80_control_values state = {mode, 1, 74, 300, 50, 9}, decoded;
        uint8_t expected[12] = {0}, actual[12];
        memcpy(expected, prefixes[mode], 9);
        expected[1] = (uint8_t)group;
        seal(expected);
        failures += check(k80_controller_build_state_packet(5, slot, group, &state, actual) == 0 &&
                          memcmp(actual, expected, 12) == 0 &&
                          k80_controller_decode_state_packet(5, slot, group, expected, &decoded) == 0 &&
                          decoded.mode == mode && decoded.level == 1,
                          "raw opt-in reproduces independent mode bodies at all 288 tuples");
        state.level = 0;
        const uint8_t zero[9] = {0x36, 0, 0, 0, 1, 0xB4, 0, 100, 8};
        memcpy(expected, zero, 9);
        expected[1] = (uint8_t)group;
        seal(expected);
        failures += check(k80_controller_build_state_packet(5, slot, group, &state, actual) == 0 &&
                          memcmp(actual, expected, 12) == 0, "raw opt-in OFF is canonical per group");
      }
    }
  const int invalid[] = {INT_MIN, -1, 48, 256, INT_MAX};
  const k80_control_values state = {1, 1, 1, 300, 50, 1};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    uint8_t output[12], before[12];
    memset(before, 0xA5, 12);
    memcpy(output, before, 12);
    k80_control_values decoded = state;
    failures += check(!k80_controller_profile_address_valid(5, invalid[i], 0) &&
                      !k80_controller_profile_address_valid(5, 0, invalid[i]) &&
                      k80_controller_build_state_packet(5, invalid[i], 0, &state, output) == -1 &&
                      memcmp(output, before, 12) == 0 &&
                      k80_controller_decode_state_packet(5, invalid[i], 0, before, &decoded) == -1 &&
                      memcmp(&decoded, &state, sizeof(state)) == 0, "raw invalid full-width address leaves outputs intact");
    failures += check(k80_controller_build_state_packet(5, 0, invalid[i], &state, output) == -1 &&
                      memcmp(output, before, 12) == 0 &&
                      k80_controller_decode_state_packet(5, 0, invalid[i], before, &decoded) == -1 &&
                      memcmp(&decoded, &state, sizeof(state)) == 0, "raw invalid group leaves outputs intact");
  }
  failures += check(!k80_controller_profile_address_valid(5, 0, 6), "raw group six rejected");
  return failures;
}

static int check_native(void) {
  const int invalid_measured[] = {INT_MIN, -1, 48, 256, INT_MAX};
  for (size_t i = 0; i < sizeof(invalid_measured) / sizeof(invalid_measured[0]); ++i) {
    if (k80_controller_profile_address_valid(4, invalid_measured[i], 0) ||
        k80_controller_profile_address_valid(4, 0, invalid_measured[i]))
      return check(0, "experimental native profile rejects full-width invalid addresses");
  }
  if (!k80_controller_profile_is_native(4) || !k80_controller_profile_is_native(5) ||
      k80_controller_profile_is_native(6))
    return check(0, "experimental native capability identity is explicit");
  for (int slot = 0; slot < 48; ++slot) {
    for (int group = 0; group < 6; ++group) {
      if (k80_controller_profile_address_valid(4, slot, group) != (slot <= 3))
        return check(0, "experimental native profile admits measured carriers only");
      if (k80_controller_profile_address_valid(3, slot, group) != (slot == 0 && group == 0))
        return check(0, "ordinary native admission remains strict");
      if (!k80_controller_profile_address_valid(5, slot, group))
        return check(0, "explicit raw native profile admits all 288 syntactic tuples");
    }
  }
  const int profile = K80_CONTROLLER_PROFILE_NATIVE;
  // Measured labels are independent of either codec direction. Unused fields
  // are valid builder inputs, not assertions about retained desired state.
  static const struct {
    const char *label;
    const char *hex;
    k80_control_values expected;
  } goldens[] = {
    {"CCT2700 1%", "3600000101B40064085815DB", {0, 1, 1, 0, 100, 1}},
    {"canonical OFF", "3600000001B4006408575C55", {0, 0, 1, 0, 100, 1}},
    {"HSI hue0 sat100", "3600010100000064019D83E1", {1, 1, 1, 0, 100, 1}},
    {"HSI hue120 sat100", "360001010078006401158CCA", {1, 1, 1, 120, 100, 1}},
    {"HSI hue300 sat100", "36000101002C016401CA5CD8", {1, 1, 1, 300, 100, 1}},
    {"HSI hue300 sat0", "36000101002C010001666F15", {1, 1, 1, 300, 0, 1}},
    {"HSI hue300 sat50", "36000101002C01320198CA01", {1, 1, 1, 300, 50, 1}},
    {"HSI hue360 sat100", "36000101006801640106DC76", {1, 1, 1, 360, 100, 1}},
    {"CCT2800 index2", "3600000102B400640859CB1A", {0, 1, 2, 0, 100, 1}},
    {"CCT10000 index74", "360000014AB4006408A1C25F", {0, 1, 74, 0, 100, 1}},
    {"FLS1 SOS", "3600020100000064019E7BF7", {2, 1, 1, 0, 100, 1}},
    {"FLS2 Lightning1", "3600020100000064029F3E85", {2, 1, 1, 0, 100, 2}},
    {"FLS3 Lightning2", "360002010000006403A0CA08", {2, 1, 1, 0, 100, 3}},
    {"FLS4 TV Screen", "360002010000006404A143BE", {2, 1, 1, 0, 100, 4}},
    {"FLS5 Police", "360002010000006405A240EC", {2, 1, 1, 0, 100, 5}},
    {"FLS6 Ambulance", "360002010000006406A3059E", {2, 1, 1, 0, 100, 6}},
    {"FLS7 Fire Engine", "360002010000006407A44648", {2, 1, 1, 0, 100, 7}},
    {"FLS8 RGB Circle1", "360002010000006408A54657", {2, 1, 1, 0, 100, 8}},
    {"FLS9 RGB Circle2", "360002010000006409A64505", {2, 1, 1, 0, 100, 9}},
  };
  int failures = 0;
  for (size_t i = 0; i < sizeof(goldens) / sizeof(goldens[0]); ++i) {
    uint8_t body[12], rebuilt[12];
    decode(goldens[i].hex, body);
    const k80_control_values *expected = &goldens[i].expected;
    k80_control_values state = {0, 1, 1, 0, 100, 1};
    failures += check(independent_crc(body, 12) == 0, "independent golden CRC");
    failures += check(k80_controller_decode_state_packet(profile, 0, 0, body, &state) == 0,
                      "native golden admitted");
    const int semantic_ok = state.mode == expected->mode && state.level == expected->level &&
                            (state.mode != K80_MODE_CCT || state.ct_index == expected->ct_index) &&
                            (state.mode != K80_MODE_HSI ||
                             (state.hue == expected->hue && state.saturation == expected->saturation)) &&
                            (state.mode != K80_MODE_FLS || state.effect == expected->effect);
    failures += check(semantic_ok, "native golden decodes to independently labeled active fields");
    const int encoding_ok = k80_controller_build_state_packet(profile, 0, 0, expected, rebuilt) == 0 &&
                            memcmp(body, rebuilt, 12) == 0;
    failures += check(encoding_ok, "independently labeled state encodes exact native golden");
    if (!semantic_ok || !encoding_ok) fprintf(stderr, "native golden: %s\n", goldens[i].label);
    for (int byte = 0; byte < 12; ++byte) {
      k80_control_values unchanged = state, output = state;
      body[byte] ^= 1;
      failures += check(k80_controller_decode_state_packet(profile, 0, 0, body, &output) == -1 &&
                        memcmp(&output, &unchanged, sizeof(output)) == 0, "corruption preserves decode output");
      body[byte] ^= 1;
    }
  }
  k80_control_values state = {1, 1, 1, 300, 50, 1};
  const int invalid[] = {INT_MIN, -1, 361, 65536, INT_MAX};
  uint8_t body[12], unchanged[12];
  memset(unchanged, 0xA5, 12);
  for (int field = 0; field < 6; ++field) {
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
      k80_control_values bad = state;
      switch (field) {
        case 0:
          bad.mode = invalid[i];
          break;
        case 1:
          bad.level = invalid[i];
          break;
        case 2:
          bad.ct_index = invalid[i];
          break;
        case 3:
          bad.hue = invalid[i];
          break;
        case 4:
          bad.saturation = invalid[i];
          break;
        case 5:
          bad.effect = invalid[i];
          break;
      }
      memcpy(body, unchanged, 12);
      failures += check(k80_controller_build_state_packet(profile, 0, 0, &bad, body) == -1 &&
                        memcmp(body, unchanged, 12) == 0, "full-width invalid field preserves output");
    }
  }
  for (int mode = 0; mode <= 2; ++mode) {
    state.mode = mode;
    state.level = 0;
    uint8_t off[12];
    decode(goldens[1].hex, off);
    failures += check(k80_controller_build_state_packet(profile, 0, 0, &state, body) == 0 &&
                      memcmp(off, body, 12) == 0, "OFF canonicalizes every mode");
  }
  state.level = 1;
  const k80_control_values upper_bad[] = {
    {3, 1, 1, 0, 100, 1}, {0, 101, 1, 0, 100, 1}, {0, 1, 75, 0, 100, 1},
    {0, 1, 1, 361, 100, 1}, {0, 1, 1, 0, 101, 1}, {0, 1, 1, 0, 100, 0}, {0, 1, 1, 0, 100, 10},
  };
  for (size_t i = 0; i < sizeof(upper_bad) / sizeof(upper_bad[0]); ++i) {
    memcpy(body, unchanged, 12);
    failures += check(k80_controller_build_state_packet(profile, 0, 0, &upper_bad[i], body) == -1 &&
                      memcmp(body, unchanged, 12) == 0, "inactive field domain boundaries are enforced");
  }
  for (int mode = 0; mode <= 2; ++mode) {
    k80_control_values edge = {mode, 100, 74, 360, 100, 9}, decoded;
    failures += check(k80_controller_build_state_packet(profile, 0, 0, &edge, body) == 0 &&
                      k80_controller_decode_state_packet(profile, 0, 0, body, &decoded) == 0 &&
                      decoded.mode == mode && decoded.level == 100, "provisional host domain maxima admitted");
    edge.ct_index = edge.hue = edge.saturation = 0;
    edge.level = edge.effect = 1;
    failures += check(k80_controller_build_state_packet(profile, 0, 0, &edge, body) == 0 &&
                      k80_controller_decode_state_packet(profile, 0, 0, body, &decoded) == 0,
                      "provisional host domain minima admitted");
  }
  for (int other_profile = -1; other_profile <= 6; ++other_profile) {
    if (other_profile == profile || other_profile == 4 || other_profile == 5) continue;
    memcpy(body, unchanged, 12);
    failures += check(k80_controller_build_state_packet(other_profile, 0, 0, &state, body) == -1 &&
                      memcmp(body, unchanged, 12) == 0, "typed codec rejects other profiles");
  }
  memcpy(body, unchanged, 12);
  failures += check(k80_controller_build_profile_packet(profile, 0, 0, 1, body) == -1 &&
                    memcmp(body, unchanged, 12) == 0, "level-only API cannot guess native state");
  for (int slot = -1; slot <= 48; ++slot) {
    if (slot >= 0 && slot < 48) continue;
    memcpy(body, unchanged, 12);
    failures += check(k80_controller_build_state_packet(profile, slot, 0, &state, body) == -1 &&
                      memcmp(body, unchanged, 12) == 0, "native raw codec rejects out-of-domain slot");
  }
  for (int group = -1; group <= 6; ++group) {
    if (group >= 0 && group < 6) continue;
    failures += check(k80_controller_build_state_packet(profile, 0, group, &state, body) == -1,
                      "native raw codec rejects out-of-domain group");
  }
  for (int byte = 0; byte < 9; ++byte) {
    decode(goldens[0].hex, body);
    body[byte] = 255;
    seal(body);
    k80_control_values output = state;
    failures += check(k80_controller_decode_state_packet(profile, 0, 0, body, &output) == -1 &&
                      memcmp(&output, &state, sizeof(state)) == 0, "intact unknown body rejected without guessing");
  }
  failures += check(k80_controller_build_state_packet(profile, 0, 0, NULL, body) == -1 &&
                    k80_controller_build_state_packet(profile, 0, 0, &state, NULL) == -1 &&
                    k80_controller_decode_state_packet(profile, 0, 0, NULL, &state) == -1 &&
                    k80_controller_decode_state_packet(profile, 0, 0, body, NULL) == -1,
                    "native null pointers rejected");
  return failures;
}

int main(int argc, char **argv) {
  // Twelve observed endpoints, independent of the implementation's seeds.
  static const char *const endpoints[6][2] = {
    {"3600000000B40064085609D4", "3600006400B4006408BA47AA"},
    {"360100000004016408A8B4AC", "3601006400040164080C331E"},
    {"360200000037016408DC45C7", "36020064003701640840752E"},
    {"360300000078006401166E5F", "3603006400780064017AB1A9"},
    {"36040000007800640117CFD5", "3604006400780064017B1023"},
    {"36050000007800640118D519", "3605006400780064017C8BE7"},
  };
  int failures = check_native() + check_raw_native_addresses() + check_raw_frequency_profile();
  uint8_t captured_cct[12], cct_actual[12] = {0};
  decode("3600000101B40064085815DB", captured_cct);
  failures += check(k80_controller_build_profile_packet(
                      K80_CONTROLLER_PROFILE_CCT_2700, 0, 0, 1, cct_actual) == 0 &&
                    memcmp(captured_cct, cct_actual, 12) == 0,
                    "captured CCT2700 1 percent is reproduced");
  decode("3600000001B4006408575C55", captured_cct);
  failures += check(k80_controller_build_profile_packet(
                      K80_CONTROLLER_PROFILE_CCT_2700, 0, 0, 0, cct_actual) == 0 &&
                    memcmp(captured_cct, cct_actual, 12) == 0, "captured CCT2700 OFF is reproduced");
  for (int level = 0; level <= 100; ++level) {
    failures += check(k80_controller_build_profile_packet(
                        K80_CONTROLLER_PROFILE_CCT_2700, 0, 0, level, cct_actual) == 0,
                      "captured brightness profile accepts every integer level");
    k80_frame_checks checks = k80_check_frame(cct_actual, 12);
    failures += check(checks.sum_ok && checks.crc_ok && cct_actual[3] == level,
                      "captured profile emits level with both checks");
    for (int i = 0; i < 9; ++i)
      if (i != 3) failures += check(cct_actual[i] == captured_cct[i],
                                      "captured opaque bytes remain fixed");
  }
  memset(cct_actual, 0xA5, 12);
  uint8_t original_cct[12];
  memcpy(original_cct, cct_actual, 12);
  const int bad_profile[] = {0, -1, 3, INT_MAX};
  for (size_t i = 0; i < sizeof(bad_profile) / sizeof(bad_profile[0]); ++i)
    failures += check(k80_controller_build_profile_packet(
                        bad_profile[i], 0, 0, 1, cct_actual) == -1 &&
                      memcmp(cct_actual, original_cct, 12) == 0, "unknown profile preserves output");
  for (int slot = -1; slot <= 48; ++slot) {
    if (slot == 0) continue;
    failures += check(k80_controller_build_profile_packet(
                        K80_CONTROLLER_PROFILE_CCT_2700, slot, 0, 1, cct_actual) == -1 &&
                      memcmp(cct_actual, original_cct, 12) == 0, "captured profile rejects other slots");
  }
  for (int group = -1; group <= 6; ++group) {
    if (group == 0) continue;
    failures += check(k80_controller_build_profile_packet(
                        K80_CONTROLLER_PROFILE_CCT_2700, 0, group, 1, cct_actual) == -1 &&
                      memcmp(cct_actual, original_cct, 12) == 0, "captured profile rejects other groups");
  }
  failures += check(k80_controller_build_profile_packet(
                      K80_CONTROLLER_PROFILE_CCT_2700, 0, 0, -1, cct_actual) == -1 &&
                    memcmp(cct_actual, original_cct, 12) == 0, "captured profile rejects negative level");
  failures += check(k80_controller_build_profile_packet(
                      K80_CONTROLLER_PROFILE_CCT_2700, 0, 0, 101, cct_actual) == -1 &&
                    memcmp(cct_actual, original_cct, 12) == 0, "captured profile rejects excessive level");
  failures += check(k80_controller_build_profile_packet(
                      K80_CONTROLLER_PROFILE_CCT_2700, 0, 0, 1, NULL) == -1,
                    "captured profile rejects null output");
  failures += check(k80_crc16((const uint8_t *)"123456789", 9) == 0xE5CC,
                    "standard CRC check vector");
  failures += check(k80_crc16(NULL, 0) == 0x1D0F, "empty CRC preserves initial state");
  for (int group = 0; group < 6; ++group) {
    for (int endpoint = 0; endpoint < 2; ++endpoint)
      failures += check_capture(endpoints[group][endpoint], 1);
    uint8_t seed[12];
    if (decode(endpoints[group][0], seed) != 0) return 1;
    for (int level = 0; level <= 100; ++level) {
      uint8_t guarded[14];
      memset(guarded, 0xA5, sizeof(guarded));
      uint8_t *body = guarded + 1;
      failures += check(k80_controller_build_packet(group, level, body) == 0,
                        "all six groups accept every integer level 0 through 100");
      uint8_t profiled[12];
      failures += check(k80_controller_build_profile_packet(
                          K80_CONTROLLER_PROFILE_BRIGHTNESS, 32, group, level, profiled) == 0 &&
                        memcmp(profiled, body, 12) == 0,
                        "captured brightness profile preserves all six seeds on an independent slot");
      failures += check(body[3] == level && guarded[0] == 0xA5 && guarded[13] == 0xA5,
                        "requested level and exact twelve-byte write boundary");
      unsigned sum = 0;
      for (size_t i = 0; i < 9; ++i) {
        sum += body[i];
        if (i != 3)
          failures += check(body[i] == seed[i], "group-specific opaque state stays unchanged");
      }
      failures += check(body[9] == (uint8_t)sum && k80_crc16(body, 12) == 0,
                        "all generated packets have additive checksum and big-endian CRC");
    }
  }

  const int invalid_groups[] = {INT_MIN, -1, 6, 255, 256, INT_MAX};
  const int invalid_levels[] = {INT_MIN, -1, 101, 255, 256, INT_MAX};
  uint8_t output[12], unchanged[12];
  memset(output, 0xA5, sizeof(output));
  memcpy(unchanged, output, sizeof(output));
  for (size_t i = 0; i < sizeof(invalid_groups) / sizeof(invalid_groups[0]); ++i) {
    failures += check(k80_controller_build_packet(invalid_groups[i], 50, output) == -1 &&
                      memcmp(output, unchanged, sizeof(output)) == 0,
                      "invalid group rejected without output changes or integer narrowing");
  }
  for (size_t i = 0; i < sizeof(invalid_levels) / sizeof(invalid_levels[0]); ++i) {
    for (int group = 0; group < 6; ++group)
      failures += check(k80_controller_build_packet(group, invalid_levels[i], output) == -1 &&
                        memcmp(output, unchanged, sizeof(output)) == 0,
                        "invalid level rejected without output changes or integer narrowing");
  }
  failures += check(k80_controller_build_packet(0, 0, NULL) == -1,
                    "null output is rejected");
  failures += check_capture("360400001078000001613D96", 0);  // Invalid observation 226.

  // Optional external observations: valid:<hex> or invalid:<hex>, generated
  // directly from index.jsonl's AdditiveOK/CRCOK flags by the host runner.
  unsigned valid_count = 0, invalid_count = 0;
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "valid:", 6) == 0) {
      failures += check_capture(argv[i] + 6, 1);
      ++valid_count;
    } else if (strncmp(argv[i], "invalid:", 8) == 0) {
      failures += check_capture(argv[i] + 8, 0);
      ++invalid_count;
    } else {
      failures += check(0, "unrecognized capture argument");
    }
  }
  printf("controller protocol: endpoints=12 levels=606 captures_valid=%u captures_invalid=%u failures=%d\n",
         valid_count, invalid_count, failures);
  return failures ? 1 : 0;
}
