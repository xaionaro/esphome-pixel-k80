#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  A7105_REG_MODE = 0x00U,
  A7105_REG_MODE_CONTROL = 0x01U,
  A7105_REG_CALIBRATION = 0x02U,
  A7105_REG_FIFO_I = 0x03U,
  A7105_REG_FIFO_II = 0x04U,
  A7105_REG_ID_DATA = 0x06U,
  A7105_REG_CKO = 0x0AU,
  A7105_REG_GIO1 = 0x0BU,
  A7105_REG_GIO2 = 0x0CU,
  A7105_REG_CLOCK = 0x0DU,
  A7105_REG_DATA_RATE = 0x0EU,
  A7105_REG_PLL_CHANNEL = 0x0FU,
  A7105_REG_PLL_II = 0x10U,
  A7105_REG_PLL_III = 0x11U,
  A7105_REG_PLL_IV = 0x12U,
  A7105_REG_PLL_V = 0x13U,
  A7105_REG_DELAY_II = 0x17U,
  A7105_REG_RX = 0x18U,
  A7105_REG_RX_GAIN_I = 0x19U,
  A7105_REG_RSSI_ADC = 0x1DU,
  A7105_REG_ADC_CONTROL = 0x1EU,
  A7105_REG_CODE_I = 0x1FU,
  A7105_REG_CODE_II = 0x20U,
  A7105_REG_IF_CALIB = 0x22U,
  A7105_REG_VCO_CURRENT = 0x24U,
  A7105_REG_VCO_BANK = 0x25U,
  A7105_REG_VCO_THRESH = 0x26U,
  A7105_REG_RX_DEM_TEST_I = 0x29U,
  A7105_GIO1_4WIRE_DATA_OUT = 0x19U,
  // Direct-mode receive diagnostics only. RXD exposes the demodulated raw
  // stream without TRXD's ID-gated packet output; GIO2 marks preamble
  // detection. Neither selection admits a transmit state.
  A7105_GIO1_DIRECT_RXD = (8U << 2) | 1U,
  A7105_GIO2_DIRECT_CD = (2U << 2) | 1U,
  // CKO register: disabled for the normal scanner, recovered RX clock for
  // direct sampling, or the reset/default FSYCK/8 clock for the solder test.
  A7105_CKO_DISABLED = 0x00U,
  A7105_CKO_RCK = 0x0AU,
  A7105_CKO_FSYCK_DIV8 = 0xBAU,
  // GIO2S=0000 (WTR) is level-held until RX/TX finishes. It is retained as a
  // reference value, but the scanner does not use WTR as packet evidence.
  A7105_GIO2_WTR_STATUS = 1U,
  A7105_FIFO_EASY_FEP = 0x3FU,
  A7105_FIFO_EXTENSION_FEP = 0x00U,
  // GIO2S=0001 occupies bits 5:2; bit 0 enables the output. FSYNC rises
  // only after the configured ID matches and remains asserted through the
  // payload, making its falling edge the packet-complete evidence.
  A7105_GIO2_FSYNC_STATUS = (1U << 2) | 1U,
  A7105_GIO2_FSYNC_MATCH_LEVEL = 1U,
  A7105_GIO2_FSYNC_DONE_LEVEL = 0U,
  // MODE=0x00 is the A7105 software reset.  It is deliberately represented
  // as a named receive-safe operation rather than as a general mode value:
  // no other MODE/strobe value is admitted by this probe.
  A7105_MODE_SOFTWARE_RESET = 0x00U,
  // AIF shifts the upper-sideband RX LO 500 kHz below the programmed carrier.
  // Without it, the same PLL grid would receive a carrier 500 kHz too high.
  A7105_MODE_CONTROL_AUTO_IF = 0x20U,
  A7105_MODE_CONTROL_FIFO = A7105_MODE_CONTROL_AUTO_IF | 0x02U,
  A7105_MODE_CONTROL_RSSI = A7105_MODE_CONTROL_AUTO_IF | 0x40U,
  // Direct receive plus the A7105 RSSI/carrier detector. FMS remains zero;
  // ARSSI is enabled so GIO2=CD is actually driven while RX is active.
  A7105_MODE_CONTROL_DIRECT_CARRIER = A7105_MODE_CONTROL_AUTO_IF | 0x40U,
  A7105_MODE_CONTROL_DIRECT = A7105_MODE_CONTROL_AUTO_IF,
  A7105_ADC_RSSI_BACKGROUND = 0x02U,
  A7105_RSSI_THRESHOLD_DIRECT = 0x80U,
  A7105_CALIBRATION_IF = 0x01U,
  A7105_CALIBRATION_VCO_BANK = 0x02U,
  A7105_CALIBRATION_PENDING_MASK = 0x07U,
  A7105_CALIBRATION_IDLE = 0x00U,
  A7105_VCO_CURRENT_RECOMMENDED = 0x13U,
  A7105_VCO_THRESH_RECOMMENDED = 0x3BU,
  // Datasheet-recommended receive values: manual VGA with maximum mixer/LNA
  // gain, and 600-us crystal settling with the shortest AGC/RSSI delays.
  A7105_RX_GAIN_I_RECOMMENDED = 0x80U,
  A7105_DELAY_II_RECOMMENDED = 0x40U,
  A7105_IF_CALIBRATION_FAIL = (1U << 4),
  A7105_VCO_CURRENT_FAIL = (1U << 4),
  A7105_VCO_BANK_FAIL = 0x08U,
  A7105_CMD_STANDBY = 0xA0U,
  A7105_CMD_PLL = 0xB0U,
  A7105_CMD_RX = 0xC0U,
  A7105_CMD_RX_FIFO_RESET = 0xF0U,
  A7105_CMD_RX_FIFO = 0x45U,
  // The LC-8 FCC test report lists 48 channels from 2405 to 2475.5 MHz at
  // 1.5 MHz spacing.  The A7105 PLL base is about 2400 MHz and its register
  // step is 500 kHz, so the corresponding CHN values are 0x0A, 0x0D, ...
  // 0x97. These are RF slots, not K80 UI channel labels.
  A7105_SCAN_CHANNEL_COUNT = 48U,
  A7105_SCAN_RF_CHANNEL_BASE = 0x0AU,
  A7105_SCAN_RF_CHANNEL_STEP = 3U,
  A7105_FIXED_RSSI_CHANNEL_SLOT = 7U,
  A7105_SCAN_PROFILE_COUNT = 3U,
  A7105_SCAN_FIFO_BYTES = 64U,
