#include "md7105_transport.h"
#include "k80_controller_protocol.h"
#include "sdk_runtime.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); std::abort(); } } while (0)

// Only the SPI hardware boundary is replaced. The installed SPI declarations,
// actual transport setup/calibration and pure packet transaction execute below.
namespace esphome::spi {
GPIOPin *const NullPin::NULL_PIN = nullptr;
SPIDelegate *const SPIDelegate::NULL_DELEGATE = nullptr;
bool SPIDelegate::is_ready() { return true; }
SPIDelegate *SPIComponent::register_device(SPIClient *, SPIMode, SPIBitOrder, uint32_t,
                                         GPIOPin *, bool, bool) { std::abort(); }
void SPIComponent::unregister_device(SPIClient *) { std::abort(); }
}  // namespace esphome::spi

namespace {
using esphome::yiscaxia::Md7105Transport;
struct Radio : esphome::spi::SPIDelegate {
  std::array<uint8_t, 64> registers{};
  std::array<uint8_t, 4> id{};
  std::vector<std::vector<uint8_t>> operations;
  bool available = true, bad_id = false, stuck_calibration = false;
  bool unexpected_pending = false, never_active = false, stuck_tx = false;
  unsigned if_reads = 0, bank_reads = 0, calibration_reads = 0;
  unsigned calibration_stage = 0, fault_calibration_stage = 1;
  unsigned tx_count = 0, mode_reads = 0, begin_count = 0, end_count = 0;
  unsigned lose_ready_after = 0;
  unsigned bank_fail_at = 1;
  uint8_t if_flags = 0, bank_flags = 0;

  bool is_ready() override { return available; }
  void begin_transaction() override { ++begin_count; }
  void end_transaction() override { ++end_count; }
  uint8_t transfer(uint8_t) override { std::abort(); }
  void transfer(const uint8_t *tx, uint8_t *rx, size_t length) override {
    CHECK(length > 0 && available);
    operations.emplace_back(tx, tx + length);
    std::memset(rx, 0, length);
    // Register reads consume deterministic time, including calibration's busy
    // loop (which deliberately has no delay call in production).
    sdk_test::set_time_us(sdk_test::clock_us + 100);
    if (length == 5 && tx[0] == A7105_REG_ID_DATA) {
      std::memcpy(id.data(), tx + 1, 4);
    } else if (length == 5 && tx[0] == (A7105_REG_ID_DATA | 0x40)) {
      std::memcpy(rx + 1, id.data(), 4);
      if (bad_id) rx[4] ^= 1;
    } else if (length == 2 && (tx[0] & 0x40)) {
      const uint8_t reg = tx[0] & 0x3F;
      rx[1] = registers[reg];
      if (reg == A7105_REG_CALIBRATION) {
        ++calibration_reads;
        const bool fault_stage = calibration_stage == fault_calibration_stage;
        rx[1] = unexpected_pending && fault_stage ? 7 : stuck_calibration && fault_stage ? registers[reg] : 0;
      } else if (reg == A7105_REG_IF_CALIB) {
        ++if_reads; rx[1] = if_flags;
      } else if (reg == A7105_REG_VCO_BANK) {
        ++bank_reads; rx[1] = bank_reads == bank_fail_at ? bank_flags : 0;
      } else if (reg == A7105_REG_MODE) {
        ++mode_reads;
        rx[1] = never_active ? 0 : stuck_tx || mode_reads < 4 ? 3 : 0;
      }
    } else if (length == 2) {
      registers[tx[0]] = tx[1];
      if (tx[0] == A7105_REG_CALIBRATION && tx[1] != 0) ++calibration_stage;
    } else if (length == 1 && tx[0] == 0xD0) {
      ++tx_count;
    }
    if (lose_ready_after && operations.size() == lose_ready_after) available = false;
  }
  bool contains(std::initializer_list<uint8_t> bytes) const {
    return std::find(operations.begin(), operations.end(), std::vector<uint8_t>(bytes)) != operations.end();
  }
};
class Transport : public Md7105Transport {
 public:
  Radio radio;
  unsigned setup_count = 0;
  void spi_setup() override { ++setup_count; this->delegate_ = &radio; }
};
void failed_setup(Transport &transport) {
  transport.setup();
  CHECK(!transport.ready() && transport.is_failed());
  CHECK(transport.radio.tx_count == 0);
  if (transport.radio.available) {
    CHECK(transport.radio.operations.back() == std::vector<uint8_t>{A7105_CMD_STANDBY});
  }
  yiscaxia_tx_packet packet{};
  const auto count = transport.radio.operations.size();
  CHECK(transport.transmit(packet, 0).error == -1);
  CHECK(transport.radio.operations.size() == count);
}
}  // namespace

