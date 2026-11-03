// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * sim_target.c - cycle-level software model of an nRF52832 SWD target.
 *
 * It implements the sim_* pin functions declared in swd_port.h, so the real
 * swd.c / nrf52_target.c / programmer.c run unchanged against it. The model
 * covers what the programmer uses: SW-DP (line reset, JTAG-to-SWD, IDCODE,
 * ABORT, CTRL/STAT, SELECT, RDBUFF, WAIT/FAULT), the AHB-AP with posted
 * reads and 1 KB auto-increment wrap, Nordic's CTRL-AP (ERASEALL,
 * APPROTECTSTATUS, RESET) and the NVMC rules for flash and UICR writes.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sim_target.h"

sim_t sim;

/* ------------------------------------------------------------------------ */
/* Memory model                                                             */
/* ------------------------------------------------------------------------ */

static uint32_t *flash_word(uint32_t addr, int *is_nvm)
{
  *is_nvm = 0;
  if (addr < SIM_FLASH_SIZE) { *is_nvm = 1; return &sim.flash[addr / 4]; }
  if (addr >= 0x10001000UL && addr < 0x10001000UL + SIM_UICR_SIZE) {
    *is_nvm = 1;
    return &sim.uicr[(addr - 0x10001000UL) / 4];
  }
  return 0;
}

static uint32_t mem_read(uint32_t addr)
{
  int nvm;
  uint32_t *w = flash_word(addr, &nvm);
  if (w) return *w;
  switch (addr) {
    case 0x10000010UL: return 4096;               /* FICR.CODEPAGESIZE */
    case 0x10000014UL: return SIM_FLASH_SIZE / 4096;
    case 0x10000100UL: return 0x52832;            /* FICR.INFO.PART */
    case 0x10000104UL: return 0x41414530;         /* "AAE0" */
    case 0x4001E400UL:                            /* NVMC.READY */
      if (sim.nvmc_busy) { sim.nvmc_busy--; sim.ready_polls_busy++; return 0; }
      return 1;
    case 0x4001E504UL: return sim.nvmc_config;
    case 0xE000EDF0UL: return sim.halted ? (1UL << 17) | 3 : 0;
    default:
      sim.unknown_reads++;
      return 0;
  }
}

static void mem_write(uint32_t addr, uint32_t v)
{
  int nvm;
  uint32_t *w = flash_word(addr, &nvm);
  if (w) {
    if (sim.nvmc_config != 1) { sim.err_write_not_enabled++; return; }
    if (sim.nvmc_busy) { sim.err_write_while_busy++; }
    if (*w != 0xFFFFFFFFUL) sim.err_double_write++;
    *w &= v;
    if (addr == sim.stuck_addr) *w |= sim.stuck_bits;   /* defect injection */
    sim.words_written++;
    sim.nvmc_busy = sim.busy_polls_per_write;            /* READY low for a while */
    sim.wait_pending = sim.waits_per_write;              /* AP stalls for a while */
    return;
  }
  switch (addr) {
    case 0x4001E504UL: sim.nvmc_config = v & 3; break;
    case 0xE000EDF0UL:
      if ((v & 0xFFFF0000UL) == 0xA05F0000UL) {
        if (!(v & 1)) sim.halted = 0;
        else if (v & 2) sim.halted = 1;
      }
      break;
    case 0xE000ED0CUL:
      if (v == 0x05FA0004UL) { sim.sysresets++; sim.halted = 0; }
      break;
    default:
      sim.unknown_writes++;
      break;
  }
}

/* ------------------------------------------------------------------------ */
/* Debug port / access ports                                                */
/* ------------------------------------------------------------------------ */

#define ACK_OK    1
#define ACK_WAIT  2
#define ACK_FAULT 4

static void do_eraseall(void)
{
  memset(sim.flash, 0xFF, sizeof(sim.flash));
  memset(sim.uicr, 0xFF, sizeof(sim.uicr));
  sim.erased = 1;
  sim.halted = 0;
  if (!sim.legacy_unlock) sim.protected_ = 0;   /* unlocked until next reset */
  else sim.unlock_on_reset = 1;                 /* unlocked after CTRL-AP reset */
}