#if defined(MD7105_RX_EXPERIMENT_SLOT) && \
    (MD7105_RX_EXPERIMENT_SLOT < 0 || \
     MD7105_RX_EXPERIMENT_SLOT >= 48)
#error "MD7105_RX_EXPERIMENT_SLOT must select one of the 48 RF slots"
#endif
  // Observed raw frame suffix: ten protected bytes followed by two check bytes.
  // Hardware CRC remains disabled so both check bytes are retained verbatim.
  A7105_OBSERVED_FIFO_BYTES = 12U,
  A7105_OBSERVED_FIFO_FEP = A7105_OBSERVED_FIFO_BYTES - 1U,
#ifdef MD7105_RX_EXPERIMENT_SLOT
  // Scoped RX-only experiment selection; it does not select a TX carrier or
  // the channel used after a one-shot TX transaction restores RX.
  A7105_RX_EXPERIMENT_CHANNEL_SLOT = MD7105_RX_EXPERIMENT_SLOT,
#else
  A7105_RX_EXPERIMENT_CHANNEL_SLOT = 2U,
#endif
  A7105_OBSERVED_PROFILE = 2U,
  A7105_DIRECT_CAPTURE_BYTES = 64U,
  A7105_PLL_CHANNEL_MAX = 0xA7U,
  A7105_SCAN_RF_CHANNEL_MAX = 0x97U,
  // A7105 carrier detect asserts on the low-code side of the RSSI threshold.
  // The adaptive receiver therefore treats a sample as a candidate only when
  // it falls at least this many ADC counts below that channel's idle baseline.
  A7105_RSSI_TRIGGER_MARGIN = 12U,
  // Reject isolated ADC glitches and bound direct-capture retriggers while a
  // noisy channel remains occupied. A remote burst only needs two samples;
  // later bursts are admitted after the short cooldown.
  A7105_RSSI_CANDIDATE_CONSECUTIVE = 2U,
  A7105_RSSI_TRIGGER_COOLDOWN_MS = 250U,
  // Diagnostic dwell sweep: hold each RF slot long enough to correlate a
  // human-generated ramp with the RSSI trace, while sampling often enough to
  // retain its timing.
  A7105_SLOW_RSSI_SWEEP_DWELL_MS = 3000U,
  A7105_SLOW_RSSI_SAMPLE_PERIOD_MS = 100U,
};

