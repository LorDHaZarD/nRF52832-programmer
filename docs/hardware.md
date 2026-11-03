# Hardware guide

This page covers what to buy, how to wire the programmer to an SD card and to
the target nRF52832, and how to prepare the SD card.

## Bill of materials

| Qty | Part | Notes |
|-----|------|-------|
| 1 | Adafruit Feather nRF52832 (product 3406) | the programmer ("host") |
| 1 | Adafruit Adalogger FeatherWing (product 2922), or any 3.3 V SPI microSD breakout | the Adalogger already routes SD CS to pin 11 |
| 1 | microSD card, 1 GB or larger | FAT16 or FAT32 |
| 1 | Momentary push button, normally open | start button |
| 3 | LEDs (for example green BUSY, green PASS, red FAIL) | |
| 3 | 330 Ω to 1 kΩ resistors | LED current limit, about 2 to 5 mA |
| 1 | Target cable | 10-pin 1.27 mm Cortex debug cable, Tag-Connect TC2030, or flying leads |
| 1 | USB cable | power and serial log |

No level shifters are needed: the Feather, the SD card and the target all run
at 3.3 V.

## Wiring

All pin numbers are Arduino pin numbers for the Feather nRF52832, which are the
same as its P0.xx GPIO numbers. Every pin can be moved in
[`config.h`](../firmware/nrf52_swd_programmer/config.h).

```
          Adafruit Feather nRF52832
        +---------------------------+
        |  12 SCK  ---------------- SD CLK
        |  13 MO   ---------------- SD DI  (CMD)
        |  14 MI   ---------------- SD DO  (DAT0)
        |  11      ---------------- SD CS  (DAT3)
        |                           |
        |  27      ---------------- target SWDCLK
        |  30 A6   ---------------- target SWDIO
        |  29 A5   ---------------- target nRESET (P0.21)
        |                           |
        |   7      ----o  o-------- GND          (start button)
        |  15      ---[R]--|>|----- GND          (LED1 BUSY)
        |  16      ---[R]--|>|----- GND          (LED2 PASS)
        |   2 A0   ---[R]--|>|----- GND          (LED3 FAIL)
        |                           |
        |  GND     ---------------- target GND, SD GND
        |  3V      ---------------- SD VCC  (and target VDD, see below)
        +---------------------------+
```

### Pins left free on purpose

| Pin | Why it is not used |
|-----|--------------------|
| 31 / A7 | connected to the battery voltage divider |
| 9, 10 | NFC antenna pins |
| 17, 19 | on-board red and blue LEDs, driven by the core |
| 20 | DFU button |
| 6, 8 | serial TX / RX to the USB bridge (debug log) |

### Target connector

The standard 10-pin 1.27 mm Arm Cortex debug connector:

| Pin | Signal | Connect to |
|-----|--------|------------|
| 1 | VTref | target 3.3 V (sense only, or Feather 3V if powering the target) |
| 2 | SWDIO | Feather 30 |
| 3 | GND | Feather GND |
| 4 | SWCLK | Feather 27 |
| 5 | GND | Feather GND |
| 6 | SWO | not used |
| 7 | key | not used |
| 8 | NC | not used |
| 9 | GNDDetect | GND |
| 10 | nRESET | Feather 29 |

On the nRF52832 itself, SWDCLK and SWDIO are dedicated pins, and nRESET is
P0.21. P0.21 only works as a reset pin once UICR.PSELRESET is programmed, which
this programmer does (`UICR_ADDR_2`/`UICR_ADDR_3`). On a blank chip the reset
pulse does nothing, which is harmless: the programmer connects without it.

### Powering the target

* **Target powered by its own supply (recommended for production):** connect
  only GND, SWDIO, SWCLK and nRESET. Make sure the target is powered before
  pressing the button.
* **Target powered by the Feather:** connect the Feather 3V pin to target VDD.
  The Feather's 3.3 V regulator (about 600 mA, shared with the Feather
  itself and the SD card); this covers an nRF52832
  module but not boards with large loads (motors, displays, radios at full
  power).

### Cable length

Keep the SWD wires short (under 15 to 20 cm) and run a ground wire next to
them. If connect or verify fails intermittently, increase
`SWD_HALF_PERIOD_DELAY` in `config.h` to slow SWCLK down.

## SD card preparation

1. Format the card as FAT16 or FAT32 (both work; most 1 GB cards ship as
   FAT16). exFAT does not work.
2. Copy the HEX file to the **root directory**.
3. The name must match `HEX_FILENAME` in `config.h` (default `FILENAME.HEX`)
   and be a short 8.3 name: up to 8 characters, a dot, up to 3 characters.
   Case does not matter.
4. The HEX file must be in ascending address order. Files written by
   `nrfjprog`, `mergehex`, nRF Connect, `objcopy` and `srec_cat` are.

Any UICR records in the file (addresses `0x10001000` to `0x10001FFF`) are
ignored; the UICR values come from `config.h`.
