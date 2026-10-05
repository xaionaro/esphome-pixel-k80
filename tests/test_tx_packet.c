#include "md7105_tx_packet_protocol.h"
#include "k80_controller_protocol.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

struct fake {
  unsigned calls, fail_at, tx_count, fifo_count, status_count;
  uint64_t now, cleanup_at;
  int never_active, stuck;
  uint8_t bytes[256][16];
  size_t lengths[256];
};
static int transfer(void *context, const uint8_t *tx, uint8_t *rx, size_t n) {
  struct fake *f = context;
  if (f->calls >= 256 || n > 16) return -1;
  memcpy(f->bytes[f->calls], tx, n);
  f->lengths[f->calls++] = n;
  memset(rx, 0, n);
  if (n == 1 && tx[0] == 0xD0) ++f->tx_count;
  if (n == 13 && tx[0] == 0x05) ++f->fifo_count;
  if (n == 1 && tx[0] == A7105_CMD_STANDBY && f->tx_count && f->cleanup_at)
    f->now = f->cleanup_at;
  if (n == 2 && tx[0] == 0x40) {
    ++f->status_count;
    rx[1] = f->never_active ? 0 : (f->stuck || f->status_count < 4 ? 3 : 0);
  }
  return f->calls == f->fail_at ? -1 : 0;
}
static uint64_t now(void *context) { return ((struct fake *)context)->now; }
static void delay(void *context, unsigned us) { ((struct fake *)context)->now += us; }
static int check(int condition, const char *message) {
  if (!condition) fprintf(stderr, "FAIL: %s\n", message);
  return !condition;
}
static int last_write(struct fake *f, uint8_t reg) {
  for (unsigned i = f->calls; i > 0; --i)
    if (f->lengths[i - 1] == 2 && f->bytes[i - 1][0] == reg) return f->bytes[i - 1][1];
  return -1;
}
int main(void) {
  int failures = 0;
  struct fake f = {0};
  a7105_probe_bus_t bus = {&f, transfer};
  yiscaxia_tx_result r = {0};
  for (int group = 0; group < 6; ++group) {
    for (int level = 0; level <= 100; level += 100) {
      yiscaxia_tx_packet packet = {{0}};
      failures += check(k80_controller_build_packet(group, level, packet.bytes) == 0,
                        "group endpoint builds");
      memset(&f, 0, sizeof(f));
      r = a7105_tx_packet_run(&bus, &packet, 2, now, delay, &f);
      failures += check(r.error == 0 && r.completed && r.restored && f.tx_count == 1,
                        "shared transaction can send every group endpoint once");
      for (unsigned i = 0; i < f.calls; ++i)
        if (f.lengths[i] == 13)
          failures += check(f.bytes[i][0] == 5 && !memcmp(f.bytes[i] + 1, packet.bytes, 12),
                            "shared FIFO retains exact typed body");
    }
  }
  yiscaxia_tx_packet packet = {{0}};
  failures += check(k80_controller_build_packet(3, 0, packet.bytes) == 0, "carrier test body");
  for (int slot = 0; slot < 48; ++slot) {
    memset(&f, 0, sizeof(f));
    r = a7105_tx_packet_run(&bus, &packet, slot, now, delay, &f);
    int selected = -1;
    unsigned selected_write = 0;
    for (unsigned i = 0; i < f.calls; ++i) {
      if (f.lengths[i] == 2 && f.bytes[i][0] == 0x0F) {
        selected = f.bytes[i][1];
        if (!selected_write && selected == 0x0A + 3 * slot) selected_write = i + 1;
      }
      if (f.lengths[i] == 1 && f.bytes[i][0] == 0xD0) {
        failures += check(selected == 0x0A + 3 * slot, "selected CHN is in force at TX strobe");
        // The selected-channel write is immediately before FIFO reset/burst/TX.
        selected_write = i - 2;
      }
    }
    failures += check(r.error == 0 && r.completed && r.restored && f.tx_count == 1 &&
                      last_write(&f, A7105_REG_PLL_CHANNEL) ==
                      a7105_probe_rf_channel_for_slot(
                        A7105_TX_RX_RESTORE_CHANNEL_SLOT),
                      "each endpoint carrier restores the configured RX return slot");
    failures += check(selected_write && f.lengths[selected_write - 1] == 2 &&
                      f.bytes[selected_write - 1][0] == 0x0F, "retune operation located before FIFO");
    memset(&f, 0, sizeof(f));
    f.fail_at = selected_write;
    r = a7105_tx_packet_run(&bus, &packet, slot, now, delay, &f);
    failures += check(r.error && r.restored && !f.tx_count &&
                      last_write(&f, A7105_REG_PLL_CHANNEL) ==
                      a7105_probe_rf_channel_for_slot(
                        A7105_TX_RX_RESTORE_CHANNEL_SLOT),
                      "failed selected-channel write cannot strobe and restores RX");
  }
  const int invalid[] = {-1, 48, 255, 256, INT_MIN, INT_MAX};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    memset(&f, 0, sizeof(f));
    r = a7105_tx_packet_run(&bus, &packet, invalid[i], now, delay, &f);
    failures += check(r.error == -1 && !f.calls, "invalid full-width carrier never touches SPI");
  }
  for (int fault = 1; fault <= 2; ++fault) {
    memset(&f, 0, sizeof(f));
    f.never_active = fault == 1;
    f.stuck = fault == 2;
    r = a7105_tx_packet_run(&bus, &packet, 0, now, delay, &f);
    failures += check(r.error != 0 && !r.completed && r.restored && f.now <= 5000,
                      "missing/stuck TX remains bounded and restores radio");
  }
  memset(&f, 0, sizeof(f));
  r = a7105_tx_packet_run(&bus, &packet, 2, now, delay, &f);
  const unsigned successful_calls = f.calls;
  unsigned trigger_call = 0, cleanup_call = 0;
  for (unsigned i = 0; i < f.calls; ++i) {
    if (f.lengths[i] == 1 && f.bytes[i][0] == 0xD0) trigger_call = i + 1;
    if (trigger_call && f.lengths[i] == 1 && f.bytes[i][0] == A7105_CMD_STANDBY)
      cleanup_call = i + 1;
  }
  failures += check(trigger_call && cleanup_call > trigger_call && r.trigger_attempted &&
                    r.started_us == 0, "zero-time trigger is stamped distinctly from no trigger");
  for (unsigned fail = 1; fail <= successful_calls; ++fail) {
    memset(&f, 0, sizeof(f));
    f.fail_at = fail;
    r = a7105_tx_packet_run(&bus, &packet, 2, now, delay, &f);
    const int expected_error = fail == cleanup_call ? -2 : fail > cleanup_call ? -3 : -1;
    failures += check(r.error == expected_error, "each callback fault retains transaction/cleanup cause");
    failures += check(r.restored == (fail < cleanup_call), "only successful cleanup marks restoration");
    failures += check(r.trigger_attempted == (fail >= trigger_call) && r.started_us == 0,
                      "pretrigger faults are unstamped; attempted trigger includes failed strobe");
    failures += check(f.tx_count == (fail >= trigger_call), "pretrigger faults never issue TX");
    failures += check(r.completed == (fail >= cleanup_call),
                      "cleanup faults retain observed completion; earlier faults cannot complete");
    failures += check(f.calls > fail || fail == cleanup_call || fail > cleanup_call,
                      "precleanup transfer failure still attempts independent standby");
  }
  const a7105_probe_bus_t bad_bus = {&f, NULL};
  for (int invalid_argument = 0; invalid_argument < 5; ++invalid_argument) {
    memset(&f, 0, sizeof(f));
    r = a7105_tx_packet_run(invalid_argument == 0 ? NULL : invalid_argument == 1 ? &bad_bus : &bus,
                            invalid_argument == 2 ? NULL : &packet, 2,
                            invalid_argument == 3 ? NULL : now,
                            invalid_argument == 4 ? NULL : delay, &f);
    failures += check(r.error == -1 && !r.restored && !r.trigger_attempted && !f.calls,
                      "missing packet/bus/callback/clock functions reject without SPI");
  }
  for (unsigned elapsed = 5000; elapsed <= 5001; ++elapsed) {
    memset(&f, 0, sizeof(f));
    f.cleanup_at = elapsed;
    r = a7105_tx_packet_run(&bus, &packet, 2, now, delay, &f);
    failures += check(r.elapsed_us == elapsed && r.restored && r.trigger_attempted &&
                      r.error == (elapsed == 5000 ? 0 : -4) &&
                      r.completed == (elapsed == 5000), "cleanup budget includes standby with exact boundary");
  }
  if (!failures)
    puts("TX packet: all transfer failure positions, admission, trigger and cleanup boundaries checked");
  return failures ? 1 : 0;
}
