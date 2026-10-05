#include "k80_controller_config.h"
#include "test_check.h"
#include <cstring>

int main() {
  k80_controller_queue queue{};
  k80_controller_pending storage[6] {};
  k80_controller_endpoint initial[6];
  for (int i = 0; i < 6; ++i)
    initial[i] = {2, i, K80_CONTROLLER_PROFILE_NATIVE_RAW_FREQUENCY};
  CHECK(k80_controller_configure(&queue, storage, initial, 6));
  CHECK(k80_controller_config::replace_addresses(&queue, "0:0,2:1,2:2,2:3,2:4,4:5"));
  CHECK(k80_controller_config::addresses(queue) == "0:0,2:1,2:2,2:3,2:4,4:5");
  const auto before = queue;
  CHECK(!k80_controller_config::replace_addresses(&queue,
        std::string("0:0,2:1,2:2,2:3,2:4,4:5\0suffix", 30)));
  k80_controller_pending previous[6];
  std::memcpy(previous, storage, sizeof(storage));
  for (const char *bad : {
         "", "0:0", "0:0,2:1,2:2,2:3,2:4,4:5,3:1",
         "0:0,2:1,2:2,2:3,2:4,4:5,", "0:0,2:1,2:2,2:3,2:4,2:4",
         "0:0,2:1,2:2,2:3,2:4,48:5", "0:0,2:1,2:2,2:3,2:4,4:6",
         "0:0,2:1,2:2,2:3,2:4,-1:5", "0:0,2:1,2:2,2:3,2:4,04:5",
         "0:0,2:1,2:2,2:3,2:4,4:5x", "0:0,2:1,2:2,2:3,2:4,4::5"
       }) {
    CHECK(!k80_controller_config::replace_addresses(&queue, bad));
    CHECK(std::memcmp(previous, storage, sizeof(storage)) == 0);
    CHECK(queue.count == before.count && queue.pending == before.pending);
  }
  storage[0].state.level = 10;
  CHECK(k80_controller_config::replace_addresses(&queue, "0:0,2:1,2:2,2:3,2:4,3:5"));
  CHECK(storage[0].state.level == 10 && storage[5].slot == 3);
  CHECK(k80_controller_config::replace_addresses(&queue, "1:0,2:1,2:2,2:3,2:4,4:5"));
  CHECK(storage[0].state.level == 10 && storage[0].remaining == 0);
  CHECK(k80_controller_config::replace_addresses(&queue, "0:0,2:1,2:2,2:3,2:4,3:5"));
  storage[0].state.level = 0;
  storage[0].remaining = 1;
  storage[1].remaining = 2;
  CHECK(k80_controller_config::replace_addresses(&queue, "1:0,2:1,2:2,2:3,2:4,4:5"));
  CHECK(storage[0].remaining == 0 && storage[1].remaining == 2);
  CHECK(k80_controller_config::replace_addresses(&queue, "2:1,0:0,2:2,2:3,2:4,4:5"));
  storage[0].profile = K80_CONTROLLER_PROFILE_NATIVE;
  CHECK(!k80_controller_config::replace_addresses(&queue, "1:0,0:0,2:2,2:3,2:4,4:5"));
  CHECK(storage[0].slot == 2 && storage[0].group == 1);
  CHECK(!k80_controller_config::replace_addresses(nullptr, "0:0"));
  storage[0].profile = K80_CONTROLLER_PROFILE_NATIVE_RAW_FREQUENCY;
  CHECK(k80_controller_config::replace_addresses(&queue, "-,-,-,-,-,-"));
  CHECK(k80_controller_config::addresses(queue) == "-,-,-,-,-,-");
  queue.armed = 1;
  CHECK(!k80_controller_request_brightness(&queue, 0, 0.5f));
  const k80_control_values on{K80_MODE_HSI, 50, 1, 90, 100, 1};
  CHECK(!k80_controller_request_state(&queue, 0, &on));
  CHECK(storage[0].remaining == 0 && storage[0].state.level == 0);
  CHECK(k80_controller_config::replace_addresses(&queue, "0:0,-,-,-,-,-"));
  CHECK(k80_controller_request_state(&queue, 0, &on));
  CHECK(storage[0].remaining == 3 && storage[0].state.level == 50);
  storage[0].remaining = 0;
  storage[0].state.level = 0;
  CHECK(k80_controller_config::replace_positions(&queue, "1A,2B,2D"));
  CHECK(k80_controller_config::positions(queue) == "1A,2B,2D");
  CHECK(storage[0].slot == 0 && storage[1].slot == 1 && storage[2].group == 3);
  CHECK(!storage[3].enabled && !storage[4].enabled && !storage[5].enabled);
  for (const char *bad : {
         "0A", "49A", "1G", "1a", "01A", "1:A", "1A,", ",1A",
         "1A,1A", "1A,1B,1C,1D,1E,1F,2A", "1 A"
       }) {
    CHECK(!k80_controller_config::replace_positions(&queue, bad));
    CHECK(k80_controller_config::positions(queue) == "1A,2B,2D");
  }
  CHECK(!k80_controller_config::replace_positions(&queue, std::string("1A\0suffix", 9)));
  k80_controller_config::Positions proposed;
  CHECK(k80_controller_config::prepare_positions(&queue, "1A,2B,2D", &proposed));
  const auto invalid = k80_controller_config::prepare_positions(&queue, "1A,1A", &proposed);
  CHECK(!invalid && invalid.error == "Position 2: duplicate address already used by position 1");
  CHECK(proposed.text() == "1A,2B,2D");
  std::memcpy(previous, storage, sizeof(storage));
  CHECK(k80_controller_config::prepare_positions(&queue, "48F,-,1A,-", &proposed));
  CHECK(proposed.text() == "48F,-,1A" && std::memcmp(previous, storage, sizeof(storage)) == 0);
  proposed.endpoints[0].profile = K80_CONTROLLER_PROFILE_NATIVE;
  const auto changed_profile = k80_controller_config::apply_positions(&queue, proposed);
  CHECK(!changed_profile && changed_profile.error == "Position 1: address remapping cannot change the profile");
  CHECK(std::memcmp(previous, storage, sizeof(storage)) == 0);
  proposed.endpoints.pop_back();
  CHECK(!k80_controller_config::apply_positions(&queue, proposed));
  CHECK(std::memcmp(previous, storage, sizeof(storage)) == 0);
  CHECK(k80_controller_config::replace_positions(&queue, "48F,-,1A,-"));
  CHECK(k80_controller_config::positions(queue) == "48F,-,1A");
  CHECK(k80_controller_config::replace_positions(&queue, ""));
  CHECK(k80_controller_config::positions(queue).empty());
  k80_controller_pending pool[64] {};
  k80_controller_endpoint pool_initial[64];
  queue = {};
  for (int i = 0; i < 64; ++i) pool_initial[i] = {i / 6, i % 6, 5};
  CHECK(k80_controller_configure(&queue, pool, pool_initial, 64));
  std::string maximum;
  for (int i = 0; i < 64; ++i) {
    if (i) maximum += ',';
    maximum += std::to_string(38 + i / 6);
    maximum += static_cast<char>('A' + i % 6);
  }
  CHECK(maximum.size() == 255);
  CHECK(k80_controller_config::replace_positions(&queue, maximum));
  CHECK(k80_controller_config::positions(queue) == maximum);
  CHECK(!k80_controller_config::replace_positions(&queue, maximum + ",1A"));
  pool[63].remaining = 1;
  CHECK(k80_controller_config::replace_positions(&queue, "1A"));
  CHECK(k80_controller_config::positions(queue) == "1A" && pool[63].remaining == 0);
  CHECK(k80_controller_config::replace_positions(&queue, "1A,1B,1C,1D,1E,1F,2A"));
  CHECK(pool[6].enabled && !pool[7].enabled && !pool[63].enabled);
  return 0;
}
