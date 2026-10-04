#pragma once
#include "md7105_probe_protocol.h"
#include "yiscaxia_transport_types.h"
#include <string.h>
enum {
  A7105_TX_DIAGNOSTIC_CHANNEL_SLOT = 1U,
  A7105_TX_RX_RESTORE_CHANNEL_SLOT = 2U,
};
// K80 RF identity used for initialization, each transmission and RX restoration.
static const uint8_t a7105_k80_radio_id[4] = {0xA5, 0x5A, 0xB9, 0x46};

typedef uint64_t (*a7105_tx_now_fn)(void *);
typedef void (*a7105_tx_delay_fn)(void *, unsigned);
static inline yiscaxia_tx_result a7105_tx_packet_run(
    const a7105_probe_bus_t *bus, const yiscaxia_tx_packet *packet, int slot,
    a7105_tx_now_fn now, a7105_tx_delay_fn delay, void *clock) {
  yiscaxia_tx_result result = {0};
  result.error = YISCAXIA_TX_FAILED;
  if (packet == NULL || slot < 0 || slot >= A7105_SCAN_CHANNEL_COUNT) return result;
  if (!a7105_probe_bus_valid(bus) || now == NULL || delay == NULL) return result;

  // Manufacturer section 19: PAC=2/TBG=7 is nominal approximately 0 dBm.
  static const uint8_t settings[][2] = {{0x14, 0x16}, {0x15, 0x2B}, {0x28, 0x17}};
  uint8_t fifo[K80_FRAME_SIZE + 1] = {0x05};
  memcpy(fifo + 1, packet->bytes, sizeof(packet->bytes));
  uint8_t rx[sizeof(fifo)] = {0};
  const uint8_t reset = 0xE0, transmit = 0xD0;
  uint64_t started = 0;
  if (a7105_probe_send_strobe(bus, A7105_CMD_STANDBY) != A7105_PROBE_OK ||
      a7105_probe_configure_4wire(bus) != A7105_PROBE_OK ||
      a7105_probe_configure_observed_fifo_at(
          bus, A7105_TX_RX_RESTORE_CHANNEL_SLOT) != A7105_PROBE_OK ||
      a7105_probe_write_id(bus, a7105_k80_radio_id) != A7105_PROBE_OK) goto cleanup;
  for (size_t i = 0; i < sizeof(settings)/sizeof(settings[0]); ++i) {
    if (bus->transfer(bus->context, settings[i], rx, 2) != 0) goto cleanup;
  }
  // Configure the explicit RX return channel, then retune to this endpoint's
  // requested slot immediately before FIFO/TX. The PLL state machine handles
  // settling; endpoint selection remains independent across all 48 slots.
  if (a7105_probe_write_config(bus, A7105_REG_PLL_CHANNEL,
      a7105_probe_rf_channel_for_slot((uint8_t)slot)) != A7105_PROBE_OK) goto cleanup;
  if (bus->transfer(bus->context, &reset, rx, 1) != 0 ||
      bus->transfer(bus->context, fifo, rx, sizeof(fifo)) != 0) goto cleanup;
  started = now(clock);
  result.started_us = started;
  result.trigger_attempted = 1;
  if (bus->transfer(bus->context, &transmit, rx, 1) != 0) goto cleanup;
  // Reserve 500 us for the last status transaction and explicit standby.
  while (now(clock) - started < 4500U) {
    uint8_t mode = 0;
    if (a7105_probe_read_status(bus, A7105_REG_MODE, &mode) != A7105_PROBE_OK) goto cleanup;
    if ((mode & 3U) == 3U) result.active_seen = 1;
    if (result.active_seen && (mode & 1U) == 0U) {
      result.completed = 1;
      result.error = YISCAXIA_TX_OK;
      break;
    }
    delay(clock, 50);
  }
  result.elapsed_us = now(clock) - started;
cleanup:
  // Standby is attempted independently even after an earlier SPI error.
  // If it fails, stop here: the caller must halt all radio service.
  if (a7105_probe_send_strobe(bus, A7105_CMD_STANDBY) != A7105_PROBE_OK) {
    result.error = YISCAXIA_TX_STANDBY_FAILED;
    return result;
  }
  if (result.trigger_attempted) {
    result.elapsed_us = now(clock) - started;
    if (result.elapsed_us > 5000U) {
      result.error = YISCAXIA_TX_DEADLINE_EXCEEDED;
      result.completed = 0;
    }
  }
  if (a7105_probe_configure_4wire(bus) != A7105_PROBE_OK ||
      a7105_probe_configure_observed_fifo_at(
          bus, A7105_TX_RX_RESTORE_CHANNEL_SLOT) != A7105_PROBE_OK ||
      a7105_probe_write_id(bus, a7105_k80_radio_id) != A7105_PROBE_OK ||
      a7105_probe_reset_rx_fifo(bus) != A7105_PROBE_OK) {
    result.error = YISCAXIA_TX_RESTORE_FAILED;
    return result;
  }
  result.restored = 1;
  return result;
}
