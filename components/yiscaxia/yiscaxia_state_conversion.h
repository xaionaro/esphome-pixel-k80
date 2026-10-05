#pragma once

#include <algorithm>
#include <cmath>
#include "k80_controller_protocol.h"

namespace esphome::yiscaxia {

inline bool unit_value_valid(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }

inline bool brightness_to_level(float value, int *level) {
  if (!level || !unit_value_valid(value)) return false;
  *level = k80_quantize_brightness(value);
  return true;
}

// Use raw normalized RGB; intensity is already the state/brightness product.
inline bool rgb_to_state(float red, float green, float blue, float intensity,
                         const k80_control_values &previous, k80_control_values *output) {
  if (!output || !unit_value_valid(red) || !unit_value_valid(green) || !unit_value_valid(blue)) return false;
  k80_control_values next = previous;
  if (!brightness_to_level(intensity, &next.level)) return false;
  const float maximum = std::max({red, green, blue});
  const float delta = maximum - std::min({red, green, blue});
  next.mode = K80_MODE_HSI;
  next.saturation = maximum == 0 ? 0 : static_cast<int>(std::floor(
                      delta / maximum * static_cast<float>(K80_SATURATION_MAX) + .5f));
  if (delta != 0) {
    float hue;
    if (maximum == red) hue = 60 * (green - blue) / delta;
    else if (maximum == green) hue = 60 * (blue - red) / delta + 120;
    else hue = 60 * (red - green) / delta + 240;
    if (hue < 0) hue += 360;
    next.hue = static_cast<int>(std::floor(hue + .5f));
  }
  *output = next;
  return true;
}

inline bool ct_to_state(float mireds, float intensity,
                        const k80_control_values &previous, k80_control_values *output) {
  if (!output || !std::isfinite(mireds) || mireds < 1000000.0f / static_cast<float>(K80_CT_KELVIN_MAX) ||
      mireds > 1000000.0f / static_cast<float>(K80_CT_KELVIN_MIN)) return false;
  k80_control_values next = previous;
  if (!brightness_to_level(intensity, &next.level)) return false;
  next.mode = K80_MODE_CCT;
  next.ct_index = static_cast<int>(std::floor(
                                     (1000000.0f / mireds - static_cast<float>(K80_CT_KELVIN_MIN)) /
                                     static_cast<float>(K80_CT_KELVIN_STEP) + .5f));
  *output = next;
  return true;
}

}  // namespace esphome::yiscaxia