static uint32_t ap_access(int rnw, uint8_t a, uint32_t wdata, int *ack)
{
  uint32_t apsel = sim.select >> 24;
  uint32_t reg = (sim.select & 0xF0) | a;
  *ack = ACK_OK;

  if (apsel == 0) {                             /* AHB-AP */
    if (reg == 0x0C && sim.protected_) {        /* memory access blocked */
      sim.sticky = 1;
      *ack = ACK_FAULT;
      return 0;
    }
    switch (reg) {
      case 0x00: if (rnw) return sim.csw; sim.csw = wdata; return 0;
      case 0x04: if (rnw) return sim.tar; sim.tar = wdata; return 0;
      case 0x0C: {
        uint32_t v = 0;
        if ((sim.csw & 0x37) != 0x12) sim.err_csw++;
        if (rnw) v = mem_read(sim.tar); else mem_write(sim.tar, wdata);
        sim.tar = (sim.tar & ~0x3FFUL) | ((sim.tar + 4) & 0x3FFUL);   /* 1 KB wrap */
        return v;
      }
      case 0xFC: return 0x24770011;
      default: return 0;
    }
  }
  if (apsel == 1) {                             /* Nordic CTRL-AP */
    switch (reg) {
      case 0x000:
        if (rnw) return sim.ctrl_reset;
        if (sim.ctrl_reset && !wdata) {         /* reset released */
          sim.ctrl_ap_resets++;
          sim.halted = 0;
          if (sim.unlock_on_reset) { sim.protected_ = 0; sim.unlock_on_reset = 0; }
        }
        sim.ctrl_reset = wdata & 1;
        return 0;
      case 0x004:
        if (rnw) return 0;
        if (wdata & 1) { sim.eraseall_busy = 5; sim.eraseall_count++; }
        return 0;
      case 0x008:
        if (sim.eraseall_busy) {
          if (--sim.eraseall_busy == 0) do_eraseall();
          return 1;
        }
        return 0;
      case 0x00C: return sim.protected_ ? 0 : 1;
      case 0x0FC: return 0x02880000UL;
      default: return 0;
    }
  }
  return 0;
}

/* Decide the ACK for a request and perform reads (value to send). */
static int handle_request(int apndp, int rnw, uint8_t a, uint32_t *rdata)
{
  int ack = ACK_OK;

  if (sim.need_idcode && !(apndp == 0 && rnw && a == 0)) return 0;   /* lockout */

  if (apndp) {
    if (sim.sticky) return ACK_FAULT;
    if (sim.wait_pending) { sim.wait_pending--; sim.waits_sent++; return ACK_WAIT; }
    if (sim.nvmc_busy && !sim.wait_pending) { /* time passes */ }
    if (rnw) {
      uint32_t v = ap_access(1, a, 0, &ack);
      if (ack != ACK_OK) return ack;
      *rdata = sim.rdbuff;                      /* posted: previous result */
      sim.rdbuff = v;
    }
    return ACK_OK;                               /* writes are performed later */
  }

  /* DP */
  if (rnw) {
    switch (a) {
      case 0x0: *rdata = 0x2BA01477UL; sim.need_idcode = 0; break;
      case 0x4: {
        uint32_t s = sim.ctrl_stat;
        if (s & (1UL << 28)) s |= 1UL << 29;
        if (s & (1UL << 30)) s |= 1UL << 31;
        if (sim.sticky) s |= 1UL << 5;
        *rdata = s;
        break;
      }
      case 0x8: *rdata = 0; break;
      case 0xC: *rdata = sim.rdbuff; break;
    }
  }
  return ACK_OK;
}

static void handle_write_data(int apndp, uint8_t a, uint32_t v)
{
  if (apndp) {
    int ack;
    ap_access(0, a, v, &ack);
    return;
  }
  switch (a) {
    case 0x0:                                   /* ABORT */
      if (v & 0x1E) sim.sticky = 0;
      break;
    case 0x4: sim.ctrl_stat = v & 0x50000000UL; break;
    case 0x8: sim.select = v; break;
    default: break;
  }
}

/* ------------------------------------------------------------------------ */
/* Wire level state machine, advanced on each SWCLK rising edge             */
/* ------------------------------------------------------------------------ */

enum { S_JTAG, S_LOCKOUT, S_RESET, S_IDLE, S_REQ, S_TRN1, S_ACK, S_RDATA,
       S_TRN_END, S_TRN_W, S_WDATA };

static int clk, host_out = 1, host_dir_out = 1, tgt_drive, tgt_out = 1;

static int parity32(uint32_t v)
{
  int p = 0;
  while (v) { p ^= (int)(v & 1); v >>= 1; }
  return p;
}

