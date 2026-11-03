// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * test_main.c - runs the real programmer code against the simulated target.
 *
 * Usage: test_programmer DATADIR   (DATADIR from make_test_hex.py)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "programmer.h"
#include "ihex.h"
#include "config.h"
#include "sim_target.h"
#include "sim_host.h"

static const char *datadir;
static int failures, checks;
static char path[512];

#define CHECK(cond, ...) do {                                   \
    checks++;                                                   \
    if (!(cond)) {                                              \
      failures++;                                               \
      printf("  FAIL %s:%d: ", __FILE__, __LINE__);             \
      printf(__VA_ARGS__);                                      \
      printf("\n");                                             \
    }                                                           \
  } while (0)

static const char *data(const char *name)
{
  snprintf(path, sizeof(path), "%s/%s", datadir, name);
  return path;
}

static uint8_t *load(const char *name, long *len)
{
  FILE *f = fopen(data(name), "rb");
  uint8_t *b;
  if (!f) { printf("cannot open %s\n", path); exit(2); }
  fseek(f, 0, SEEK_END);
  *len = ftell(f);
  fseek(f, 0, SEEK_SET);
  b = malloc((size_t)*len);
  if (fread(b, 1, (size_t)*len, f) != (size_t)*len) exit(2);
  fclose(f);
  return b;
}

static void expect_counts(const char *name, uint32_t *written, uint32_t *skipped)
{
  FILE *f = fopen(data(name), "r");
  if (!f || fscanf(f, "%u %u", written, skipped) != 2) { printf("bad %s\n", name); exit(2); }
  fclose(f);
}

static void check_protocol_clean(void)
{
  CHECK(sim.err_contention == 0, "SWDIO driven by both sides %u times", sim.err_contention);
  CHECK(sim.err_parity == 0, "write parity errors %u", sim.err_parity);
  CHECK(sim.err_csw == 0, "memory access with wrong CSW %u", sim.err_csw);
  CHECK(sim.err_write_not_enabled == 0, "NVM writes with NVMC not in write mode %u",
        sim.err_write_not_enabled);
  CHECK(sim.err_double_write == 0, "flash words written twice %u", sim.err_double_write);
  CHECK(sim.err_write_while_busy == 0, "flash written while NVMC busy %u", sim.err_write_while_busy);
}

static void check_uicr(void)
{
  static const uint32_t addr[] = { UICR_ADDR_0, UICR_ADDR_1, UICR_ADDR_2, UICR_ADDR_3 };
  static const uint32_t val[]  = { UICR_DATA_0, UICR_DATA_1, UICR_DATA_2, UICR_DATA_3 };
  uint32_t i, set = 0;
  for (i = 0; i < 4; i++) {
    uint32_t got = sim.uicr[(addr[i] - 0x10001000UL) / 4];
    CHECK(got == val[i], "UICR 0x%08X = 0x%08X, expected 0x%08X", addr[i], got, val[i]);
  }
  for (i = 0; i < SIM_UICR_SIZE / 4; i++) if (sim.uicr[i] != 0xFFFFFFFFUL) set++;
  CHECK(set == 4, "%u UICR words programmed, expected exactly 4", set);
}

static prog_err_t run(const char *hexname)
{
  sim_hex_path = hexname ? data(hexname) : 0;
  sim_busy_led = 1;                      /* the sketch turns it on before prog_run */
  sim_busy_led_offs = 0;
  return prog_run();
}

/* ------------------------------------------------------------------------ */