typedef struct {
  const char *name;
  uint8_t data_rate;
  uint8_t rx_dem;
  uint8_t rx;
  uint8_t code_i;
  uint8_t code_ii;
} a7105_scan_profile_t;

static const a7105_scan_profile_t a7105_scan_profiles[A7105_SCAN_PROFILE_COUNT] =
    {
        // The A7105 reference values are: RXSM=11, BWS=1 (0x62),
        // IDL=1/PML=11 (0x07), DCL=001/ETH=01 and PMD selected for the
        // rate (0x16 for 250/500k, 0x17 for <=125k).
        {"500k", 0x00U, 0x47U, 0x62U, 0x07U, 0x16U},
        {"250k", 0x01U, 0x47U, 0x62U, 0x07U, 0x16U},
        {"125k", 0x03U, 0x27U, 0x62U, 0x07U, 0x17U},
    };

typedef int (*a7105_probe_transfer_fn)(void *context, const uint8_t *tx,
                                       uint8_t *rx, size_t length);

typedef struct {
  void *context;
  a7105_probe_transfer_fn transfer;
} a7105_probe_bus_t;

enum {
  A7105_PROBE_OK = 0,
  A7105_PROBE_INVALID_ARGUMENT = -1,
  A7105_PROBE_TRANSPORT_ERROR = -2,
  A7105_CALIBRATION_SCAN_STOP = 0,
  A7105_CALIBRATION_SCAN_FULL = 1,
};

static inline int a7105_probe_scan_mode_after_calibration(
    int transport_ok, uint8_t pending) {
  if (!transport_ok) {
    return A7105_CALIBRATION_SCAN_STOP;
  }
  if (pending == 0U) {
    return A7105_CALIBRATION_SCAN_FULL;
  }
  return A7105_CALIBRATION_SCAN_STOP;
}

static inline int a7105_probe_bus_valid(const a7105_probe_bus_t *bus) {
  return bus != NULL && bus->transfer != NULL;
}

static inline size_t a7105_probe_scan_profile_count(void) {
  return A7105_SCAN_PROFILE_COUNT;
}

static inline size_t a7105_probe_direct_capture_bits(size_t received_bytes) {
  const size_t bounded = received_bytes < (size_t)A7105_DIRECT_CAPTURE_BYTES
                             ? received_bytes
                             : (size_t)A7105_DIRECT_CAPTURE_BYTES;
  return bounded * 8U;
}

static inline int a7105_probe_rssi_is_candidate(uint8_t sample,
                                                uint8_t baseline,
                                                uint8_t initialized) {
  if (initialized == 0U || sample >= baseline) {
    return 0;
  }
  return (unsigned)(baseline - sample) >= A7105_RSSI_TRIGGER_MARGIN;
}

static inline uint8_t a7105_probe_rssi_candidate_streak_step(
    uint8_t streak, uint8_t sample, uint8_t baseline, uint8_t initialized) {
  if (a7105_probe_rssi_is_candidate(sample, baseline, initialized) == 0) {
    return 0U;
  }
  if (streak >= A7105_RSSI_CANDIDATE_CONSECUTIVE) {
    return A7105_RSSI_CANDIDATE_CONSECUTIVE;
  }
  return (uint8_t)(streak + 1U);
}

static inline int a7105_probe_rssi_trigger_allowed(uint64_t now_us,
                                                   uint64_t blocked_until_us) {
  return now_us >= blocked_until_us;
}

static inline uint8_t a7105_probe_rssi_baseline_step(uint8_t baseline,
                                                     uint8_t sample) {
  if (sample > baseline) {
    const uint8_t delta = (uint8_t)(sample - baseline);
    const uint8_t step = (uint8_t)((delta + 7U) / 8U);
    return (uint8_t)(baseline + (step == 0U ? 1U : step));
  }
  if (baseline > sample) {
    const uint8_t delta = (uint8_t)(baseline - sample);
    const uint8_t step = (uint8_t)((delta + 7U) / 8U);
    return (uint8_t)(baseline - (step == 0U ? 1U : step));
  }
  return baseline;
}

