// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * nrf52_target.c - nRF52832 debug operations: connect, recover (CTRL-AP
 * ERASEALL), halt, NVMC programming, read-back and reset.
 */
#include "nrf52_target.h"
#include "swd.h"
#include "swd_port.h"
#include "log.h"

#define ERASEALL_TIMEOUT_MS   3000   /* datasheet: ~200 ms typical */
#define NVMC_TIMEOUT_MS       20     /* one word write is < 0.1 ms */
#define HALT_TIMEOUT_MS       100

static void pulse_reset(uint32_t settle_ms)
{
  target_reset_assert();
  port_delay_ms(TARGET_RESET_PULSE_MS);
  target_reset_release();
  port_delay_ms(settle_ms);
}

static int attach(uint32_t *idcode)
{
  uint32_t id = 0;
  int rc = swd_connect(&id);
  if (idcode) *idcode = id;
  if (rc != SWD_OK) return TGT_ERR_SWD;
  if (id != NRF52_IDCODE) return TGT_ERR_SWD;
  if (swd_power_up() != SWD_OK) return TGT_ERR_SWD;
  return TGT_OK;
}

int nrf52_connect(nrf52_info_t *info)
{
  uint32_t id = 0, idr = 0;
  int attempt, rc = TGT_ERR_SWD;

  for (attempt = 0; attempt < 3; attempt++) {
    /* The pin reset only works once UICR.PSELRESET is programmed; on a blank
     * chip it does nothing, which is harmless. */
    pulse_reset(10 + 50 * (uint32_t)attempt);
    rc = attach(&id);
    if (rc == TGT_OK) break;
    LOG("SWD connect attempt %d failed (IDCODE 0x%08lX)\r\n", attempt + 1, (unsigned long)id);
  }
  if (info) info->idcode = id;
  if (rc != TGT_OK) return rc;
  LOG("SWD connected, IDCODE 0x%08lX\r\n", (unsigned long)id);

  if (swd_ap_read(CTRL_AP, AP_IDR, &idr) != SWD_OK || idr != NRF52_CTRL_AP_IDR) {
    LOG("CTRL-AP IDR 0x%08lX, expected 0x%08lX\r\n", (unsigned long)idr,
        (unsigned long)NRF52_CTRL_AP_IDR);
    return TGT_ERR_CTRL_AP;
  }
  return TGT_OK;
}

static int approtect_disabled(int *disabled)
{
  uint32_t st = 0;
  if (swd_ap_read(CTRL_AP, CTRL_AP_APPROTECTSTATUS, &st) != SWD_OK) return TGT_ERR_SWD;
  *disabled = (int)(st & 1);
  return TGT_OK;
}

int nrf52_recover(void)
{
  uint32_t t0, st = 1;
  int disabled = 0;

  if (approtect_disabled(&disabled) == TGT_OK) {
    LOG("Access port protection %s before recover\r\n", disabled ? "off" : "ON");
  }

  LOG("Recover: CTRL-AP ERASEALL\r\n");
  if (swd_ap_write(CTRL_AP, CTRL_AP_ERASEALL, 1) != SWD_OK) return TGT_ERR_RECOVER;
  port_delay_ms(10);
  t0 = port_millis();
  for (;;) {
    if (swd_ap_read(CTRL_AP, CTRL_AP_ERASEALLSTATUS, &st) != SWD_OK) return TGT_ERR_RECOVER;
    if (st == 0) break;
    if (port_millis() - t0 > ERASEALL_TIMEOUT_MS) return TGT_ERR_RECOVER;
    port_delay_ms(5);
  }
  swd_ap_write(CTRL_AP, CTRL_AP_ERASEALL, 0);
  LOG("ERASEALL done in %lu ms\r\n", (unsigned long)(port_millis() - t0 + 10));

  if (approtect_disabled(&disabled) != TGT_OK) return TGT_ERR_SWD;
  if (!disabled) {
    /* Some silicon revisions only lift protection after a reset. */
    LOG("Protection still reported, CTRL-AP reset\r\n");
    swd_ap_write(CTRL_AP, CTRL_AP_RESET, 1);
    port_delay_ms(5);
    swd_ap_write(CTRL_AP, CTRL_AP_RESET, 0);
    port_delay_ms(10);
    if (attach(0) != TGT_OK) return TGT_ERR_SWD;
    if (approtect_disabled(&disabled) != TGT_OK) return TGT_ERR_SWD;
    if (!disabled) return TGT_ERR_LOCKED;
  }

  if (swd_mem_init() != SWD_OK) return TGT_ERR_ACCESS;
  return nrf52_halt();
}

