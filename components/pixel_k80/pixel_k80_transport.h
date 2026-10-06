#pragma once

#include "esphome/core/component.h"
#include "pixel_k80_transport_types.h"

namespace esphome::pixel_k80 {
class PixelK80Transport : public Component {
 public:
  virtual bool ready() const = 0;
  virtual uint64_t now_us() const = 0;
  virtual pixel_k80_tx_result transmit(const pixel_k80_tx_packet &packet, int slot) = 0;
};
}  // namespace esphome::pixel_k80