static inline const a7105_scan_profile_t *a7105_probe_scan_profile(
    size_t index) {
  return index < A7105_SCAN_PROFILE_COUNT ? &a7105_scan_profiles[index] : NULL;
}

static inline uint8_t a7105_probe_rf_channel_for_slot(uint8_t slot) {
  if (slot >= A7105_SCAN_CHANNEL_COUNT) {
    return 0xFFU;
  }
  return (uint8_t)(A7105_SCAN_RF_CHANNEL_BASE +
                   slot * A7105_SCAN_RF_CHANNEL_STEP);
}

static inline int a7105_probe_read_status(const a7105_probe_bus_t *bus,
                                          uint8_t reg, uint8_t *value) {
  const uint8_t allowed =
      (uint8_t)(reg == A7105_REG_MODE ||
                reg == A7105_REG_MODE_CONTROL ||
                reg == A7105_REG_CALIBRATION ||
                reg == A7105_REG_GIO1 || reg == A7105_REG_GIO2 ||
                reg == A7105_REG_RSSI_ADC || reg == A7105_REG_IF_CALIB ||
                reg == A7105_REG_VCO_CURRENT || reg == A7105_REG_VCO_BANK);
  uint8_t tx[2] = {0U, 0U};
  uint8_t rx[2] = {0U, 0U};

  if (!a7105_probe_bus_valid(bus) || value == NULL || allowed == 0U) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  tx[0] = (uint8_t)(reg | 0x40U);
  if (bus->transfer(bus->context, tx, rx, sizeof(tx)) != 0) {
    return A7105_PROBE_TRANSPORT_ERROR;
  }
  *value = rx[1];
  return A7105_PROBE_OK;
}

static inline int a7105_probe_read_id(const a7105_probe_bus_t *bus,
                                      uint8_t id[4]) {
  const uint8_t tx[5] = {(uint8_t)(A7105_REG_ID_DATA | 0x40U), 0U, 0U, 0U,
                         0U};
  uint8_t rx[5] = {0U, 0U, 0U, 0U, 0U};

  if (!a7105_probe_bus_valid(bus) || id == NULL) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  if (bus->transfer(bus->context, tx, rx, sizeof(tx)) != 0) {
    return A7105_PROBE_TRANSPORT_ERROR;
  }
  id[0] = rx[1];
  id[1] = rx[2];
  id[2] = rx[3];
  id[3] = rx[4];
  return A7105_PROBE_OK;
}

// The diagnostic lane may write a deliberately nonzero ID to distinguish
// genuine ID-matched FSYNC from a reset/all-zero ID accepting noise. This is a
// configuration write only; it never writes the FIFO or selects a TX state.
static inline int a7105_probe_write_id(const a7105_probe_bus_t *bus,
                                       const uint8_t id[4]) {
  uint8_t tx[5] = {A7105_REG_ID_DATA, 0U, 0U, 0U, 0U};
  uint8_t rx[5] = {0U, 0U, 0U, 0U, 0U};
  if (!a7105_probe_bus_valid(bus) || id == NULL) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  tx[1] = id[0];
  tx[2] = id[1];
  tx[3] = id[2];
  tx[4] = id[3];
  if (bus->transfer(bus->context, tx, rx, sizeof(tx)) != 0) {
    return A7105_PROBE_TRANSPORT_ERROR;
  }
  return A7105_PROBE_OK;
}

