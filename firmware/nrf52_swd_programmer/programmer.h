// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * programmer.h - one complete programming cycle:
 * open HEX file, connect, recover, program, write UICR, verify, reset.
 */
#ifndef PROGRAMMER_H
#define PROGRAMMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PROG_OK = 0,
  PROG_E_SD,            /* SD card missing / not answering / no FAT volume */
  PROG_E_FILE,          /* HEX file not found */
  PROG_E_SWD,           /* no SWD response or wrong IDCODE */
  PROG_E_NOT_NRF52,     /* CTRL-AP missing */
  PROG_E_RECOVER,       /* CTRL-AP ERASEALL failed or timed out */
  PROG_E_LOCKED,        /* still protected after ERASEALL */
  PROG_E_HALT,          /* core did not halt */
  PROG_E_HEX,           /* syntax or checksum error in the HEX file */
  PROG_E_HEX_ORDER,     /* addresses not ascending */
  PROG_E_RANGE,         /* data outside flash (and outside UICR) */
  PROG_E_EMPTY,         /* HEX file holds no data for flash */
  PROG_E_WRITE,         /* SWD or NVMC error while writing flash */
  PROG_E_UICR,          /* SWD or NVMC error while writing UICR */
  PROG_E_VERIFY,        /* flash content differs from the HEX file */
  PROG_E_UICR_VERIFY    /* UICR content differs from config.h */
} prog_err_t;

typedef struct {
  uint32_t blocks_written;
  uint32_t blocks_skipped;     /* blocks that were all 0xFF */
  uint32_t blocks_verified;
  uint32_t uicr_bytes_ignored; /* UICR data found in the HEX file (not written) */
  uint32_t ms_recover, ms_program, ms_verify, ms_total;
  uint32_t fail_addr;          /* first bad address for RANGE / VERIFY errors */
  uint32_t fail_expected, fail_actual;
} prog_stats_t;

extern prog_stats_t prog_stats;

prog_err_t  prog_run(void);
const char *prog_err_str(prog_err_t err);

/* Implemented by the sketch: LED1 "busy" control, so the programmer can turn
 * it off between programming and verify as the brief requires. */
void ui_busy_led(int on);

#ifdef __cplusplus
}
#endif

#endif /* PROGRAMMER_H */