int main() {
  sdk_test::set_time_us(0);
  Transport transport;
  yiscaxia_tx_packet packet{};
  CHECK(k80_controller_build_packet(3, 0, packet.bytes) == 0);
  CHECK(transport.transmit(packet, 0).error == -1);
  CHECK(transport.radio.operations.empty());
  transport.setup();
  CHECK(transport.setup_count == 1 && transport.ready() && !transport.is_failed());
  auto &radio = transport.radio;
  CHECK(radio.operations.front() == (std::vector<uint8_t>{0, 0}));
  CHECK(radio.contains({A7105_REG_GIO1, A7105_GIO1_4WIRE_DATA_OUT}));
  CHECK(radio.contains({A7105_REG_GIO2, A7105_GIO2_FSYNC_STATUS}));
  CHECK(radio.contains({A7105_REG_ID_DATA, 0xA5, 0x5A, 0xB9, 0x46}));
  CHECK(radio.contains({uint8_t(A7105_REG_ID_DATA | 0x40), 0, 0, 0, 0}));
  CHECK(radio.contains({A7105_REG_CALIBRATION, A7105_CALIBRATION_IF}));
  CHECK(radio.contains({A7105_REG_CALIBRATION, A7105_CALIBRATION_VCO_BANK}));
  CHECK(radio.contains({A7105_REG_PLL_CHANNEL, 0x00}));
  CHECK(radio.contains({A7105_REG_PLL_CHANNEL, 0xA0}));
  CHECK(radio.if_reads == 1 && radio.bank_reads == 2 && radio.calibration_reads == 3);
  std::vector<std::vector<uint8_t>> calibration;
  bool calibration_started = false;
  for (const auto &operation : radio.operations) {
    if (operation == std::vector<uint8_t>{A7105_REG_CALIBRATION, A7105_CALIBRATION_IF})
      calibration_started = true;
    if (calibration_started && (operation[0] == A7105_REG_CALIBRATION ||
        operation[0] == (A7105_REG_CALIBRATION | 0x40) ||
        operation[0] == (A7105_REG_IF_CALIB | 0x40) ||
        operation[0] == (A7105_REG_VCO_BANK | 0x40))) calibration.push_back(operation);
  }
  CHECK(calibration == (std::vector<std::vector<uint8_t>>{
      {2, 1}, {0x42, 0}, {0x62, 0}, {2, 2}, {0x42, 0}, {0x65, 0},
      {2, 2}, {0x42, 0}, {0x65, 0}}));
  CHECK(radio.registers[A7105_REG_VCO_CURRENT] == A7105_VCO_CURRENT_RECOMMENDED);
  CHECK(radio.registers[A7105_REG_VCO_THRESH] == A7105_VCO_THRESH_RECOMMENDED);
  CHECK(radio.begin_count == radio.operations.size() && radio.end_count == radio.begin_count);
  const auto setup_calls = radio.operations.size();
  for (unsigned call = 1; call < setup_calls; ++call) {
    sdk_test::set_time_us(0);
    Transport unavailable_during_setup;
    unavailable_during_setup.radio.lose_ready_after = call;
    failed_setup(unavailable_during_setup);
    CHECK(unavailable_during_setup.radio.operations.size() == call);
  }
  CHECK(transport.transmit(packet, -1).error == -1);
  CHECK(transport.transmit(packet, 48).error == -1);
  CHECK(radio.operations.size() == setup_calls && transport.ready());
  sdk_test::set_time_us(0);
  auto result = transport.transmit(packet, 47);
  CHECK(result.error == 0 && result.completed && result.restored && result.trigger_attempted);
  CHECK(transport.ready() && radio.tx_count == 1);
  CHECK(radio.registers[A7105_REG_PLL_CHANNEL] == a7105_probe_rf_channel_for_slot(A7105_TX_RX_RESTORE_CHANNEL_SLOT));
  CHECK(radio.operations.back() == std::vector<uint8_t>{A7105_CMD_RX_FIFO_RESET});
  CHECK(radio.contains({A7105_REG_PLL_CHANNEL, 0x97}));
  unsigned fifo_count = 0;
  for (const auto &operation : radio.operations) {
    if (operation.size() != 13) continue;
    ++fifo_count;
    CHECK(operation[0] == 5 && std::memcmp(operation.data() + 1, packet.bytes, 12) == 0);
  }
  CHECK(fifo_count == 1);
  const auto successful_tx_calls = radio.operations.size() - setup_calls;
  radio.never_active = true;
  result = transport.transmit(packet, 2);
  CHECK(result.error == -1 && !result.completed && result.restored && transport.ready());
  CHECK(result.elapsed_us <= 5000);
  radio.never_active = false;
  radio.stuck_tx = true;
  result = transport.transmit(packet, 2);
  CHECK(result.error == -1 && result.active_seen && !result.completed && result.restored && transport.ready());
  CHECK(result.elapsed_us <= 5000);

  for (int fault = 0; fault < 6; ++fault) {
    sdk_test::set_time_us(0);
    Transport rejected;
    rejected.radio.available = fault != 0;
    rejected.radio.bad_id = fault == 1;
    rejected.radio.if_flags = fault == 2 ? A7105_IF_CALIBRATION_FAIL : 0;
    rejected.radio.bank_flags = fault == 3 ? A7105_VCO_BANK_FAIL : 0;
    rejected.radio.unexpected_pending = fault == 4;
    rejected.radio.stuck_calibration = fault == 5;
    failed_setup(rejected);
    if (fault == 0) CHECK(rejected.radio.operations.empty());
    if (fault == 1) CHECK(rejected.radio.calibration_reads == 0);
    if (fault == 2) CHECK(rejected.radio.bank_reads == 0);
    if (fault == 3) CHECK(rejected.radio.bank_reads == 1);
    if (fault == 4) CHECK(rejected.radio.calibration_reads == 1);
    if (fault == 5) CHECK(rejected.radio.calibration_reads == 50);
  }
  {
    Transport second_bank_failure;
    second_bank_failure.radio.bank_flags = A7105_VCO_BANK_FAIL;
    second_bank_failure.radio.bank_fail_at = 2;
    failed_setup(second_bank_failure);
    CHECK(second_bank_failure.radio.bank_reads == 2);
  }
  for (unsigned stage : {2U, 3U}) {
    for (bool timeout : {false, true}) {
      sdk_test::set_time_us(0);
      Transport rejected;
      rejected.radio.fault_calibration_stage = stage;
      rejected.radio.stuck_calibration = timeout;
      rejected.radio.unexpected_pending = !timeout;
      failed_setup(rejected);
      CHECK(rejected.radio.calibration_stage == stage);
      CHECK(rejected.radio.calibration_reads == stage - 1 + (timeout ? 50 : 1));
      CHECK(rejected.radio.bank_reads == stage - 2);
    }
  }
  // Loss of delegate readiness is observable before the next transaction.
  // Void SDK transfers expose no arbitrary transfer-error result.
  for (unsigned offset : {1U, unsigned(successful_tx_calls - 1)}) {
    sdk_test::set_time_us(0);
    Transport stopped;
    stopped.setup();
    CHECK(stopped.ready());
    stopped.radio.lose_ready_after = stopped.radio.operations.size() + offset;
    result = stopped.transmit(packet, 2);
    CHECK(result.error == (offset == 1 ? -2 : -3) && !result.restored);
    CHECK(!stopped.ready() && stopped.is_failed());
    const auto count = stopped.radio.operations.size();
    CHECK(stopped.transmit(packet, 2).error == -1);
    CHECK(stopped.radio.operations.size() == count);
  }
  std::puts("MD7105 actual transport: SDK delegate admission/calibration/status/restoration checked");
}