static inline int a7105_probe_write_config(const a7105_probe_bus_t *bus,
                                           uint8_t reg, uint8_t value) {
  uint8_t tx[2] = {reg, value};
  uint8_t rx[2] = {0U, 0U};
  const int allowed =
      (reg == A7105_REG_GIO1 && value == A7105_GIO1_4WIRE_DATA_OUT) ||
      (reg == A7105_REG_GIO1 && value == A7105_GIO1_DIRECT_RXD) ||
      (reg == A7105_REG_GIO2 && value == A7105_GIO2_FSYNC_STATUS) ||
      (reg == A7105_REG_GIO2 && value == A7105_GIO2_DIRECT_CD) ||
      (reg == A7105_REG_CKO &&
       (value == A7105_CKO_DISABLED || value == A7105_CKO_RCK ||
        value == A7105_CKO_FSYCK_DIV8)) ||
      (reg == A7105_REG_MODE_CONTROL && value == A7105_MODE_CONTROL_DIRECT) ||
      (reg == A7105_REG_MODE_CONTROL &&
       value == A7105_MODE_CONTROL_DIRECT_CARRIER) ||
      (reg == A7105_REG_MODE_CONTROL && value == A7105_MODE_CONTROL_FIFO) ||
      (reg == A7105_REG_MODE_CONTROL && value == A7105_MODE_CONTROL_RSSI) ||
      (reg == A7105_REG_CALIBRATION &&
       (value == A7105_CALIBRATION_IDLE ||
        value == A7105_CALIBRATION_IF ||
        value == A7105_CALIBRATION_VCO_BANK)) ||
      (reg == A7105_REG_ADC_CONTROL && value == A7105_ADC_RSSI_BACKGROUND) ||
      (reg == A7105_REG_RSSI_ADC && value == A7105_RSSI_THRESHOLD_DIRECT) ||
      (reg == A7105_REG_FIFO_I &&
       (value == A7105_FIFO_EASY_FEP || value == A7105_OBSERVED_FIFO_FEP)) ||
      (reg == A7105_REG_FIFO_II && value == A7105_FIFO_EXTENSION_FEP) ||
      (reg == A7105_REG_RX && value == 0x62U) ||
      (reg == A7105_REG_CODE_I && value == 0x07U) ||
      (reg == A7105_REG_CODE_II && (value == 0x16U || value == 0x17U)) ||
      (reg == A7105_REG_CLOCK && value == 0xF5U) ||
      (reg == A7105_REG_DATA_RATE &&
       (value == 0x00U || value == 0x01U || value == 0x03U)) ||
      (reg == A7105_REG_PLL_CHANNEL && value <= A7105_PLL_CHANNEL_MAX) ||
      (reg == A7105_REG_PLL_II && value == 0x9EU) ||
      (reg == A7105_REG_PLL_III && value == 0x4BU) ||
      (reg == A7105_REG_PLL_IV && value == 0x00U) ||
      (reg == A7105_REG_PLL_V && value == 0x02U) ||
      (reg == A7105_REG_DELAY_II && value == A7105_DELAY_II_RECOMMENDED) ||
      (reg == A7105_REG_RX_GAIN_I && value == A7105_RX_GAIN_I_RECOMMENDED) ||
      (reg == A7105_REG_VCO_CURRENT &&
       value == A7105_VCO_CURRENT_RECOMMENDED) ||
      (reg == A7105_REG_VCO_THRESH &&
       value == A7105_VCO_THRESH_RECOMMENDED) ||
      (reg == A7105_REG_RX_DEM_TEST_I &&
       (value == 0x47U || value == 0x27U));

  if (!a7105_probe_bus_valid(bus) || !allowed) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  if (bus->transfer(bus->context, tx, rx, sizeof(tx)) != 0) {
    return A7105_PROBE_TRANSPORT_ERROR;
  }
  return A7105_PROBE_OK;
}

static inline int a7105_probe_reset_device(const a7105_probe_bus_t *bus) {
  // The A7105 reset command is a write of 0x00 to MODE.  It clears the
  // transceiver state machine without selecting TX or touching the FIFOs.
  uint8_t tx[2] = {A7105_REG_MODE, A7105_MODE_SOFTWARE_RESET};
  uint8_t rx[2] = {0U, 0U};
  if (!a7105_probe_bus_valid(bus)) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  if (bus->transfer(bus->context, tx, rx, sizeof(tx)) != 0) {
    return A7105_PROBE_TRANSPORT_ERROR;
  }
  return A7105_PROBE_OK;
}

static inline int a7105_probe_configure_4wire(const a7105_probe_bus_t *bus) {
  int result = a7105_probe_write_config(bus, A7105_REG_CKO,
                                        A7105_CKO_DISABLED);
  if (result != A7105_PROBE_OK) {
    return result;
  }
  result =
      a7105_probe_write_config(bus, A7105_REG_GIO1,
                               A7105_GIO1_4WIRE_DATA_OUT);
  if (result != A7105_PROBE_OK) {
    return result;
  }
  return a7105_probe_write_config(bus, A7105_REG_GIO2,
                                  A7105_GIO2_FSYNC_STATUS);
}

