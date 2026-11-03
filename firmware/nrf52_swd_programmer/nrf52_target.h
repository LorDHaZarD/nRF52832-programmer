// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * nrf52_target.h - nRF52832 specific debug operations over SWD.
 */
#ifndef NRF52_TARGET_H
#define NRF52_TARGET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NRF52_IDCODE            0x2BA01477UL   /* SW-DP of the nRF52 series */
#define NRF52_CTRL_AP_IDR       0x02880000UL

/* CTRL-AP (access port 1), Nordic specific */
#define CTRL_AP                 1
#define CTRL_AP_RESET           0x000
#define CTRL_AP_ERASEALL        0x004
#define CTRL_AP_ERASEALLSTATUS  0x008
#define CTRL_AP_APPROTECTSTATUS 0x00C

/* NVMC */
#define TGT_NVMC_READY              0x4001E400UL
#define TGT_NVMC_CONFIG             0x4001E504UL
#define TGT_NVMC_CONFIG_REN         0
#define TGT_NVMC_CONFIG_WEN         1

/* FICR */
#define TGT_FICR_CODEPAGESIZE       0x10000010UL
#define TGT_FICR_CODESIZE           0x10000014UL
#define TGT_FICR_INFO_PART          0x10000100UL
#define TGT_FICR_INFO_VARIANT       0x10000104UL

/* UICR */
#define TGT_UICR_BASE               0x10001000UL
#define TGT_UICR_END                0x10002000UL   /* exclusive, whole UICR page */

/* Cortex-M4 debug */
#define TGT_DHCSR                   0xE000EDF0UL
#define TGT_DHCSR_KEY               0xA05F0000UL
#define TGT_DHCSR_C_DEBUGEN         (1UL << 0)
#define TGT_DHCSR_C_HALT            (1UL << 1)
#define TGT_DHCSR_S_HALT            (1UL << 17)
#define TGT_AIRCR                   0xE000ED0CUL
#define TGT_AIRCR_SYSRESETREQ       0x05FA0004UL

/* Result codes (0 = OK) */
#define TGT_OK                  0
#define TGT_ERR_SWD             1   /* no SWD response or wrong IDCODE */
#define TGT_ERR_CTRL_AP         2   /* CTRL-AP missing: not an nRF52? */
#define TGT_ERR_RECOVER         3   /* ERASEALL did not finish */
#define TGT_ERR_LOCKED          4   /* still protected after ERASEALL */
#define TGT_ERR_HALT            5
#define TGT_ERR_NVMC            6   /* NVMC never became ready */
#define TGT_ERR_ACCESS          7   /* SWD error during memory access */

typedef struct {
  uint32_t idcode;
  uint32_t part;          /* FICR.INFO.PART, 0x52832 */
  uint32_t variant;       /* FICR.INFO.VARIANT, ASCII e.g. "AAE0" */
  uint32_t page_size;     /* bytes */
  uint32_t flash_size;    /* bytes */
} nrf52_info_t;

/* Pulse nRESET, connect, power up the debug port, check the CTRL-AP. */
int nrf52_connect(nrf52_info_t *info);

/* Equivalent of "nrfjprog --recover": CTRL-AP ERASEALL (flash, UICR, RAM),
 * which also removes access-port protection. Leaves the core halted. */
int nrf52_recover(void);

/* Halt the core (TGT_DHCSR). */
int nrf52_halt(void);

/* Fill in part, variant, page and flash size from FICR (after recover). */
int nrf52_read_info(nrf52_info_t *info);

int nrf52_nvmc_config(uint32_t mode);
/* Program words to flash or UICR. NVMC must be in write mode. Words equal to
 * 0xFFFFFFFF are skipped (erased flash already holds that value). */
int nrf52_write_words(uint32_t addr, const uint32_t *words, uint32_t count);
int nrf52_read_words(uint32_t addr, uint32_t *words, uint32_t count);

/* Release the core, system reset (applies UICR), drop the debug power
 * request and float the SWD lines. */
void nrf52_reset_and_run(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF52_TARGET_H */
