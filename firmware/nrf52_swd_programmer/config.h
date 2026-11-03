// SPDX-License-Identifier: MIT
// Copyright (c) 2026 LorDHaZarD
/*
 * config.h - every user-adjustable setting of the nRF52832 SWD programmer.
 *
 * Host board : Adafruit Feather nRF52832 (Arduino core "Adafruit nRF52").
 * Pin numbers are Arduino pin numbers. On the Feather nRF52832 they are the
 * same as the P0.xx GPIO numbers printed on the board.
 */
#ifndef PROGRAMMER_CONFIG_H
#define PROGRAMMER_CONFIG_H

/* ------------------------------------------------------------------------ */
/* File on the SD card (8.3 name, root directory, case-insensitive)          */
/* ------------------------------------------------------------------------ */
#define HEX_FILENAME            "FILENAME.HEX"

/* ------------------------------------------------------------------------ */
/* SD card on the hardware SPI bus                                          */
/* MOSI, MISO and SCK are the Feather's SPI pins. Changing them also        */
/* re-routes the SPI peripheral (the nRF52 can use any pin for SPI).        */
/* Pin 11 is the SD chip select used by the Adalogger FeatherWing.          */
/* ------------------------------------------------------------------------ */
#define SD_PIN_MOSI             13
#define SD_PIN_MISO             14
#define SD_PIN_SCK              12
#define SD_PIN_CS               11
#define SD_SPI_INIT_HZ          250000UL    /* card identification, 100-400 kHz */
#define SD_SPI_FAST_HZ          8000000UL   /* after init, max 8 MHz on nRF52832 */

/* ------------------------------------------------------------------------ */
/* SWD connection to the target nRF52832                                    */
/* ------------------------------------------------------------------------ */
#define SWD_PIN_SWCLK           27
#define SWD_PIN_SWDIO           30
#define TARGET_PIN_RESET        29          /* target nRESET (P0.21 on target) */

/*
 * SWD clock speed. Each half period of SWCLK is padded with this many
 * delay-loop iterations (a few CPU cycles each at 64 MHz). Rough values,
 * check SWCLK with a scope if it matters:
 *   0  -> fastest, roughly 3 to 4 MHz (short wires only)
 *   2  -> roughly 2 MHz (default)
 *   10 -> roughly 1 MHz (long or noisy wiring)
 * The nRF52832 accepts up to 8 MHz. Lower it if connect or verify fails
 * intermittently.
 */
#define SWD_HALF_PERIOD_DELAY   2

#define SWD_WAIT_RETRIES        2000        /* retries when the target answers WAIT */
#define TARGET_RESET_PULSE_MS   10          /* nRESET low time */

/* ------------------------------------------------------------------------ */
/* Operator interface                                                       */
/* ------------------------------------------------------------------------ */
#define BUTTON_PIN              7           /* to GND, internal pull-up, active low */
#define BUTTON_DEBOUNCE_MS      20

#define LED_BUSY_PIN            15          /* LED1, on while programming (active high) */
#define LED_PASS_PIN            16          /* LED2, on after a good verify (active high) */
#define LED_FAIL_PIN            2           /* LED3 (A0), on after any error (active high) */

/* ------------------------------------------------------------------------ */
/* Debug log on the Feather's USB serial port                               */
/* ------------------------------------------------------------------------ */
#define DEBUG_SERIAL            1           /* 0 compiles all log output out */
#define DEBUG_BAUD              115200

/* ------------------------------------------------------------------------ */
/* Target memory                                                            */
/* ------------------------------------------------------------------------ */
#define PROGRAM_BLOCK_SIZE      512         /* bytes per program/verify block */

/*
 * Values written to the target's UICR after the flash is programmed.
 * UICR records inside the HEX file are ignored; only these are written.
 * Set UICR_ENTRY_COUNT to the number of entries used (max 8).
 */
#define UICR_ADDR_0             0x10001014UL    /* NRFFW[0]  bootloader address */
#define UICR_DATA_0             0x00074000UL
#define UICR_ADDR_1             0x10001018UL    /* NRFFW[1]  MBR params page */
#define UICR_DATA_1             0x0007E000UL
#define UICR_ADDR_2             0x10001200UL    /* PSELRESET[0] = P0.21 */
#define UICR_DATA_2             0x00000015UL
#define UICR_ADDR_3             0x10001204UL    /* PSELRESET[1] = P0.21 */
#define UICR_DATA_3             0x00000015UL
#define UICR_ENTRY_COUNT        4

/* Optional extra entries. Example for newer nRF52832 revisions that re-enable
 * access-port protection on every reset unless UICR.APPROTECT = HwDisabled:
 *   #define UICR_ADDR_4  0x10001208UL
 *   #define UICR_DATA_4  0x0000005AUL
 *   and set UICR_ENTRY_COUNT to 5.
 */

#endif /* PROGRAMMER_CONFIG_H */
