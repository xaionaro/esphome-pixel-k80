#ifndef K80_POLICY_UNDER_TEST
#define K80_POLICY_UNDER_TEST "k80_controller_scheduler.h"
#endif
#include K80_POLICY_UNDER_TEST
#include "test_check.h"

int main(int argc, char **argv) {
  k80_controller_queue queue = {0};
  k80_controller_pending pending[3] = {0};
  const k80_controller_endpoint endpoints[] = {{0,0,5},{0,1,5},{1,0,5}};
  CHECK(k80_controller_configure(&queue, pending, endpoints, 3));
  queue.armed = 1;
  CHECK(k80_controller_request(&queue, 0, .01f));
  CHECK(k80_controller_request(&queue, 1, .01f));
  CHECK(k80_controller_request(&queue, 2, .01f));
  int endpoint = -1;
  k80_control_values state = {0};
  if (argc == 2 && strcmp(argv[1], "fairness") == 0) goto fairness;
  CHECK(k80_controller_take_state(&queue, 0, &endpoint, &state));
  CHECK(endpoint == 0);
  k80_controller_started(&queue, 0);
  CHECK(k80_controller_take_state(&queue, 1, &endpoint, &state));
  CHECK(endpoint == 2); // A different RF channel need not wait for slot0.
  k80_controller_started(&queue, 1);
  CHECK(!k80_controller_take_state(&queue, 149999, &endpoint, &state));
  CHECK(k80_controller_take_state(&queue, 150000, &endpoint, &state));
  CHECK(endpoint == 1); // A second group shares slot0's clock, not its own.
  k80_controller_started(&queue, 150000);
  CHECK(k80_controller_take_state(&queue, 150001, &endpoint, &state) && endpoint == 2);
  k80_controller_started(&queue, 150001);

  const float invalid[] = {0, -1, 1.5f, NAN, INFINITY, -INFINITY, 65536};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    CHECK(!k80_controller_set_spacing(&queue, invalid[i]));
    CHECK(!k80_controller_set_attempts(&queue, invalid[i]));
  }
  CHECK(!k80_controller_set_attempts(&queue, 256));
  const unsigned remaining = pending[0].remaining;
  CHECK(k80_controller_set_attempts(&queue, 5));
  k80_control_values automatic = pending[1].state;
  automatic.hue = 1;
  CHECK(k80_controller_request_state_attempts(&queue, 1, &automatic, 1));
  CHECK(queue.attempts == 5 && pending[1].remaining == 1);
  automatic.hue = 2;
  CHECK(k80_controller_request_state(&queue, 1, &automatic));
  CHECK(queue.attempts == 5 && pending[1].remaining == 5);
  CHECK(pending[0].remaining == remaining);
  CHECK(k80_controller_request(&queue, 0, .01f) && pending[0].remaining == remaining);
  CHECK(k80_controller_request(&queue, 0, .02f) && pending[0].remaining == 5);
  CHECK(k80_controller_request(&queue, 0, 0) && pending[0].remaining == 5);
  CHECK(k80_controller_set_attempts(&queue, 255));
  CHECK(k80_controller_request(&queue, 0, .03f) && pending[0].remaining == 255);
  CHECK(k80_controller_set_attempts(&queue, 1));
  CHECK(pending[0].remaining == 255);
  CHECK(k80_controller_request(&queue, 0, 0) && pending[0].remaining == 1);
  CHECK(k80_controller_set_spacing(&queue, 65535));
  CHECK(!k80_controller_take_state(&queue, 65534999 + 150000, &endpoint, &state));
  CHECK(k80_controller_set_spacing(&queue, 1));
  CHECK(k80_controller_take_state(&queue, 151000, &endpoint, &state) && endpoint == 0);
  k80_controller_started(&queue, 151000);
  CHECK(k80_controller_set_spacing(&queue, 500));
  pending[1].remaining = pending[2].remaining = 0;
  CHECK(k80_controller_request(&queue, 0, 0));
  CHECK(!k80_controller_take_state(&queue, 650999, &endpoint, &state));
  CHECK(k80_controller_take_state(&queue, 651000, &endpoint, &state));
  k80_controller_started(&queue, 700000); // Actual trigger later than dequeue.
  CHECK(!k80_controller_take_state(&queue, 1199999, &endpoint, &state));
  CHECK(k80_controller_request(&queue, 0, 0));
  CHECK(k80_controller_take_state(&queue, 1200000, &endpoint, &state));
  k80_controller_started(&queue, 1200000);
  const uint64_t clocks = queue.started_slots;
  k80_controller_halt(&queue);
  queue.armed = 1;
  CHECK(queue.started_slots == clocks && queue.slot_started_us[0] == 1200000);
  // Address identity may move, but an RF slot's clock remains shared.
  pending[0].slot = 1;
  CHECK(k80_controller_request(&queue, 0, 0));
  CHECK(k80_controller_take_state(&queue, 1200001, &endpoint, &state));
  k80_controller_started(&queue, 1200001);
  pending[0].slot = 0;
  CHECK(k80_controller_request(&queue, 0, 0));
  CHECK(!k80_controller_take_state(&queue, 1200002, &endpoint, &state));

fairness: ;
  for (int attempts = 0; attempts <= 1; ++attempts) {
  k80_controller_queue fair = {0};
  k80_controller_pending fair_pending[12] = {0};
  k80_controller_endpoint fair_addresses[12];
  for (int i = 0; i < 12; ++i) fair_addresses[i] = (k80_controller_endpoint){i / 6, i % 6, 5};
  CHECK(k80_controller_configure(&fair, fair_pending, fair_addresses, 12));
  fair.armed = 1;
  unsigned seen[12] = {0};
  for (uint64_t now = 0; now < 4000000; now += 1000) {
    if (now % 1000000 == 0) {
      const k80_control_values phase = {1, 1, 1, (int)(now / 1000000), 100, 1};
      for (int i = 0; i < 12; ++i)
        CHECK(k80_controller_request_state_attempts(&fair, i, &phase, attempts));
    }
    if (k80_controller_take_state(&fair, now, &endpoint, &state)) {
      ++seen[endpoint];
      k80_controller_started(&fair, now);
    }
  }
  for (int i = 0; i < 12; ++i) CHECK(seen[i] >= 4);
  CHECK(fair.attempts == 3);
  }
  puts("policy: per-channel live spacing, snapshot total attempts, valid zero clocks, RF retarget and two-level fairness PASS");
  return 0;
}
