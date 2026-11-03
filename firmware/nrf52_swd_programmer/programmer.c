// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * programmer.c - the programming cycle. See programmer.h and docs/SPEC.md.
 *
 * The HEX file is streamed twice (program pass, verify pass). Data is
 * collected into one PROGRAM_BLOCK_SIZE buffer aligned to the block size;
 * when the stream moves on to the next block the buffer is flushed. A block
 * that is entirely 0xFF is skipped in both passes.
 */
#include <string.h>
#include "programmer.h"
#include "config.h"
#include "ihex.h"
#include "hexsrc.h"
#include "nrf52_target.h"
#include "swd.h"
#include "swd_port.h"
#include "log.h"

#if (PROGRAM_BLOCK_SIZE % 4) || (1024 % PROGRAM_BLOCK_SIZE)
#error "PROGRAM_BLOCK_SIZE must be a multiple of 4 that divides 1024"
#endif

#define BLOCK_WORDS (PROGRAM_BLOCK_SIZE / 4)

/* ------------------------------------------------------------------------ */
/* UICR table from config.h                                                 */
/* ------------------------------------------------------------------------ */

static const uint32_t uicr_addr[] = {
  UICR_ADDR_0, UICR_ADDR_1, UICR_ADDR_2, UICR_ADDR_3,
#if UICR_ENTRY_COUNT > 4
  UICR_ADDR_4,
#endif
#if UICR_ENTRY_COUNT > 5
  UICR_ADDR_5,
#endif
#if UICR_ENTRY_COUNT > 6
  UICR_ADDR_6,
#endif
#if UICR_ENTRY_COUNT > 7
  UICR_ADDR_7,
#endif
};

static const uint32_t uicr_data[] = {
  UICR_DATA_0, UICR_DATA_1, UICR_DATA_2, UICR_DATA_3,
#if UICR_ENTRY_COUNT > 4
  UICR_DATA_4,
#endif
#if UICR_ENTRY_COUNT > 5
  UICR_DATA_5,
#endif
#if UICR_ENTRY_COUNT > 6
  UICR_DATA_6,
#endif
#if UICR_ENTRY_COUNT > 7
  UICR_DATA_7,
#endif
};

#if UICR_ENTRY_COUNT < 1 || UICR_ENTRY_COUNT > 8
#error "UICR_ENTRY_COUNT must be 1..8"
#endif

/* ------------------------------------------------------------------------ */
/* Block assembler                                                          */
/* ------------------------------------------------------------------------ */

#define PASS_PROGRAM 0
#define PASS_VERIFY  1

typedef struct {
  int        pass;
  uint32_t   flash_size;
  int        active;
  uint32_t   base;
  prog_err_t err;
  union {
    uint8_t  bytes[PROGRAM_BLOCK_SIZE];
    uint32_t words[BLOCK_WORDS];
  } blk;
  uint32_t   readback[BLOCK_WORDS];
} pass_ctx_t;

static pass_ctx_t ctx;
prog_stats_t prog_stats;

static uint32_t le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void block_start(uint32_t base)
{
  ctx.base = base;
  ctx.active = 1;
  memset(ctx.blk.bytes, 0xFF, sizeof(ctx.blk.bytes));
}

static int block_flush(void)
{
  uint32_t i;
  int blank = 1;

  ctx.active = 0;
  for (i = 0; i < PROGRAM_BLOCK_SIZE; i++) {
    if (ctx.blk.bytes[i] != 0xFF) { blank = 0; break; }
  }
  if (blank) {
    if (ctx.pass == PASS_PROGRAM) prog_stats.blocks_skipped++;
    return 0;
  }

  /* Byte order independent of the host: the target is little endian. */
  for (i = 0; i < BLOCK_WORDS; i++) ctx.blk.words[i] = le32(&ctx.blk.bytes[4 * i]);

  if (ctx.pass == PASS_PROGRAM) {
    int rc = nrf52_write_words(ctx.base, ctx.blk.words, BLOCK_WORDS);
    if (rc != TGT_OK) {
      LOG("Write failed at block 0x%08lX (code %d)\r\n", (unsigned long)ctx.base, rc);
      prog_stats.fail_addr = ctx.base;
      ctx.err = PROG_E_WRITE;
      return 1;
    }
    prog_stats.blocks_written++;
  } else {
    if (nrf52_read_words(ctx.base, ctx.readback, BLOCK_WORDS) != TGT_OK) {
      LOG("Read-back failed at block 0x%08lX\r\n", (unsigned long)ctx.base);
      prog_stats.fail_addr = ctx.base;
      ctx.err = PROG_E_VERIFY;
      return 1;
    }
    for (i = 0; i < BLOCK_WORDS; i++) {
      if (ctx.readback[i] != ctx.blk.words[i]) {
        prog_stats.fail_addr = ctx.base + 4 * i;
        prog_stats.fail_expected = ctx.blk.words[i];
        prog_stats.fail_actual = ctx.readback[i];
        LOG("Verify error at 0x%08lX: expected 0x%08lX, read 0x%08lX\r\n",
            (unsigned long)prog_stats.fail_addr, (unsigned long)prog_stats.fail_expected,
            (unsigned long)prog_stats.fail_actual);
        ctx.err = PROG_E_VERIFY;
        return 1;
      }
    }
    prog_stats.blocks_verified++;
  }
  return 0;
}

