// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * hexsrc_sd.cpp - SD card (SPI mode) driver, Petit FatFs disk interface and
 * the HEX file source used by programmer.c.
 *
 * Supports SDSC (byte addressed, typical for 1 GB cards), SDHC/SDXC (block
 * addressed) and old MMC cards. Only single block reads (CMD17) are used.
 */
#ifndef PROGRAMMER_SIM

#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include "config.h"
#include "hexsrc.h"
#include "log.h"

extern "C" {
#include "pff.h"
#include "diskio.h"
}

/* Own SPI instance so the pins in config.h are used. SPIM2 is the
 * peripheral the core's default SPI object uses; that object is never begun. */
static SPIClass sdSPI(NRF_SPIM2, SD_PIN_MISO, SD_PIN_SCK, SD_PIN_MOSI);
static uint32_t spi_hz = SD_SPI_INIT_HZ;

/* Card type flags */
#define CT_MMC    0x01
#define CT_SD1    0x02
#define CT_SD2    0x04
#define CT_BLOCK  0x08     /* block addressing (SDHC/SDXC) */

static uint8_t card_type;

/* One cached sector: Petit FatFs reads small pieces (FAT entries, directory
 * entries) many times from the same sector. */
static uint8_t  sector_buf[512];
static uint32_t sector_cached = 0xFFFFFFFFUL;

static FATFS fatfs;

/* ------------------------------------------------------------------------ */
/* SPI helpers                                                              */
/* ------------------------------------------------------------------------ */

static inline uint8_t xchg(uint8_t b) { return sdSPI.transfer(b); }

static void cs_high(void)
{
  digitalWrite(SD_PIN_CS, HIGH);
  xchg(0xFF);                          /* release MISO (card needs one clock) */
  sdSPI.endTransaction();
}

static void cs_low(void)
{
  sdSPI.beginTransaction(SPISettings(spi_hz, MSBFIRST, SPI_MODE0));
  digitalWrite(SD_PIN_CS, LOW);
  xchg(0xFF);
}

static int wait_ready(uint32_t ms)
{
  uint32_t t0 = millis();
  do {
    if (xchg(0xFF) == 0xFF) return 1;
  } while (millis() - t0 < ms);
  return 0;
}

/* Send a command and return R1. Caller holds CS low. */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
  uint8_t r1, n, crc = 0x01;

  if (cmd & 0x80) {                    /* ACMD: CMD55 first */
    cmd &= 0x7F;
    r1 = send_cmd(55, 0);
    if (r1 > 1) return r1;
  }
  if (cmd != 0 && !wait_ready(500)) return 0xFF;

  if (cmd == 0) crc = 0x95;            /* valid CRC for CMD0(0) */
  if (cmd == 8) crc = 0x87;            /* valid CRC for CMD8(0x1AA) */

  xchg(0x40 | cmd);
  xchg((uint8_t)(arg >> 24));
  xchg((uint8_t)(arg >> 16));
  xchg((uint8_t)(arg >> 8));
  xchg((uint8_t)arg);
  xchg(crc);

  n = 10;
  do {
    r1 = xchg(0xFF);
  } while ((r1 & 0x80) && --n);
  return r1;
}

#define ACMD41 (0x80 | 41)

/* ------------------------------------------------------------------------ */
/* Petit FatFs disk interface                                               */
/* ------------------------------------------------------------------------ */

