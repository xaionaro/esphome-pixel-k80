#include "md7105_transport.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esp_timer.h"
#include <cstring>

namespace esphome::pixel_k80 {

static const char *const TAG = "pixel_k80.md7105";

int Md7105Transport::transfer_(void *context, const uint8_t *tx, uint8_t *rx, size_t length) {
  auto *transport = static_cast<Md7105Transport *>(context);
  if (tx == nullptr || rx == nullptr || !transport->spi_is_ready())
    return -1;
  transport->enable();
  transport->delegate_->transfer(tx, rx, length);
  transport->disable();
  // ESPHome's SPI delegate exposes no transaction error result.
  return 0;
}

uint64_t Md7105Transport::now_(void *) { return static_cast<uint64_t>(esp_timer_get_time()); }

uint64_t Md7105Transport::now_us() const { return now_(nullptr); }

void Md7105Transport::delay_(void *, unsigned microseconds) { delayMicroseconds(microseconds); }

bool Md7105Transport::wait_calibration_(uint8_t expected) {
  const uint64_t started = now_(nullptr);
  while (now_(nullptr) - started < 5000U) {
    uint8_t pending = 0;
    if (a7105_probe_read_status(&this->bus_, A7105_REG_CALIBRATION, &pending) != A7105_PROBE_OK)
      return false;
    if ((pending & A7105_CALIBRATION_PENDING_MASK) == 0U)
      return true;
    if (pending != expected)
      return false;
  }
  return false;
}

bool Md7105Transport::calibrate_() {
  if (a7105_probe_send_strobe(&this->bus_, A7105_CMD_STANDBY) != A7105_PROBE_OK)
    return false;
  const uint8_t settings[][2] = {
    {A7105_REG_CLOCK, 0xF5U}, {A7105_REG_PLL_II, 0x9EU}, {A7105_REG_PLL_III, 0x4BU},
    {A7105_REG_PLL_IV, 0x00U}, {A7105_REG_PLL_V, 0x02U},
  };
  for (const auto &setting : settings) {
    if (a7105_probe_write_config(&this->bus_, setting[0], setting[1]) != A7105_PROBE_OK)
      return false;
  }
  uint8_t flags = 0;
  if (a7105_probe_write_config(&this->bus_, A7105_REG_CALIBRATION, A7105_CALIBRATION_IF) != A7105_PROBE_OK ||
      !this->wait_calibration_(A7105_CALIBRATION_IF) ||
      a7105_probe_read_status(&this->bus_, A7105_REG_IF_CALIB, &flags) != A7105_PROBE_OK ||
      (flags & A7105_IF_CALIBRATION_FAIL) != 0U)
    return false;
  // The carrier uses manual VCO current; do not request VCC calibration.
  if (a7105_probe_write_config(&this->bus_, A7105_REG_VCO_CURRENT, A7105_VCO_CURRENT_RECOMMENDED) != A7105_PROBE_OK ||
      a7105_probe_write_config(&this->bus_, A7105_REG_VCO_THRESH, A7105_VCO_THRESH_RECOMMENDED) != A7105_PROBE_OK)
    return false;
  const uint8_t channels[] = {0x00U, 0xA0U};
  for (uint8_t channel : channels) {
    if (a7105_probe_send_strobe(&this->bus_, A7105_CMD_STANDBY) != A7105_PROBE_OK ||
        a7105_probe_write_config(&this->bus_, A7105_REG_PLL_CHANNEL, channel) != A7105_PROBE_OK ||
        a7105_probe_send_strobe(&this->bus_, A7105_CMD_PLL) != A7105_PROBE_OK ||
        a7105_probe_write_config(&this->bus_, A7105_REG_CALIBRATION, A7105_CALIBRATION_VCO_BANK) != A7105_PROBE_OK ||
        !this->wait_calibration_(A7105_CALIBRATION_VCO_BANK) ||
        a7105_probe_read_status(&this->bus_, A7105_REG_VCO_BANK, &flags) != A7105_PROBE_OK ||
        (flags & A7105_VCO_BANK_FAIL) != 0U)
      return false;
  }
  return a7105_probe_send_strobe(&this->bus_, A7105_CMD_STANDBY) == A7105_PROBE_OK;
}

void Md7105Transport::setup() {
  this->spi_setup();
  uint8_t readback[4] {};
  if (!this->spi_is_ready() || a7105_probe_reset_device(&this->bus_) != A7105_PROBE_OK ||
      a7105_probe_configure_4wire(&this->bus_) != A7105_PROBE_OK ||
      a7105_probe_initialize_rx(&this->bus_) != A7105_PROBE_OK ||
      a7105_probe_write_id(&this->bus_, a7105_k80_radio_id) != A7105_PROBE_OK ||
      a7105_probe_read_id(&this->bus_, readback) != A7105_PROBE_OK ||
      std::memcmp(a7105_k80_radio_id, readback, sizeof(a7105_k80_radio_id)) != 0 || !this->calibrate_()) {
    a7105_probe_send_strobe(&this->bus_, A7105_CMD_STANDBY);
    ESP_LOGE(TAG, "Radio initialization or calibration failed");
    this->mark_failed();
    return;
  }
  this->ready_ = true;
}

void Md7105Transport::dump_config() {
  ESP_LOGCONFIG(TAG, "MD7105 transport:");
  LOG_PIN("  CS pin: ", this->cs_);
  ESP_LOGCONFIG(TAG, "  Radio ready: %s", this->ready_ ? "yes" : "no");
}

pixel_k80_tx_result Md7105Transport::transmit(const pixel_k80_tx_packet &packet, int slot) {
  if (!this->ready_) {
    pixel_k80_tx_result result{};
    result.error = PIXEL_K80_TX_FAILED;
    return result;
  }
  const auto result = a7105_tx_packet_run(&this->bus_, &packet, slot, now_, delay_, nullptr);
  if (result.error == PIXEL_K80_TX_STANDBY_FAILED || result.error == PIXEL_K80_TX_RESTORE_FAILED) {
    this->ready_ = false;
    this->mark_failed();
    ESP_LOGE(TAG, "Radio restoration failed; transport stopped");
  }
  return result;
}

}  // namespace esphome::pixel_k80
