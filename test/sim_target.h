// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
#ifndef SIM_TARGET_H
#define SIM_TARGET_H

#include <stdint.h>

#define SIM_FLASH_SIZE   (512UL * 1024UL)
#define SIM_FLASH_WORDS  (SIM_FLASH_SIZE / 4)
#define SIM_UICR_SIZE    0x1000UL

typedef struct {
  /* memory */
  uint32_t flash[SIM_FLASH_WORDS];
  uint32_t uicr[SIM_UICR_SIZE / 4];
  uint32_t nvmc_config;
  int      nvmc_busy;
  int      wait_pending;
  int      busy_polls_per_write;
  int      waits_per_write;
  uint32_t stuck_addr, stuck_bits;   /* defect injection for verify tests */
  int      halted;
  int      protected_;
  int      legacy_unlock;            /* protection lifts only after CTRL-AP reset */
  int      unlock_on_reset;
  int      erased;
  int      eraseall_busy;
  uint32_t ctrl_reset;

  /* debug port */
  int      present;
  int      state;
  int      need_idcode;
  int      sticky;
  uint16_t jtag_shift;
  uint32_t ones;
  uint32_t req, cnt;
  int      apndp, rnw, ack;
  uint8_t  a;
  uint32_t rdata, wdata;
  uint32_t select, ctrl_stat, rdbuff, csw, tar;

  /* statistics and error counters */
  uint64_t clocks;
  uint32_t transactions, waits_sent, ready_polls_busy, words_written;
  uint32_t eraseall_count, ctrl_ap_resets, sysresets, reset_pulses;
  int      reset_asserted;
  uint32_t err_contention, err_protocol, err_parity, err_csw;
  uint32_t err_write_not_enabled, err_double_write, err_write_while_busy;
  uint32_t unknown_reads, unknown_writes;
} sim_t;

extern sim_t sim;

void sim_init(int locked, int legacy_unlock);

#endif