static void test_good_image(int locked, int legacy)
{
  long len;
  uint8_t *img = load("good.bin", &len);
  uint32_t written, skipped;
  prog_err_t err;

  printf("good.hex, target %s%s\n", locked ? "protected" : "unprotected",
         legacy ? " (unlocks only after reset)" : "");
  expect_counts("good.txt", &written, &skipped);
  sim_init(locked, legacy);
  err = run("good.hex");
  CHECK(err == PROG_OK, "result %s", prog_err_str(err));
  CHECK(memcmp(sim.flash, img, (size_t)len) == 0, "flash image differs from the HEX file");
  check_uicr();
  check_protocol_clean();
  CHECK(sim.eraseall_count == 1, "ERASEALL issued %u times", sim.eraseall_count);
  CHECK(prog_stats.blocks_written == written, "blocks written %u, expected %u",
        prog_stats.blocks_written, written);
  CHECK(prog_stats.blocks_skipped == skipped, "blank blocks skipped %u, expected %u",
        prog_stats.blocks_skipped, skipped);
  CHECK(prog_stats.blocks_verified == written, "blocks verified %u", prog_stats.blocks_verified);
  CHECK(prog_stats.uicr_bytes_ignored == 8, "UICR bytes ignored %u", prog_stats.uicr_bytes_ignored);
  CHECK(sim.sysresets == 1, "system resets %u", sim.sysresets);
  CHECK(sim.reset_pulses >= 2, "nRESET pulses %u", sim.reset_pulses);
  CHECK(sim.waits_sent > 0, "WAIT path not exercised");
  CHECK(sim.ready_polls_busy > 0, "NVMC READY polling not exercised");
  CHECK(sim_busy_led_offs == 1 && sim_busy_led == 0, "busy LED not turned off before verify");
  CHECK(sim.ctrl_ap_resets == (uint32_t)(legacy ? 1 : 0), "CTRL-AP resets %u", sim.ctrl_ap_resets);
  free(img);
}

static void test_typical_timing(void)
{
  long len;
  uint8_t *img = load("typical.bin", &len);
  uint32_t written, skipped;
  uint64_t clk_prog, clk_ver;
  prog_err_t err;
  double mhz[] = { 1.0, 2.0, 4.0 };
  int i;

  printf("typical.hex (MBR + SoftDevice + app + bootloader), timing estimate\n");
  expect_counts("typical.txt", &written, &skipped);
  sim_init(1, 0);
  sim_quiet = 1;
  err = run("typical.hex");
  sim_quiet = 0;
  CHECK(err == PROG_OK, "result %s", prog_err_str(err));
  CHECK(memcmp(sim.flash, img, (size_t)len) == 0, "flash image differs from the HEX file");
  CHECK(prog_stats.blocks_written == written, "blocks written %u, expected %u",
        prog_stats.blocks_written, written);
  check_uicr();
  check_protocol_clean();

  clk_ver = sim.clocks - sim_clocks_at_busy_off;
  clk_prog = sim_clocks_at_busy_off;
  printf("    image: %u blocks written (%u KB), %u blank blocks skipped\n",
         written, written / 2, skipped);
  printf("    SWD clocks: %llu to recover + program + UICR, %llu to verify\n",
         (unsigned long long)clk_prog, (unsigned long long)clk_ver);
  for (i = 0; i < 3; i++) {
    double prog_s = (double)clk_prog / (mhz[i] * 1e6);
    double nvm_s  = sim.words_written * 68e-6;   /* worst case word write time */
    double ver_s  = (double)clk_ver / (mhz[i] * 1e6);
    printf("    at %.0f MHz SWCLK: program %.1f to %.1f s, verify %.1f s"
           " (plus ~0.2 s ERASEALL and SD reads)\n",
           mhz[i], prog_s > nvm_s ? prog_s : nvm_s, prog_s + nvm_s, ver_s);
  }
  free(img);
}

static void test_error(const char *hexname, prog_err_t expect, int expect_erase)
{
  prog_err_t err;
  printf("%s -> expect \"%s\"\n", hexname ? hexname : "(no file)", prog_err_str(expect));
  sim_init(0, 0);
  err = run(hexname);
  CHECK(err == expect, "result \"%s\", expected \"%s\"", prog_err_str(err), prog_err_str(expect));
  CHECK(sim.eraseall_count == (uint32_t)expect_erase, "ERASEALL issued %u times", sim.eraseall_count);
  CHECK(sim.sysresets == 0, "target reset after an error");
  check_protocol_clean();
}

static void test_no_target(void)
{
  prog_err_t err;
  printf("no target connected -> expect \"%s\"\n", prog_err_str(PROG_E_SWD));
  sim_init(0, 0);
  sim.present = 0;
  err = run("good.hex");
  CHECK(err == PROG_E_SWD, "result \"%s\"", prog_err_str(err));
}