int nrf52_halt(void)
{
  uint32_t t0, dhcsr = 0;
  if (swd_mem_write32(TGT_DHCSR, TGT_DHCSR_KEY | TGT_DHCSR_C_HALT | TGT_DHCSR_C_DEBUGEN) != SWD_OK) {
    return TGT_ERR_HALT;
  }
  t0 = port_millis();
  for (;;) {
    if (swd_mem_read32(TGT_DHCSR, &dhcsr) != SWD_OK) return TGT_ERR_HALT;
    if (dhcsr & TGT_DHCSR_S_HALT) return TGT_OK;
    if (port_millis() - t0 > HALT_TIMEOUT_MS) return TGT_ERR_HALT;
  }
}

int nrf52_read_info(nrf52_info_t *info)
{
  uint32_t pagesize = 0, codesize = 0;
  if (swd_mem_read32(TGT_FICR_CODEPAGESIZE, &pagesize) != SWD_OK ||
      swd_mem_read32(TGT_FICR_CODESIZE, &codesize) != SWD_OK ||
      swd_mem_read32(TGT_FICR_INFO_PART, &info->part) != SWD_OK ||
      swd_mem_read32(TGT_FICR_INFO_VARIANT, &info->variant) != SWD_OK) {
    return TGT_ERR_ACCESS;
  }
  /* Fall back to the nRF52832-QFAA layout if FICR looks wrong. */
  if (pagesize == 0 || pagesize > 0x10000UL || codesize == 0 || codesize > 1024) {
    pagesize = 4096;
    codesize = 128;
  }
  info->page_size  = pagesize;
  info->flash_size = pagesize * codesize;
  return TGT_OK;
}

static int nvmc_wait_ready(void)
{
  uint32_t t0 = port_millis(), ready = 0;
  for (;;) {
    if (swd_mem_read32(TGT_NVMC_READY, &ready) != SWD_OK) return TGT_ERR_ACCESS;
    if (ready & 1) return TGT_OK;
    if (port_millis() - t0 > NVMC_TIMEOUT_MS) return TGT_ERR_NVMC;
  }
}

int nrf52_nvmc_config(uint32_t mode)
{
  int rc = nvmc_wait_ready();
  if (rc) return rc;
  if (swd_mem_write32(TGT_NVMC_CONFIG, mode) != SWD_OK) return TGT_ERR_ACCESS;
  return nvmc_wait_ready();
}

int nrf52_write_words(uint32_t addr, const uint32_t *words, uint32_t count)
{
  uint32_t i;
  for (i = 0; i < count; i++) {
    int rc;
    if (words[i] == 0xFFFFFFFFUL) continue;
    if (swd_mem_write32(addr + 4 * i, words[i]) != SWD_OK) return TGT_ERR_ACCESS;
    rc = nvmc_wait_ready();
    if (rc) return rc;
  }
  return TGT_OK;
}

int nrf52_read_words(uint32_t addr, uint32_t *words, uint32_t count)
{
  return swd_mem_read_block(addr, words, count) == SWD_OK ? TGT_OK : TGT_ERR_ACCESS;
}

void nrf52_reset_and_run(void)
{
  /* Clear C_DEBUGEN (also releases the halt), then request a system reset.
   * The reset loads the new UICR values; the write may not be acknowledged. */
  swd_mem_write32(TGT_DHCSR, TGT_DHCSR_KEY);
  swd_mem_write32(TGT_AIRCR, TGT_AIRCR_SYSRESETREQ);
  port_delay_ms(5);
  swd_power_down();
  swd_release();
  /* PSELRESET is active now, so a pin reset also takes the chip out of
   * debug interface mode and starts the new firmware cleanly. */
  pulse_reset(5);
}
