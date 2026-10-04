#pragma once

#include <stdint.h>
#include "k80_frame.h"

// Transport payload and attempt evidence are independent of the radio bus.
typedef struct {
  uint8_t bytes[K80_FRAME_SIZE];
} yiscaxia_tx_packet;

// Attempt outcome; restoration and trigger evidence remain independently useful.
enum {
  YISCAXIA_TX_OK = 0,
  YISCAXIA_TX_FAILED = -1,
  YISCAXIA_TX_STANDBY_FAILED = -2,
  YISCAXIA_TX_RESTORE_FAILED = -3,
  YISCAXIA_TX_DEADLINE_EXCEEDED = -4,
};

typedef struct {
  int error;
  int active_seen;
  int completed;
  int restored;
  int trigger_attempted;
  uint64_t elapsed_us;
  uint64_t started_us;
} yiscaxia_tx_result;