static void test_no_card(void)
{
  prog_err_t err;
  printf("no SD card -> expect \"%s\"\n", prog_err_str(PROG_E_SD));
  sim_init(0, 0);
  sim_card_present = 0;
  err = run("good.hex");
  sim_card_present = 1;
  CHECK(err == PROG_E_SD, "result \"%s\"", prog_err_str(err));
  CHECK(sim.eraseall_count == 0, "target erased without a card");
}

static void test_verify_failure(void)
{
  prog_err_t err;
  printf("flash bit stuck at 1 -> expect \"%s\"\n", prog_err_str(PROG_E_VERIFY));
  sim_init(0, 0);
  sim.stuck_addr = 0x74100;
  sim.stuck_bits = 0x00010000;
  err = run("good.hex");
  CHECK(err == PROG_E_VERIFY, "result \"%s\"", prog_err_str(err));
  CHECK(prog_stats.fail_addr == 0x74100, "fail address 0x%08X", prog_stats.fail_addr);
  CHECK(sim_busy_led == 0, "busy LED still on after programming");
}

static void test_wait_heavy(void)
{
  prog_err_t err;
  long len;
  uint8_t *img = load("good.bin", &len);
  printf("slow NVMC (10 WAITs and 5 busy polls per word)\n");
  sim_init(1, 0);
  sim.waits_per_write = 10;
  sim.busy_polls_per_write = 5;
  sim_quiet = 1;
  err = run("good.hex");
  sim_quiet = 0;
  CHECK(err == PROG_OK, "result \"%s\"", prog_err_str(err));
  CHECK(memcmp(sim.flash, img, (size_t)len) == 0, "flash image differs");
  check_protocol_clean();
  free(img);
}

static int cb_count;
static int count_cb(void *c, uint32_t a, const uint8_t *d, uint8_t n)
{
  (void)c; (void)a; (void)d; cb_count += n; return 0;
}

static void test_ihex_chunking(void)
{
  long len, i;
  uint8_t *txt = load("good.hex", &len);
  ihex_t p;
  int rc = IHEX_OK, total1, chunk;

  printf("ihex parser: same result for any chunk size\n");
  ihex_init(&p, count_cb, 0);
  cb_count = 0;
  rc = ihex_feed(&p, txt, (size_t)len);
  if (rc == IHEX_OK) rc = ihex_finish(&p);
  CHECK(rc == IHEX_DONE, "whole buffer rc %d", rc);
  total1 = cb_count;
  for (chunk = 1; chunk <= 7; chunk += 3) {
    ihex_init(&p, count_cb, 0);
    cb_count = 0;
    rc = IHEX_OK;
    for (i = 0; i < len && rc == IHEX_OK; i += chunk) {
      size_t n = (size_t)((len - i) < chunk ? (len - i) : chunk);
      rc = ihex_feed(&p, txt + i, n);
    }
    if (rc == IHEX_OK) rc = ihex_finish(&p);
    CHECK(rc == IHEX_DONE && cb_count == total1, "chunk %d: rc %d, bytes %d vs %d",
          chunk, rc, cb_count, total1);
  }
  free(txt);
}

int main(int argc, char **argv)
{
  if (argc < 2) { printf("usage: %s DATADIR\n", argv[0]); return 2; }
  datadir = argv[1];

  test_ihex_chunking();
  test_good_image(1, 0);
  test_good_image(1, 1);
  test_good_image(0, 0);
  test_wait_heavy();
  test_verify_failure();
  test_error("bad_checksum.hex", PROG_E_HEX, 1);
  test_error("out_of_order.hex", PROG_E_HEX_ORDER, 1);
  test_error("out_of_range.hex", PROG_E_RANGE, 1);
  test_error("no_eof.hex", PROG_E_HEX, 1);
  test_error("garbage.hex", PROG_E_HEX, 1);
  test_error("uicr_only.hex", PROG_E_EMPTY, 1);
  test_error("missing.hex", PROG_E_FILE, 0);
  test_no_card();
  test_no_target();
  test_typical_timing();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