static int on_data(void *unused, uint32_t addr, const uint8_t *data, uint8_t len)
{
  uint8_t i;
  (void)unused;

  for (i = 0; i < len; i++) {
    uint32_t a = addr + i;
    uint32_t base = a & ~(uint32_t)(PROGRAM_BLOCK_SIZE - 1);

    if (a >= TGT_UICR_BASE && a < TGT_UICR_END) {           /* UICR records are ignored */
      if (ctx.pass == PASS_PROGRAM) prog_stats.uicr_bytes_ignored++;
      continue;
    }
    if (a >= ctx.flash_size) {
      LOG("HEX data at 0x%08lX is outside flash (size 0x%lX)\r\n", (unsigned long)a,
          (unsigned long)ctx.flash_size);
      prog_stats.fail_addr = a;
      ctx.err = PROG_E_RANGE;
      return 1;
    }
    if (!ctx.active) {
      block_start(base);
    } else if (base != ctx.base) {
      if (base < ctx.base) {
        LOG("HEX addresses go backwards at 0x%08lX\r\n", (unsigned long)a);
        prog_stats.fail_addr = a;
        ctx.err = PROG_E_HEX_ORDER;
        return 1;
      }
      if (block_flush()) return 1;
      block_start(base);
    }
    ctx.blk.bytes[a - base] = data[i];
  }
  return 0;
}

static prog_err_t hexsrc_to_err(int rc)
{
  if (rc == HEXSRC_ERR_FILE) return PROG_E_FILE;
  return PROG_E_SD;
}

static prog_err_t run_pass(int pass, uint32_t flash_size)
{
  static ihex_t parser;
  static uint8_t buf[512];
  uint16_t br;
  int rc;

  ctx.pass = pass;
  ctx.flash_size = flash_size;
  ctx.active = 0;
  ctx.err = PROG_OK;

  rc = hexsrc_open();
  if (rc != HEXSRC_OK) return hexsrc_to_err(rc);

  ihex_init(&parser, on_data, 0);
  for (;;) {
    if (hexsrc_read(buf, sizeof(buf), &br) != HEXSRC_OK) return PROG_E_SD;
    if (br == 0) {
      rc = ihex_finish(&parser);
      break;
    }
    rc = ihex_feed(&parser, buf, br);
    if (rc != IHEX_OK) break;
  }

  if (rc == IHEX_ERR_CALLBACK) return ctx.err;
  if (rc != IHEX_DONE) {
    LOG("HEX file error %d at line %lu\r\n", rc, (unsigned long)parser.line);
    return PROG_E_HEX;
  }
  if (ctx.active && block_flush()) return ctx.err;
  return PROG_OK;
}

/* ------------------------------------------------------------------------ */
/* UICR                                                                     */
/* ------------------------------------------------------------------------ */

static prog_err_t write_uicr(void)
{
  uint32_t i;
  if (nrf52_nvmc_config(TGT_NVMC_CONFIG_WEN) != TGT_OK) return PROG_E_UICR;
  for (i = 0; i < UICR_ENTRY_COUNT; i++) {
    LOG("UICR 0x%08lX = 0x%08lX\r\n", (unsigned long)uicr_addr[i], (unsigned long)uicr_data[i]);
    if (nrf52_write_words(uicr_addr[i], &uicr_data[i], 1) != TGT_OK) {
      nrf52_nvmc_config(TGT_NVMC_CONFIG_REN);
      return PROG_E_UICR;
    }
  }
  if (nrf52_nvmc_config(TGT_NVMC_CONFIG_REN) != TGT_OK) return PROG_E_UICR;
  return PROG_OK;
}

static prog_err_t verify_uicr(void)
{
  uint32_t i, v;
  for (i = 0; i < UICR_ENTRY_COUNT; i++) {
    if (nrf52_read_words(uicr_addr[i], &v, 1) != TGT_OK || v != uicr_data[i]) {
      prog_stats.fail_addr = uicr_addr[i];
      prog_stats.fail_expected = uicr_data[i];
      prog_stats.fail_actual = v;
      LOG("UICR verify error at 0x%08lX: expected 0x%08lX, read 0x%08lX\r\n",
          (unsigned long)uicr_addr[i], (unsigned long)uicr_data[i], (unsigned long)v);
      return PROG_E_UICR_VERIFY;
    }
  }
  return PROG_OK;
}

/* ------------------------------------------------------------------------ */
/* Cycle                                                                    */
/* ------------------------------------------------------------------------ */