static inline int a7105_probe_configure_rx_profile(
    const a7105_probe_bus_t *bus, uint8_t channel,
    const a7105_scan_profile_t *profile) {
  if (!a7105_probe_bus_valid(bus) || profile == NULL ||
      channel >= A7105_SCAN_CHANNEL_COUNT) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  if ((profile->data_rate != 0x00U && profile->data_rate != 0x01U &&
       profile->data_rate != 0x03U) ||
      (profile->rx_dem != 0x47U && profile->rx_dem != 0x27U) ||
      profile->rx != 0x62U || profile->code_i != 0x07U ||
      (profile->code_ii != 0x16U && profile->code_ii != 0x17U)) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }

  const uint8_t rf_channel = a7105_probe_rf_channel_for_slot(channel);
  const uint8_t registers[][2] = {
      {A7105_REG_GIO1, A7105_GIO1_4WIRE_DATA_OUT},
      {A7105_REG_GIO2, A7105_GIO2_FSYNC_STATUS},
      {A7105_REG_MODE_CONTROL, A7105_MODE_CONTROL_FIFO},
      {A7105_REG_FIFO_I, A7105_FIFO_EASY_FEP},
      {A7105_REG_FIFO_II, A7105_FIFO_EXTENSION_FEP},
      {A7105_REG_RX, profile->rx},
      {A7105_REG_CODE_I, profile->code_i},
      {A7105_REG_CODE_II, profile->code_ii},
      {A7105_REG_CLOCK, 0xF5U},
      {A7105_REG_DATA_RATE, profile->data_rate},
      {A7105_REG_PLL_CHANNEL, rf_channel},
      {A7105_REG_PLL_II, 0x9EU},
      {A7105_REG_PLL_III, 0x4BU},
      {A7105_REG_PLL_IV, 0x00U},
      {A7105_REG_PLL_V, 0x02U},
      {A7105_REG_DELAY_II, A7105_DELAY_II_RECOMMENDED},
      {A7105_REG_RX_GAIN_I, A7105_RX_GAIN_I_RECOMMENDED},
      {A7105_REG_RX_DEM_TEST_I, profile->rx_dem},
  };
  for (size_t index = 0U; index < sizeof(registers) / sizeof(registers[0]);
       ++index) {
    const int result =
        a7105_probe_write_config(bus, registers[index][0], registers[index][1]);
    if (result != A7105_PROBE_OK) {
      return result;
    }
  }
  return A7105_PROBE_OK;
}

static inline int a7105_probe_initialize_rx(const a7105_probe_bus_t *bus) {
  // Install a complete, receive-only baseline before calibration.  The
  // selected channel is only a calibration starting point; scan retuning
  // writes the channel again for every slot.
  return a7105_probe_configure_rx_profile(
      bus, 0U, &a7105_scan_profiles[0]);
}

// Identified K80 FIFO tuning; general scan profiles retain their original
// 64-byte FIFO and calibration baseline.
static inline int a7105_probe_configure_observed_fifo_at(
    const a7105_probe_bus_t *bus, uint8_t channel) {
  const int result = a7105_probe_configure_rx_profile(
      bus, channel,
      &a7105_scan_profiles[A7105_OBSERVED_PROFILE]);
  if (result != A7105_PROBE_OK) {
    return result;
  }
  return a7105_probe_write_config(bus, A7105_REG_FIFO_I,
                                  A7105_OBSERVED_FIFO_FEP);
}

static inline int a7105_probe_configure_observed_fifo(
    const a7105_probe_bus_t *bus) {
  return a7105_probe_configure_observed_fifo_at(
      bus, A7105_RX_EXPERIMENT_CHANNEL_SLOT);
}

// Receive-only identified-FIFO scheduling; no controller or TX dependency.
enum {
  A7105_K80_FIFO_WAIT = 0,
  A7105_K80_FIFO_CAPTURE = 1,
  A7105_K80_FIFO_EXPIRE = 2,
  A7105_K80_FIFO_SETTLE_US = 5000,
  A7105_K80_FIFO_DWELL_US = 15000,
  A7105_K80_FIFO_COMPLETION_GRACE_US = 3000,
};

static inline int a7105_k80_scan_slot(int current, int scan_all, int fixed) {
  return scan_all ? current : fixed;
}

static inline int a7105_k80_scan_next(int current, int scan_all, int fixed) {
  return scan_all ? (current + 1) % A7105_SCAN_CHANNEL_COUNT : fixed;
}