extern "C" DSTATUS disk_initialize(void)
{
  uint8_t ocr[4], ty = 0, i;
  uint32_t t0;

  sector_cached = 0xFFFFFFFFUL;
  pinMode(SD_PIN_CS, OUTPUT);
  digitalWrite(SD_PIN_CS, HIGH);
  sdSPI.begin();
  spi_hz = SD_SPI_INIT_HZ;

  /* >= 74 clocks with CS high */
  sdSPI.beginTransaction(SPISettings(spi_hz, MSBFIRST, SPI_MODE0));
  for (i = 0; i < 10; i++) xchg(0xFF);
  sdSPI.endTransaction();

  cs_low();
  for (i = 0; i < 10 && send_cmd(0, 0) != 1; i++) { }
  if (i < 10) {
    t0 = millis();
    if (send_cmd(8, 0x1AA) == 1) {                     /* SD v2 */
      for (i = 0; i < 4; i++) ocr[i] = xchg(0xFF);
      if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
        while (send_cmd(ACMD41, 1UL << 30) != 0 && millis() - t0 < 1000) { }
        if (millis() - t0 < 1000 && send_cmd(58, 0) == 0) {
          for (i = 0; i < 4; i++) ocr[i] = xchg(0xFF);
          ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
        }
      }
    } else {                                           /* SD v1 or MMC */
      uint8_t cmd;
      if (send_cmd(ACMD41, 0) <= 1) { ty = CT_SD1; cmd = ACMD41; }
      else                          { ty = CT_MMC; cmd = 1; }
      while (send_cmd(cmd, 0) != 0 && millis() - t0 < 1000) { }
      if (millis() - t0 >= 1000) ty = 0;
    }
    if (ty && !(ty & CT_BLOCK) && send_cmd(16, 512) != 0) ty = 0;  /* 512 byte blocks */
  }
  card_type = ty;
  cs_high();

  if (!ty) {
    LOG("SD card not found or not answering\r\n");
    return STA_NOINIT;
  }
  spi_hz = SD_SPI_FAST_HZ;
  LOG("SD card type %s\r\n", (ty & CT_BLOCK) ? "SDHC/SDXC" : (ty & CT_SD2) ? "SDSC v2" :
                             (ty & CT_SD1) ? "SDSC v1" : "MMC");
  return 0;
}

static int read_sector(uint32_t sector)
{
  uint8_t token = 0xFF;
  uint32_t t0, addr;
  int ok = 0;

  if (sector == sector_cached) return 1;
  sector_cached = 0xFFFFFFFFUL;
  addr = (card_type & CT_BLOCK) ? sector : sector * 512;  /* byte address on SDSC */

  cs_low();
  if (send_cmd(17, addr) == 0) {
    t0 = millis();
    do {
      token = xchg(0xFF);
    } while (token == 0xFF && millis() - t0 < 200);
    if (token == 0xFE) {
      sdSPI.transfer(NULL, sector_buf, 512);            /* clocks out 0xFF */
      xchg(0xFF);                                       /* CRC, ignored */
      xchg(0xFF);
      ok = 1;
    }
  }
  cs_high();
  if (ok) sector_cached = sector;
  return ok;
}

extern "C" DRESULT disk_readp(BYTE *buff, DWORD sector, UINT offset, UINT count)
{
  if (!card_type) return RES_NOTRDY;
  if (offset + count > 512) return RES_PARERR;
  if (!read_sector(sector)) return RES_ERROR;
  if (buff) memcpy(buff, &sector_buf[offset], count);
  return RES_OK;
}

extern "C" DRESULT disk_writep(const BYTE *buff, DWORD sc)
{
  (void)buff;
  (void)sc;
  return RES_ERROR;                                      /* read only */
}

/* ------------------------------------------------------------------------ */
/* HEX file source                                                          */
/* ------------------------------------------------------------------------ */

extern "C" int hexsrc_open(void)
{
  char name[13];
  size_t i;
  FRESULT fr;

  if (disk_initialize() != 0) return HEXSRC_ERR_CARD;
  fr = pf_mount(&fatfs);
  if (fr != FR_OK) {
    LOG("pf_mount failed (%d)\r\n", (int)fr);
    return fr == FR_NO_FILESYSTEM ? HEXSRC_ERR_FS : HEXSRC_ERR_CARD;
  }

  /* FAT short names are stored in upper case. */
  for (i = 0; i < sizeof(name) - 1 && HEX_FILENAME[i]; i++) {
    char c = HEX_FILENAME[i];
    name[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  name[i] = 0;

  fr = pf_open(name);
  if (fr != FR_OK) {
    LOG("pf_open(%s) failed (%d)\r\n", name, (int)fr);
    return fr == FR_NO_FILE ? HEXSRC_ERR_FILE : HEXSRC_ERR_READ;
  }
  return HEXSRC_OK;
}

extern "C" int hexsrc_read(uint8_t *buf, uint16_t n, uint16_t *br)
{
  UINT got = 0;
  FRESULT fr = pf_read(buf, n, &got);
  *br = (uint16_t)got;
  return fr == FR_OK ? HEXSRC_OK : HEXSRC_ERR_READ;
}

extern "C" uint32_t hexsrc_size(void)
{
  return fatfs.fsize;
}

#endif /* PROGRAMMER_SIM */
