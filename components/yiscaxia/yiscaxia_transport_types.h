#pragma once

#include <stdint.h>
#include "k80_frame.h"

// Transport payload and attempt evidence are independent of the radio bus.
typedef struct {
  uint8_t bytes[K80_FRAME_SIZE];
} yiscaxia_tx_packet;

typedef struct {
  int error;
  int active_seen;
  int completed;
  int restored;
  int trigger_attempted;
  uint64_t elapsed_us;
  uint64_t started_us;
} yiscaxia_tx_result;
