#include "yiscaxia_state_conversion.h"
#include "k80_controller_scheduler.h"
#include "test_check.h"
#include <limits>

int main() {
  using namespace esphome::yiscaxia;
  int level = -1;
  CHECK(brightness_to_level(1.0f / 255.0f, &level) && level == 1);
  CHECK(brightness_to_level(0, &level) && level == 0);
  CHECK(brightness_to_level(1, &level) && level == 100);
  CHECK(!brightness_to_level(std::numeric_limits<float>::quiet_NaN(), &level) && level == 100);
  CHECK(!brightness_to_level(-.1f, &level));
  k80_control_values previous = {K80_MODE_HSI, 1, 1, 120, 100, 1}, result;
  CHECK(rgb_to_state(1, 0, 0, .01f, previous, &result));
  CHECK(result.mode == 1 && result.level == 1 && result.hue == 0 && result.saturation == 100);
  CHECK(rgb_to_state(0, 1, 0, .01f, previous, &result) && result.hue == 120);
  CHECK(rgb_to_state(1, 0, 1, .01f, previous, &result) && result.hue == 300);
  CHECK(rgb_to_state(.5f, .25f, .5f, .01f, previous, &result) &&
        result.hue == 300 && result.saturation == 50 && result.level == 1);
  CHECK(rgb_to_state(1, 1, 1, .01f, previous, &result) && result.hue == 120 && result.saturation == 0);
  const auto unchanged = result;
  CHECK(!rgb_to_state(std::numeric_limits<float>::infinity(), 0, 0, .01f, previous, &result));
  CHECK(k80_controller_states_equal(&unchanged, &result));
  for (int kelvin : {2600, 2700, 2800, 10000}) {
    CHECK(ct_to_state(1000000.0f / kelvin, .01f, previous, &result));
    CHECK(result.mode == 0 && result.ct_index == (kelvin - 2600) / 100 && result.level == 1);
  }
  CHECK(!ct_to_state(0, .01f, previous, &result));
  CHECK(!ct_to_state(99, .01f, previous, &result));
  CHECK(!ct_to_state(std::numeric_limits<float>::quiet_NaN(), .01f, previous, &result));
  return 0;
}