static prog_err_t tgt_to_err(int rc)
{
  switch (rc) {
    case TGT_ERR_CTRL_AP: return PROG_E_NOT_NRF52;
    case TGT_ERR_RECOVER: return PROG_E_RECOVER;
    case TGT_ERR_LOCKED:  return PROG_E_LOCKED;
    case TGT_ERR_HALT:    return PROG_E_HALT;
    default:              return PROG_E_SWD;
  }
}

static prog_err_t cycle(void)
{
  nrf52_info_t info;
  prog_err_t err;
  uint32_t t;
  int rc;

  /* Check the card and file before touching the target, so a missing file
   * never erases a good board. */
  rc = hexsrc_open();
  if (rc != HEXSRC_OK) {
    LOG("Cannot open %s (code %d)\r\n", HEX_FILENAME, rc);
    return hexsrc_to_err(rc);
  }
  LOG("%s: %lu bytes\r\n", HEX_FILENAME, (unsigned long)hexsrc_size());

  rc = nrf52_connect(&info);
  if (rc != TGT_OK) return tgt_to_err(rc);

  t = port_millis();
  rc = nrf52_recover();
  if (rc != TGT_OK) return tgt_to_err(rc);
  prog_stats.ms_recover = port_millis() - t;

  if (nrf52_read_info(&info) != TGT_OK) return PROG_E_SWD;
  LOG("Target nRF%lX variant %c%c%c%c, flash %lu KB, page %lu bytes\r\n",
      (unsigned long)info.part,
      (char)(info.variant >> 24), (char)(info.variant >> 16),
      (char)(info.variant >> 8), (char)info.variant,
      (unsigned long)(info.flash_size / 1024), (unsigned long)info.page_size);

  /* Program flash */
  t = port_millis();
  if (nrf52_nvmc_config(TGT_NVMC_CONFIG_WEN) != TGT_OK) return PROG_E_WRITE;
  err = run_pass(PASS_PROGRAM, info.flash_size);
  if (nrf52_nvmc_config(TGT_NVMC_CONFIG_REN) != TGT_OK && err == PROG_OK) err = PROG_E_WRITE;
  prog_stats.ms_program = port_millis() - t;
  if (err != PROG_OK) return err;
  LOG("Programmed %lu blocks, skipped %lu blank blocks in %lu ms\r\n",
      (unsigned long)prog_stats.blocks_written, (unsigned long)prog_stats.blocks_skipped,
      (unsigned long)prog_stats.ms_program);
  if (prog_stats.uicr_bytes_ignored) {
    LOG("Ignored %lu bytes of UICR data in the HEX file\r\n",
        (unsigned long)prog_stats.uicr_bytes_ignored);
  }
  if (prog_stats.blocks_written == 0) return PROG_E_EMPTY;

  /* UICR */
  err = write_uicr();
  if (err != PROG_OK) return err;

  /* Verify */
  ui_busy_led(0);
  t = port_millis();
  err = run_pass(PASS_VERIFY, info.flash_size);
  if (err == PROG_OK) err = verify_uicr();
  prog_stats.ms_verify = port_millis() - t;
  if (err != PROG_OK) return err;
  LOG("Verified %lu blocks and %d UICR words in %lu ms\r\n",
      (unsigned long)prog_stats.blocks_verified, UICR_ENTRY_COUNT,
      (unsigned long)prog_stats.ms_verify);

  nrf52_reset_and_run();
  return PROG_OK;
}

prog_err_t prog_run(void)
{
  uint32_t t0 = port_millis();
  prog_err_t err;

  memset(&prog_stats, 0, sizeof(prog_stats));
  swd_wait_count = 0;

  err = cycle();
  if (err != PROG_OK) {
    /* Leave the target alone: drop debug power and float the lines. */
    swd_power_down();
    swd_release();
  }
  prog_stats.ms_total = port_millis() - t0;
  LOG("Result: %s (%lu ms, %lu SWD waits)\r\n", prog_err_str(err),
      (unsigned long)prog_stats.ms_total, (unsigned long)swd_wait_count);
  return err;
}

const char *prog_err_str(prog_err_t err)
{
  switch (err) {
    case PROG_OK:            return "PASS";
    case PROG_E_SD:          return "SD card error";
    case PROG_E_FILE:        return "HEX file not found";
    case PROG_E_SWD:         return "no SWD connection to target";
    case PROG_E_NOT_NRF52:   return "target is not an nRF52";
    case PROG_E_RECOVER:     return "recover (ERASEALL) failed";
    case PROG_E_LOCKED:      return "target still protected after recover";
    case PROG_E_HALT:        return "target core did not halt";
    case PROG_E_HEX:         return "HEX file format error";
    case PROG_E_HEX_ORDER:   return "HEX file addresses not ascending";
    case PROG_E_RANGE:       return "HEX data outside target flash";
    case PROG_E_EMPTY:       return "HEX file has no flash data";
    case PROG_E_WRITE:       return "flash write failed";
    case PROG_E_UICR:        return "UICR write failed";
    case PROG_E_VERIFY:      return "flash verify failed";
    case PROG_E_UICR_VERIFY: return "UICR verify failed";
  }
  return "unknown error";
}