static void rising_edge(void)
{
  int hb = host_dir_out ? host_out : -1;
  sim.clocks++;

  if (host_dir_out && tgt_drive) sim.err_contention++;

  /* Line reset detection (only host-driven bits count). */
  if (hb == 1) sim.ones++; else sim.ones = 0;

  if (!sim.present) return;                     /* nothing connected */

  if (sim.state == S_JTAG) {
    if (hb >= 0) sim.jtag_shift = (uint16_t)((sim.jtag_shift >> 1) | ((uint16_t)hb << 15));
    if (sim.jtag_shift == 0xE79E) sim.state = S_LOCKOUT;
    return;
  }
  if (sim.ones >= 50 && (sim.state == S_IDLE || sim.state == S_LOCKOUT || sim.state == S_RESET ||
                         sim.state == S_REQ)) {
    sim.state = S_RESET;
    sim.need_idcode = 1;
    tgt_drive = 0;
    return;
  }

  switch (sim.state) {
    case S_LOCKOUT:
      break;
    case S_RESET:
      if (hb == 0) sim.state = S_IDLE;
      break;
    case S_IDLE:
      if (hb == 1) { sim.req = 1; sim.cnt = 1; sim.state = S_REQ; }
      break;
    case S_REQ:
      if (hb < 0) { sim.err_protocol++; sim.state = S_IDLE; break; }
      sim.req |= (uint32_t)hb << sim.cnt;
      if (++sim.cnt == 8) {
        int apndp = (sim.req >> 1) & 1, rnw = (sim.req >> 2) & 1;
        uint8_t a = (uint8_t)(((sim.req >> 3) & 3) << 2);
        int par = (sim.req >> 5) & 1;
        if (((sim.req >> 6) & 1) != 0 || ((sim.req >> 7) & 1) != 1 ||
            par != parity32((sim.req >> 1) & 0xF)) {
          sim.err_protocol++;
          sim.state = S_IDLE;                   /* no response */
          break;
        }
        sim.apndp = apndp; sim.rnw = rnw; sim.a = a;
        sim.rdata = 0;
        sim.ack = handle_request(apndp, rnw, a, &sim.rdata);
        if (sim.ack == 0) { sim.state = S_IDLE; break; }  /* silent: host sees 111 */
        sim.transactions++;
        sim.state = S_TRN1;
      }
      break;
    case S_TRN1:                                /* turnaround done, drive ACK */
      tgt_drive = 1;
      tgt_out = sim.ack & 1;
      sim.cnt = 1;
      sim.state = S_ACK;
      break;
    case S_ACK:
      if (sim.cnt < 3) {
        tgt_out = (sim.ack >> sim.cnt) & 1;
        sim.cnt++;
        break;
      }
      if (sim.ack == ACK_OK && sim.rnw) {
        tgt_out = (int)(sim.rdata & 1);
        sim.cnt = 1;
        sim.state = S_RDATA;
      } else if (sim.ack == ACK_OK) {
        tgt_drive = 0;
        sim.state = S_TRN_W;
      } else {
        tgt_drive = 0;
        sim.state = S_TRN_END;
      }
      break;
    case S_RDATA:
      if (sim.cnt < 32) tgt_out = (int)((sim.rdata >> sim.cnt) & 1);
      else if (sim.cnt == 32) tgt_out = parity32(sim.rdata);
      else { tgt_drive = 0; sim.state = S_TRN_END; break; }
      sim.cnt++;
      break;
    case S_TRN_END:
      sim.state = S_IDLE;
      break;
    case S_TRN_W:
      sim.wdata = 0;
      sim.cnt = 0;
      sim.state = S_WDATA;
      break;
    case S_WDATA:
      if (hb < 0) { sim.err_protocol++; sim.state = S_IDLE; break; }
      if (sim.cnt < 32) {
        sim.wdata |= (uint32_t)hb << sim.cnt;
      } else {
        if (hb != parity32(sim.wdata)) sim.err_parity++;
        else handle_write_data(sim.apndp, sim.a, sim.wdata);
        sim.state = S_IDLE;
        break;
      }
      sim.cnt++;
      break;
    default:
      break;
  }
}

void sim_swclk(int level)
{
  if (!clk && level) rising_edge();
  clk = level;
}

void sim_swdio_out(int level)     { host_out = level; }
void sim_swdio_dir_out(int out)   { host_dir_out = out; }
int  sim_swdio_in(void)           { return tgt_drive ? tgt_out : (host_dir_out ? host_out : 1); }

void sim_target_reset(int asserted)
{
  if (asserted && !sim.reset_asserted) sim.reset_pulses++;
  sim.reset_asserted = asserted;
}

void sim_init(int locked, int legacy_unlock)
{
  memset(&sim, 0, sizeof(sim));
  /* Old firmware in flash, so a missing erase would be caught. */
  for (uint32_t i = 0; i < SIM_FLASH_WORDS; i++) sim.flash[i] = 0x12345678UL ^ (i * 2654435761UL);
  memset(sim.uicr, 0xFF, sizeof(sim.uicr));
  sim.uicr[0x208 / 4] = 0xFFFFFF00UL;          /* old APPROTECT value */
  sim.present = 1;
  sim.protected_ = locked;
  sim.legacy_unlock = legacy_unlock;
  sim.state = S_JTAG;
  sim.busy_polls_per_write = 1;
  sim.waits_per_write = 1;
  sim.stuck_addr = 0xFFFFFFFFUL;
  clk = 0; host_out = 1; host_dir_out = 1; tgt_drive = 0; tgt_out = 1;
}
