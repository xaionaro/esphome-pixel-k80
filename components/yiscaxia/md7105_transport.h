#pragma once

#include "yiscaxia_transport.h"
#include "md7105_tx_packet_protocol.h"
#include "esphome/components/spi/spi.h"

namespace esphome::yiscaxia {

class Md7105Transport : public YiscaxiaTransport,
                       public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                             spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return 801.0f; }
  bool ready() const override { return this->ready_; }
  uint64_t now_us() const override;
  yiscaxia_tx_result transmit(const yiscaxia_tx_packet &packet, int slot) override;

 protected:
  static int transfer_(void *context, const uint8_t *tx, uint8_t *rx, size_t length);
  static uint64_t now_(void *context);
  static void delay_(void *context, unsigned microseconds);
  bool calibrate_();
  bool wait_calibration_(uint8_t expected);
  bool ready_{false};
  a7105_probe_bus_t bus_{this, transfer_};
};

}  // namespace esphome::yiscaxia