static inline int a7105_k80_scan_bounds_valid(int first, int last, int repeat) {
  return first >= 0 && first <= last && last < A7105_SCAN_CHANNEL_COUNT &&
      repeat >= 1 && repeat <= 16;
}

static inline int a7105_k80_scan_resident_slot(int current, int scan_all,
    int fixed, int first, int last) {
  if (!scan_all) return fixed;
  return current >= first && current <= last ? current : first;
}

// Called exactly once for a successfully serviced capture or expired window.
static inline int a7105_k80_scan_terminal_next(int current, int scan_all,
    int fixed, int first, int last, int repeat, unsigned *ordinal) {
  if (!scan_all) {
    *ordinal = 0U;
    return fixed;
  }
  current = a7105_k80_scan_resident_slot(current, scan_all, fixed, first, last);
  if (++*ordinal < (unsigned)repeat) return current;
  *ordinal = 0U;
  return current < last ? current + 1 : first;
}

static inline int a7105_k80_fifo_action(int64_t now, int64_t started,
    int matched, int completed, int64_t match_started) {
  // Service a complete latched packet before retuning, including delayed service.
  if (matched && completed && now >= started + A7105_K80_FIFO_SETTLE_US)
    return A7105_K80_FIFO_CAPTURE;
  const int64_t deadline = started + A7105_K80_FIFO_DWELL_US;
  if (now < deadline) return A7105_K80_FIFO_WAIT;
  if (matched && match_started >= started && match_started <= deadline &&
      now < deadline + A7105_K80_FIFO_COMPLETION_GRACE_US)
    return A7105_K80_FIFO_WAIT;
  return A7105_K80_FIFO_EXPIRE;
}

static inline int a7105_probe_configure_direct_profile(
    const a7105_probe_bus_t *bus, uint8_t channel,
    const a7105_scan_profile_t *profile) {
  if (!a7105_probe_bus_valid(bus) || profile == NULL ||
      channel >= A7105_SCAN_CHANNEL_COUNT) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  if ((profile->data_rate != 0x00U && profile->data_rate != 0x01U &&
       profile->data_rate != 0x03U) ||
      (profile->rx_dem != 0x47U && profile->rx_dem != 0x27U) ||
      profile->rx != 0x62U || profile->code_i != 0x07U ||
      (profile->code_ii != 0x16U && profile->code_ii != 0x17U)) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }

  const uint8_t rf_channel = a7105_probe_rf_channel_for_slot(channel);
  // GIO1 is SPI MISO in the default 4-wire profile. Keep it as SDO until the
  // final write so every preceding configuration transaction retains normal
  // bus observability. The caller must restore the 4-wire profile before any
  // subsequent SPI read.
  const uint8_t registers[][2] = {
      {A7105_REG_RX, profile->rx},
      {A7105_REG_CODE_I, profile->code_i},
      {A7105_REG_CODE_II, profile->code_ii},
      {A7105_REG_CLOCK, 0xF5U},
      {A7105_REG_DATA_RATE, profile->data_rate},
      {A7105_REG_PLL_CHANNEL, rf_channel},
      {A7105_REG_PLL_II, 0x9EU},
      {A7105_REG_PLL_III, 0x4BU},
      {A7105_REG_PLL_IV, 0x00U},
      {A7105_REG_PLL_V, 0x02U},
      {A7105_REG_DELAY_II, A7105_DELAY_II_RECOMMENDED},
      {A7105_REG_RX_GAIN_I, A7105_RX_GAIN_I_RECOMMENDED},
      {A7105_REG_RX_DEM_TEST_I, profile->rx_dem},
      {A7105_REG_CKO, A7105_CKO_RCK},
      {A7105_REG_RSSI_ADC, A7105_RSSI_THRESHOLD_DIRECT},
      {A7105_REG_ADC_CONTROL, A7105_ADC_RSSI_BACKGROUND},
      {A7105_REG_GIO2, A7105_GIO2_DIRECT_CD},
      {A7105_REG_MODE_CONTROL, A7105_MODE_CONTROL_DIRECT_CARRIER},
      {A7105_REG_GIO1, A7105_GIO1_DIRECT_RXD},
  };
  for (size_t index = 0U; index < sizeof(registers) / sizeof(registers[0]);
       ++index) {
    const int result =
        a7105_probe_write_config(bus, registers[index][0], registers[index][1]);
    if (result != A7105_PROBE_OK) {
      return result;
    }
  }
  return A7105_PROBE_OK;
}

