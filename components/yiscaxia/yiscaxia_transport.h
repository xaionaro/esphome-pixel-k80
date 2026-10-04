#pragma once

#include "esphome/core/component.h"
#include "yiscaxia_transport_types.h"

namespace esphome::yiscaxia {
class YiscaxiaTransport : public Component {
 public:
  virtual bool ready() const = 0;
  virtual uint64_t now_us() const = 0;
  virtual yiscaxia_tx_result transmit(const yiscaxia_tx_packet &packet, int slot) = 0;
};
}  // namespace esphome::yiscaxia
