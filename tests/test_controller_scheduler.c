#include "k80_controller_scheduler.h"
#include "test_check.h"
#include <math.h>
#include <stdio.h>
#include <limits.h>

int main(void) {
  k80_controller_queue raw = {0};
  k80_controller_pending raw_pending[288];
  k80_controller_endpoint raw_endpoints[288];
  for (int i = 0; i < 288; ++i)
    raw_endpoints[i] = (k80_controller_endpoint) {i / 6, i % 6, 5};
  CHECK(k80_controller_configure(&raw, raw_pending, raw_endpoints, 288));
  k80_control_values raw_state = {1, 1, 74, 300, 50, 1}, raw_taken;
  int raw_endpoint = -1;
  CHECK(k80_controller_begin_drain(&raw));
  CHECK(k80_controller_request_state(&raw, 287, &raw_state));
  CHECK(!k80_controller_take_state(&raw, 0, &raw_endpoint, &raw_taken));
  k80_controller_end_drain(&raw);
  raw.armed = 1;
  CHECK(!k80_controller_take_state(&raw, 0, &raw_endpoint, &raw_taken));
  CHECK(k80_controller_request_state(&raw, 0, &raw_state));
  raw_state.hue = 120;
  CHECK(k80_controller_request_state(&raw, 287, &raw_state));
  CHECK(k80_controller_take_state(&raw, 0, &raw_endpoint, &raw_taken) && raw_endpoint == 0 && raw_taken.hue == 300);
  CHECK(k80_controller_request_state(&raw, 0, &raw_pending[0].state) && raw_pending[0].remaining == 2);
  raw_state.mode = 2;
  raw_state.effect = 9;
  CHECK(k80_controller_request_state(&raw, 287, &raw_state) && raw_pending[287].remaining == 3);
  const k80_controller_pending raw_before = raw_pending[287];
  raw_state.hue = INT_MAX;
  CHECK(!k80_controller_request_state(&raw, 287, &raw_state) &&
        memcmp(&raw_before, &raw_pending[287], sizeof(raw_before)) == 0);
  raw_state.hue = 120;
  raw_state.level = 0;
  CHECK(k80_controller_request_state(&raw, 287, &raw_state));
  CHECK(k80_controller_take_state(&raw, 0, &raw_endpoint, &raw_taken) && raw_endpoint == 287 && raw_taken.level == 0);
  CHECK(raw_pending[0].state.hue == 300 && raw_pending[0].state.mode == 1);
  k80_controller_halt(&raw);
  CHECK(!k80_controller_request_state(&raw, 287, &raw_state));
  k80_controller_queue measured = {0};
  k80_controller_pending measured_pending[18];
  k80_controller_endpoint measured_endpoints[18];
  for (int i = 0; i < 18; ++i)
    measured_endpoints[i] = (k80_controller_endpoint) {i / 6, i % 6, 4};
  CHECK(k80_controller_configure(&measured, measured_pending, measured_endpoints, 18));
  k80_control_values measured_state = {1, 1, 1, 120, 100, 1}, measured_taken;
  CHECK(k80_controller_begin_drain(&measured));
  CHECK(k80_controller_request_state(&measured, 17, &measured_state));
  int measured_endpoint = -1;
  CHECK(!k80_controller_take_state(&measured, 0, &measured_endpoint, &measured_taken));
  k80_controller_end_drain(&measured);
  measured.armed = 1;
  CHECK(!k80_controller_take_state(&measured, 0, &measured_endpoint, &measured_taken));
  for (int i = 0; i < 18; ++i) {
    measured_state.hue = i * 20;
    CHECK(k80_controller_request_state(&measured, i, &measured_state));
  }
  for (int i = 0; i < 54; ++i) {
    const uint64_t now = (uint64_t)i * 150000;
    CHECK(k80_controller_take_state(&measured, now, &measured_endpoint, &measured_taken));
    const int expected = (i % 3) * 6 + (i / 3) % 6;
    CHECK(measured_endpoint == expected && measured_taken.hue == expected * 20);
    k80_controller_started(&measured, now);
    CHECK(measured.slot_started_us[expected / 6] == now);
  }
  CHECK(!k80_controller_take_state(&measured, 9000000, &measured_endpoint, &measured_taken));
  measured_state.mode = 2;
  measured_state.level = 1;
  measured_state.effect = 9;
  CHECK(k80_controller_request_state(&measured, 17, &measured_state));
  CHECK(k80_controller_take_state(&measured, 9000000, &measured_endpoint, &measured_taken));
  CHECK(measured_endpoint == 17 && measured_taken.mode == 2 && measured_taken.effect == 9);
  CHECK(k80_controller_request_state(&measured, 17, &measured_state));
  CHECK(measured_pending[17].remaining == 2);
  measured_state.effect = 8;
  CHECK(k80_controller_request_state(&measured, 17, &measured_state));
  CHECK(measured_pending[17].remaining == 3 && measured_pending[17].state.effect == 8);
  const k80_controller_pending measured_before = measured_pending[17];
  measured_state.hue = INT_MAX;
  CHECK(!k80_controller_request_state(&measured, 17, &measured_state));
  CHECK(memcmp(&measured_before, &measured_pending[17], sizeof(measured_before)) == 0);
  measured_state.hue = 300;
  measured_state.level = 0;
  CHECK(k80_controller_request_state(&measured, 17, &measured_state));
  CHECK(measured_pending[17].remaining == 3 && measured_pending[17].state.level == 0 &&
        measured_pending[16].remaining == 0);
  k80_controller_halt(&measured);
  for (int slot = 4; slot <= 47; ++slot) {
    k80_controller_queue invalid_queue = {0};
    const k80_controller_endpoint invalid_endpoint = {slot, 0, 4};
    CHECK(!k80_controller_configure(&invalid_queue, measured_pending, &invalid_endpoint, 1));
  }
  k80_controller_queue fourth = {0};
  k80_controller_pending fourth_pending;
  const k80_controller_endpoint fourth_endpoint = {3, 5, 4};
  CHECK(k80_controller_configure(&fourth, &fourth_pending, &fourth_endpoint, 1));
  CHECK(k80_controller_begin_drain(&fourth));
  const k80_control_values fourth_off = {0, 0, 1, 0, 100, 1};
  CHECK(k80_controller_request_state(&fourth, 0, &fourth_off));
  CHECK(!k80_controller_take_state(&fourth, 0, &measured_endpoint, &measured_taken));
  k80_controller_end_drain(&fourth);
  fourth.armed = 1;
  CHECK(!k80_controller_take_state(&fourth, 0, &measured_endpoint, &measured_taken));
  for (int mode = 0; mode <= 2; ++mode) {
    for (int effect = 1; effect <= 9; ++effect) {
      const k80_control_values command = {mode, 1, 1, 0, 100, effect};
      CHECK(k80_controller_request_state(&fourth, 0, &command));
      CHECK(k80_controller_take_state(&fourth, 0, &measured_endpoint, &measured_taken));
      CHECK(measured_taken.mode == mode && measured_taken.level == 1 &&
            measured_taken.hue == 0 && measured_taken.effect == effect);
      uint8_t fourth_body[12];
      CHECK(k80_controller_build_state_packet(4, 3, 5, &measured_taken, fourth_body) == 0);
      CHECK(fourth_body[1] == 5 && fourth_body[2] == mode && fourth_body[3] == 1);
      CHECK(k80_controller_request_state(&fourth, 0, &fourth_off));
      CHECK(k80_controller_take_state(&fourth, 0, &measured_endpoint, &measured_taken));
      CHECK(k80_controller_build_state_packet(4, 3, 5, &measured_taken, fourth_body) == 0);
      CHECK(fourth_body[1] == 5 && fourth_body[2] == 0 && fourth_body[3] == 0 && fourth_body[4] == 1);
    }
  }
  // Raw codec capability must not admit synthetic tuples to the live queue.
  for (int slot = 0; slot < 48; ++slot) {
    for (int group = 0; group < 6; ++group) {
      if (slot == 0 && group == 0) continue;
      k80_controller_queue rejected_native = {0};
      k80_controller_pending untouched, snapshot;
      memset(&untouched, 0xA5, sizeof(untouched));
      snapshot = untouched;
      const k80_controller_endpoint tuple = {slot, group, K80_CONTROLLER_PROFILE_NATIVE};
      CHECK(!k80_controller_configure(&rejected_native, &untouched, &tuple, 1));
      CHECK(!rejected_native.pending && !rejected_native.count &&
            memcmp(&untouched, &snapshot, sizeof(untouched)) == 0);
      // Host-only injected storage checks the request admission boundary too.
      untouched = (k80_controller_pending) {0};
      untouched.slot = (uint8_t)slot;
      untouched.group = (uint8_t)group;
      untouched.profile = K80_CONTROLLER_PROFILE_NATIVE;
      snapshot = untouched;
      rejected_native.pending = &untouched;
      rejected_native.count = 1;
      rejected_native.armed = 1;
      const k80_control_values state = {2, 1, 1, 0, 100, 9};
      CHECK(!k80_controller_request_state(&rejected_native, 0, &state));
      CHECK(!k80_controller_request_brightness(&rejected_native, 0, .01f));
      CHECK(memcmp(&untouched, &snapshot, sizeof(untouched)) == 0);
    }
  }
  k80_controller_queue native = {0};
  k80_controller_pending native_pending[2];
  const k80_controller_endpoint native_endpoints[] = {
    {0, 0, K80_CONTROLLER_PROFILE_NATIVE},
    {2, 1, K80_CONTROLLER_PROFILE_BRIGHTNESS}
  };
  CHECK(k80_controller_configure(&native, native_pending, native_endpoints, 2));
  k80_control_values desired = {1, 1, 1, 300, 50, 1}, taken;
  int native_endpoint = -1;
  CHECK(!k80_controller_request_state(&native, 0, &desired));
  CHECK(k80_controller_begin_drain(&native));
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(k80_controller_request_brightness(&native, 0, 1.0f / 255.0f));
  CHECK(native_pending[0].state.level == 1);
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(!k80_controller_take_state(&native, 0, &native_endpoint, &taken));
  k80_controller_end_drain(&native);
  native.armed = 1;
  CHECK(!k80_controller_take_state(&native, 0, &native_endpoint, &taken));
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(k80_controller_take_state(&native, 0, &native_endpoint, &taken));
  CHECK(native_endpoint == 0 && taken.mode == 1 && taken.level == 1 &&
        taken.hue == 300 && taken.saturation == 50);
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(native_pending[0].remaining == 2);
  desired.hue = 120;
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(native_pending[0].remaining == 3 && native_pending[0].state.hue == 120);
  desired.saturation = 0;
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(native_pending[0].state.saturation == 0);
  desired.mode = 0;
  desired.ct_index = 74;
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(native_pending[0].state.ct_index == 74 && native_pending[0].state.mode == 0);
  desired.mode = 2;
  desired.effect = 9;
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(native_pending[0].remaining == 3 && native_pending[0].state.mode == 2);
  desired.effect = 8;
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(native_pending[0].state.effect == 8);
  CHECK(k80_controller_request_brightness(&native, 0, .5f));
  CHECK(native_pending[0].state.level == 50 &&
        native_pending[0].state.mode == 2 && native_pending[0].state.effect == 8);
  k80_controller_pending before = native_pending[0];
  desired.hue = 361;
  CHECK(!k80_controller_request_state(&native, 0, &desired));
  CHECK(memcmp(&before, &native_pending[0], sizeof(before)) == 0);
  desired.hue = 300;
  CHECK(!k80_controller_request_state(&native, 1, &desired));
  CHECK(!k80_controller_request_state(&native, -1, &desired));
  CHECK(!k80_controller_request_state(&native, 2, &desired));
  CHECK(!k80_controller_request_state(&native, 0, NULL));
  CHECK(k80_controller_request_brightness(&native, 1, .01f));
  CHECK(k80_controller_take_state(&native, 0, &native_endpoint, &taken));
  CHECK(native_endpoint == 1 && taken.level == 1);
  desired.level = 0;
  CHECK(k80_controller_request_state(&native, 0, &desired));
  CHECK(native_pending[0].state.level == 0 && native_pending[0].remaining == 3);
  CHECK(k80_controller_take_state(&native, 0, &native_endpoint, &taken));
  CHECK(native_endpoint == 0 && taken.level == 0 && taken.effect == 8);
  uint8_t off[12];
  CHECK(k80_controller_build_state_packet(K80_CONTROLLER_PROFILE_NATIVE,
                                          0, 0, &taken, off) == 0 && off[2] == 0 && off[3] == 0 && off[4] == 1);
  k80_controller_started(&native, 100);
  CHECK(k80_controller_take_state(&native, 150099, &native_endpoint, &taken));
  CHECK(native_endpoint == 1); // A different RF channel is not held by channel 1.
  k80_controller_abort(&native, 1);
  CHECK(!k80_controller_take_state(&native, 150099, &native_endpoint, &taken));
  CHECK(k80_controller_take_state(&native, 150100, &native_endpoint, &taken));
  k80_controller_halt(&native);
  CHECK(!native_pending[0].remaining && !native_pending[1].remaining);
  CHECK(!k80_controller_request_state(&native, 0, &desired));
  k80_controller_queue captured = {0};
  k80_controller_pending captured_storage[2], sentinel[2];
  memset(captured_storage, 0xA5, sizeof(captured_storage));
  memcpy(sentinel, captured_storage, sizeof(sentinel));
  k80_controller_endpoint captured_endpoints[] = {
    {2, 1, K80_CONTROLLER_PROFILE_BRIGHTNESS},
    {0, 0, K80_CONTROLLER_PROFILE_CCT_2700}
  };
  captured_endpoints[1].slot = 2;
  CHECK(!k80_controller_configure(&captured, captured_storage, captured_endpoints, 2));
  CHECK(!captured.pending && !captured.count &&
        memcmp(captured_storage, sentinel, sizeof(sentinel)) == 0);
  captured_endpoints[1].slot = 0;
  captured_endpoints[1].group = 1;
  CHECK(!k80_controller_configure(&captured, captured_storage, captured_endpoints, 2));
  CHECK(!captured.pending && !captured.count &&
        memcmp(captured_storage, sentinel, sizeof(sentinel)) == 0);
  captured_endpoints[1].group = 0;
  CHECK(k80_controller_configure(&captured, captured_storage, captured_endpoints, 2));
  CHECK(captured_storage[1].slot == 0 && captured_storage[0].slot == 2);
  captured.armed = 1;
  CHECK(k80_controller_request_brightness(&captured, 1, .01f));
  CHECK(!k80_controller_profile_supports_field(captured.pending[1].profile, K80_CONTROLLER_FIELD_RGB));
  CHECK(!k80_controller_profile_supports_field(captured.pending[1].profile,
        K80_CONTROLLER_FIELD_COLOR_TEMPERATURE));
  CHECK(captured_storage[1].remaining == 3 && captured_storage[1].state.level == 1 &&
        !captured_storage[0].remaining);
  k80_controller_queue q = {0};
  k80_controller_pending storage[6];
  const k80_controller_endpoint defaults[] = {
    {2, 0, K80_CONTROLLER_PROFILE_BRIGHTNESS},
    {2, 1, K80_CONTROLLER_PROFILE_BRIGHTNESS},
    {2, 2, K80_CONTROLLER_PROFILE_BRIGHTNESS},
    {2, 3, K80_CONTROLLER_PROFILE_BRIGHTNESS},
    {2, 4, K80_CONTROLLER_PROFILE_BRIGHTNESS},
    {2, 5, K80_CONTROLLER_PROFILE_BRIGHTNESS}
  };
  CHECK(k80_controller_configure(&q, storage, defaults, 6));
  CHECK(storage[0].profile == K80_CONTROLLER_PROFILE_BRIGHTNESS);
  CHECK(k80_controller_profile_supports_field(
          K80_CONTROLLER_PROFILE_BRIGHTNESS,
          K80_CONTROLLER_FIELD_BRIGHTNESS));
  CHECK(!k80_controller_profile_supports_field(
          K80_CONTROLLER_PROFILE_BRIGHTNESS,
          K80_CONTROLLER_FIELD_RGB));
  CHECK(!k80_controller_profile_supports_field(
          K80_CONTROLLER_PROFILE_BRIGHTNESS, 1U << 31));
  int endpoint = -1, level = -1;
  CHECK(k80_controller_begin_drain(&q));
  CHECK(q.draining && !q.armed);
  CHECK(k80_controller_request_brightness(&q, 3, 0.42f));
  CHECK(q.pending[3].state.level == 42 && q.pending[3].remaining == 3);
  CHECK(!k80_controller_profile_supports_field(q.pending[3].profile, K80_CONTROLLER_FIELD_RGB));
  CHECK(!k80_controller_take(&q, 0, &endpoint, &level));
  k80_controller_end_drain(&q);
  CHECK(!q.draining && !q.armed && q.pending[3].remaining == 0);
  CHECK(!k80_controller_take(&q, 0, &endpoint, &level));
  CHECK(!k80_controller_request_brightness(&q, 3, 0));
  CHECK(!k80_controller_take(&q, 0, &endpoint, &level));
  q.armed = 1;
  CHECK(!k80_controller_take(&q, 0, &endpoint, &level));
  CHECK(!k80_controller_profile_supports_field(q.pending[3].profile, K80_CONTROLLER_FIELD_RGB));
  CHECK(!k80_controller_profile_supports_field(
          q.pending[3].profile, K80_CONTROLLER_FIELD_COLOR_TEMPERATURE));
  CHECK(!k80_controller_profile_supports_field(q.pending[3].profile, 1U << 31));
  CHECK(!k80_controller_request_brightness(&q, -1, 1));
  CHECK(!k80_controller_request_brightness(&q, 6, 1));
  CHECK(!k80_controller_request_brightness(&q, 3, NAN));
  CHECK(!k80_controller_request_brightness(&q, 3, INFINITY));
  CHECK(!k80_controller_request_brightness(&q, 3, -INFINITY));
  CHECK(k80_controller_request_brightness(&q, 3, 0));
  CHECK(k80_controller_take(&q, 0, &endpoint, &level) && endpoint == 3 && level == 0);
  k80_controller_started(&q, 100);
  CHECK(!k80_controller_take(&q, 150099, &endpoint, &level));
  CHECK(k80_controller_request_brightness(&q, 3, 0));
  CHECK(q.pending[3].remaining == 2); // Pending duplicate must not replenish.
  CHECK(k80_controller_take(&q, 150100, &endpoint, &level));
  k80_controller_started(&q, 150100);
  CHECK(k80_controller_request_brightness(&q, 3, 1)); // Supersede unsent old copies.
  CHECK(!k80_controller_take(&q, 150101, &endpoint, &level));
  for (int i = 0; i < 3; ++i) {
    uint64_t t = 300100U + (uint64_t)i * 150000U;
    CHECK(k80_controller_take(&q, t, &endpoint, &level) && level == 100);
    k80_controller_started(&q, t);
    CHECK(!k80_controller_take(&q, t, &endpoint, &level));
  }
  CHECK(!k80_controller_take(&q, 1000000, &endpoint, &level));
  CHECK(k80_controller_request_brightness(&q, 3, 0));
  CHECK(k80_controller_take(&q, 1000000, &endpoint, &level) && level == 0);
  k80_controller_started(&q, 1000000);
  k80_controller_abort(&q, 3);
  CHECK(!k80_controller_take(&q, 2000000, &endpoint, &level));
  for (int i = 0; i < 6; ++i) CHECK(k80_controller_request_brightness(&q, i, i / 5.0f));
  for (int i = 0; i < 18; ++i) {
    uint64_t t = 2000000U + (uint64_t)i * 150000U;
    CHECK(k80_controller_take(&q, t, &endpoint, &level));
    CHECK(endpoint == (4 + i) % 6 && level == endpoint * 20);
    k80_controller_started(&q, t);
  }
  CHECK(!k80_controller_take(&q, 9000000, &endpoint, &level));
  CHECK(k80_controller_request_brightness(&q, 0, -1));
  CHECK(k80_controller_request_brightness(&q, 1, 2));
  CHECK(k80_controller_request_brightness(&q, 2, 0.505f));
  CHECK(q.pending[0].state.level == 0 && q.pending[1].state.level == 100 && q.pending[2].state.level == 51);
  k80_controller_halt(&q);
  CHECK(!k80_controller_request_brightness(&q, 0, 1));
  CHECK(!k80_controller_take(&q, 10000000, &endpoint, &level));
  for (int i = 0; i < 6; ++i) CHECK(q.pending[i].remaining == 0);
  q.armed = 1;
  CHECK(k80_controller_request_brightness(&q, 0, 0));
  for (int i = 0; i < 3; ++i) {
    uint64_t t = 20000000U + (uint64_t)i * 150000U;
    CHECK(k80_controller_take(&q, t, &endpoint, &level) && endpoint == 0 && level == 0);
    k80_controller_started(&q, t);
  }
  CHECK(k80_controller_request_brightness(&q, 0, 0)); // Same OFF after completed train is new work.
  CHECK(q.pending[0].remaining == 3);
  for (int i = 0; i < 1000; ++i) {
    CHECK(k80_controller_request_brightness(&q, i % 6, (i % 101) / 100.0f));
    CHECK(!k80_controller_take(&q, 20300001, &endpoint, &level));
  }
  for (int i = 0; i < 6; ++i) CHECK(q.pending[i].remaining <= 3);
  CHECK(k80_controller_take(&q, 90000000, &endpoint, &level)); // Late service is one packet.
  k80_controller_started(&q, 90000000);
  CHECK(!k80_controller_take(&q, 90000001, &endpoint, &level));
  k80_controller_queue many = {0};
  k80_controller_pending pending[48];
  k80_controller_endpoint endpoints[48];
  for (int i = 0; i < 48; ++i)
    endpoints[i] = (k80_controller_endpoint) {i, 3, K80_CONTROLLER_PROFILE_BRIGHTNESS};
  CHECK(k80_controller_configure(&many, pending, endpoints, 48));
  CHECK(many.count == 48);
  CHECK(pending[0].profile == K80_CONTROLLER_PROFILE_BRIGHTNESS);
  endpoints[0].slot = 47;
  endpoints[0].group = 5;
  endpoints[0].profile = K80_CONTROLLER_PROFILE_NONE;
  CHECK(pending[0].slot == 0 && pending[0].group == 3 &&
        pending[0].profile == K80_CONTROLLER_PROFILE_BRIGHTNESS);
  endpoints[7] = (k80_controller_endpoint) {2, 0, K80_CONTROLLER_PROFILE_BRIGHTNESS};
  CHECK(!k80_controller_configure(&many, pending, endpoints, 48));
  many.armed = 1;
  CHECK(!k80_controller_configure(&many, pending, endpoints, 48));
  CHECK(k80_controller_request_brightness(&many, 47, 0));
  CHECK(k80_controller_take(&many, 0, &endpoint, &level) && endpoint == 47 && level == 0);
  k80_controller_abort(&many, 47);
  CHECK(!k80_controller_request_brightness(&many, -1, 0));
  CHECK(!k80_controller_request_brightness(&many, 48, 0));
  CHECK(!k80_controller_request_brightness(&many, 255, 0));
  CHECK(!k80_controller_request_brightness(&many, 256, 0));
  CHECK(!k80_controller_request_brightness(&many, INT_MAX, 0));
  CHECK(!k80_controller_request_brightness(&many, INT_MIN, 0));
  for (int i = 0; i < 48; ++i) CHECK(k80_controller_request_brightness(&many, i, i / 100.0f));
  for (int i = 0; i < 144; ++i) {
    uint64_t t = (uint64_t)i * 150000U;
    CHECK(k80_controller_take(&many, t, &endpoint, &level));
    CHECK(endpoint == i % 48 && level == i % 48);
    CHECK(pending[endpoint].slot == endpoint && pending[endpoint].group == 3);
    k80_controller_started(&many, t);
    CHECK(many.slot_started_us[endpoint] == t);
  }
  CHECK(!k80_controller_take(&many, 99999999, &endpoint, &level));
  CHECK(k80_controller_request_brightness(&many, 7, 0));
  CHECK(k80_controller_request_brightness(&many, 8, 1));
  CHECK(k80_controller_request_brightness(&many, 7, .5f));
  CHECK(pending[7].state.level == 50 && pending[8].state.level == 100 && pending[7].slot == 7);
  k80_controller_abort(&many, 7);
  CHECK(!pending[7].remaining && pending[8].remaining == 3);
  k80_controller_halt(&many);
  CHECK(!k80_controller_configure(&many, pending, endpoints, 48));
  for (int i = 0; i < 48; ++i) CHECK(!pending[i].remaining);
  const int invalid[] = {-1, 48, 255, 256, INT_MIN, INT_MAX};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    k80_controller_queue rejected = {0};
    k80_controller_endpoint bad = {invalid[i], 0, K80_CONTROLLER_PROFILE_BRIGHTNESS};
    CHECK(!k80_controller_configure(&rejected, pending, &bad, 1));
    bad.slot = 0;
    bad.group = invalid[i];
    CHECK(!k80_controller_configure(&rejected, pending, &bad, 1));
    CHECK(!rejected.pending && !rejected.count);
  }
  k80_controller_queue rejected = {0};
  const k80_controller_endpoint unsupported_profile = {2, 0,
                                                       K80_CONTROLLER_PROFILE_NONE
                                                      };
  CHECK(!k80_controller_configure(&rejected, pending, &unsupported_profile, 1));
  CHECK(!rejected.pending && !rejected.count);
  const k80_controller_endpoint duplicate[] = {
    {2, 3, K80_CONTROLLER_PROFILE_BRIGHTNESS},
    {2, 3, K80_CONTROLLER_PROFILE_BRIGHTNESS}
  };
  CHECK(!k80_controller_configure(&rejected, pending, duplicate, 2));
  CHECK(!k80_controller_configure(&rejected, pending, defaults, 0));
  CHECK(!k80_controller_configure(&rejected, pending, defaults, 289));
  for (int g = 6; g <= 7; ++g) {
    const k80_controller_endpoint bad = {2, g, K80_CONTROLLER_PROFILE_BRIGHTNESS};
    CHECK(!k80_controller_configure(&rejected, pending, &bad, 1));
  }
  k80_controller_pending all_pending[288];
  k80_controller_endpoint all_endpoints[288];
  for (int i = 0; i < 288; ++i)
    all_endpoints[i] = (k80_controller_endpoint) {
    i / 6, i % 6,
    K80_CONTROLLER_PROFILE_BRIGHTNESS
  };
  CHECK(k80_controller_configure(&rejected, all_pending, all_endpoints, 288));
  rejected.armed = 1;
  CHECK(k80_controller_request_brightness(&rejected, 256, 0));
  CHECK(k80_controller_request_brightness(&rejected, 287, 1));
  CHECK(k80_controller_take(&rejected, 0, &endpoint, &level) && endpoint == 256 && level == 0);
  k80_controller_started(&rejected, 0);
  CHECK(k80_controller_take(&rejected, 150000, &endpoint, &level) && endpoint == 287 && level == 100);
  CHECK(all_pending[endpoint].slot == 47 && all_pending[endpoint].group == 5);
  puts("controller scheduler: mute, finite, latest-wins, repeat, spacing, fairness, halt OK");
}