static inline int a7105_probe_send_strobe(const a7105_probe_bus_t *bus,
                                          uint8_t command) {
  if (command != A7105_CMD_STANDBY && command != A7105_CMD_PLL &&
      command != A7105_CMD_RX && command != A7105_CMD_RX_FIFO_RESET) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  const uint8_t tx[1] = {command};
  uint8_t rx[1] = {0U};
  if (!a7105_probe_bus_valid(bus) ||
      bus->transfer(bus->context, tx, rx, sizeof(tx)) != 0) {
    return a7105_probe_bus_valid(bus) ? A7105_PROBE_TRANSPORT_ERROR
                                      : A7105_PROBE_INVALID_ARGUMENT;
  }
  return A7105_PROBE_OK;
}

static inline int a7105_probe_send_rx(const a7105_probe_bus_t *bus) {
  return a7105_probe_send_strobe(bus, A7105_CMD_RX);
}

static inline int a7105_probe_reset_rx_fifo(const a7105_probe_bus_t *bus) {
  return a7105_probe_send_strobe(bus, A7105_CMD_RX_FIFO_RESET);
}

static inline int a7105_probe_read_rx_fifo(const a7105_probe_bus_t *bus,
                                           uint8_t *payload, size_t length) {
  uint8_t tx[A7105_SCAN_FIFO_BYTES + 1U] = {0U};
  uint8_t rx[A7105_SCAN_FIFO_BYTES + 1U] = {0U};
  if (!a7105_probe_bus_valid(bus) || payload == NULL || length == 0U ||
      length > A7105_SCAN_FIFO_BYTES) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  tx[0] = A7105_CMD_RX_FIFO;
  if (bus->transfer(bus->context, tx, rx, length + 1U) != 0) {
    return A7105_PROBE_TRANSPORT_ERROR;
  }
  for (size_t index = 0U; index < length; ++index) {
    payload[index] = rx[index + 1U];
  }
  return A7105_PROBE_OK;
}

static inline int a7105_probe_configure_rssi(const a7105_probe_bus_t *bus,
                                             uint8_t channel) {
  if (!a7105_probe_bus_valid(bus) ||
      channel >= A7105_SCAN_CHANNEL_COUNT) {
    return A7105_PROBE_INVALID_ARGUMENT;
  }
  const uint8_t rf_channel = a7105_probe_rf_channel_for_slot(channel);
  const uint8_t registers[][2] = {
      {A7105_REG_GIO1, A7105_GIO1_4WIRE_DATA_OUT},
      {A7105_REG_GIO2, A7105_GIO2_FSYNC_STATUS},
      {A7105_REG_MODE_CONTROL, A7105_MODE_CONTROL_RSSI},
      {A7105_REG_ADC_CONTROL, A7105_ADC_RSSI_BACKGROUND},
      {A7105_REG_RX, a7105_scan_profiles[0].rx},
      {A7105_REG_CODE_I, a7105_scan_profiles[0].code_i},
      {A7105_REG_CODE_II, a7105_scan_profiles[0].code_ii},
      {A7105_REG_RX_DEM_TEST_I, a7105_scan_profiles[0].rx_dem},
      {A7105_REG_CLOCK, 0xF5U},
      {A7105_REG_PLL_CHANNEL, rf_channel},
      {A7105_REG_PLL_II, 0x9EU},
      {A7105_REG_PLL_III, 0x4BU},
      {A7105_REG_PLL_IV, 0x00U},
      {A7105_REG_PLL_V, 0x02U},
      {A7105_REG_DELAY_II, A7105_DELAY_II_RECOMMENDED},
      {A7105_REG_RX_GAIN_I, A7105_RX_GAIN_I_RECOMMENDED},
  };
  for (size_t index = 0U; index < sizeof(registers) / sizeof(registers[0]);
       ++index) {
    const int result =
        a7105_probe_write_config(bus, registers[index][0], registers[index][1]);
    if (result != A7105_PROBE_OK) {
      return result;
    }
  }
  return A7105_PROBE_OK;
}

#ifdef __cplusplus
}
#endif
