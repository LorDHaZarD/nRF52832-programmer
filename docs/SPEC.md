# nRF52832 SWD Programmer: Specification (rev 2)

Rev 2 folds in the answers to [QUESTIONS.md](QUESTIONS.md). The biggest change
from rev 1: the programmer ("host") is now an **nRF52832 on an Adafruit Feather
nRF52832**, built as an **Arduino IDE sketch**, instead of an AVR.

---

## 1. Purpose

A stand-alone production programmer. The host reads an Intel HEX file from an
SD card, recovers the target nRF52832 (erase plus protection removal),
programs the file into flash over SWD in 512-byte blocks, writes fixed UICR
values, verifies, and shows the result on three LEDs. One button press
programs one target.

## 2. Requirements

| ID | Requirement | Source |
|----|-------------|--------|
| R1 | Source code in C (Arduino sketch, C modules plus a small C++ glue layer). | brief, Q1, Q2 |
| R2 | Host is an nRF52832 on an Adafruit Feather nRF52832. | Q1 |
| R3 | Programs the target nRF52832 flash over SWD. | brief |
| R4 | HEX file read from an SD card over SPI (MOSI, MISO, SCK, CS); FAT via Petit FatFs. | brief, Q3 |
| R5 | File name set by `#define HEX_FILENAME`, 8.3, root directory. | brief, Q12 |
| R6 | Every pin and clock rate set by `#define` (all in `config.h`). | brief, Q14 |
| R7 | Idle loop waits for a push button (active low). | brief |
| R8 | Before programming, recover the target (CTRL-AP ERASEALL), since protection may be set. | Q4, Q6 |
| R9 | Program in 512-byte blocks, skipping blocks that are entirely `0xFF`. | brief |
| R10 | HEX file is in ascending address order. | Q8 |
| R11 | Ignore UICR records in the HEX file; write the UICR values from `config.h`, address and data both `#define`s. | brief, Q9 |
| R12 | LED1 BUSY on while programming; off before verify. | brief |
| R13 | Verify every non-blank block against the HEX file; skip blank blocks. | brief, Q10 |
| R14 | LED2 PASS on after a good verify; LED3 FAIL on after any error. | brief, Q11 |
| R15 | Target nRESET is wired to the programmer. | Q7 |
| R16 | Debug messages on the Feather serial port at 115200 baud. | Q13 |
| R17 | 3.3 V everywhere, direct wiring. | Q5 |

## 3. Hardware

### 3.1 Wiring (defaults in `config.h`)

| Signal | Feather pin | Target / device |
|--------|-------------|-----------------|
| SD SCK | 12 (SCK) | SD CLK |
| SD MOSI | 13 (MO) | SD DI / CMD |
| SD MISO | 14 (MI) | SD DO / DAT0 |
| SD CS | 11 | SD CS / DAT3 (same pin as the Adalogger FeatherWing) |
| SWCLK | 27 | target SWDCLK |
| SWDIO | 30 (A6) | target SWDIO |
| nRESET | 29 (A5) | target P0.21 / nRESET |
| Button | 7 | push button to GND (internal pull-up) |
| LED1 BUSY | 15 | LED + resistor to GND (active high) |
| LED2 PASS | 16 | LED + resistor to GND (active high) |
| LED3 FAIL | 2 (A0) | LED + resistor to GND (active high) |
| Serial | USB (CP2104) | debug log, 115200 baud |
| GND, 3V | GND, 3V | target GND (and 3.3 V if the target is not self-powered) |

Pins avoided on purpose: 31/A7 (battery divider), 9/10 (NFC), 17/19 (on-board
LEDs used by the core), 20 (DFU), 6/8 (serial).

### 3.2 Levels and power

Both chips and the SD card run at 3.3 V, so all lines are wired directly.
The target shares ground with the Feather. The firmware does not switch target
power.

## 4. Clocks and timing (`config.h`)

| Define | Default | Meaning |
|--------|---------|---------|
| `SD_SPI_INIT_HZ` | 250 kHz | SPI clock during card identification |
| `SD_SPI_FAST_HZ` | 8 MHz | SPI clock afterwards (nRF52832 maximum) |
| `SWD_HALF_PERIOD_DELAY` | 2 | Delay-loop count per SWCLK half period (roughly 2 MHz SWCLK) |
| `SWD_WAIT_RETRIES` | 2000 | Retries on an SWD WAIT response |
| `TARGET_RESET_PULSE_MS` | 10 | nRESET low time |
| `BUTTON_DEBOUNCE_MS` | 20 | Button must read low this long |
| `DEBUG_BAUD` | 115200 | Serial log speed |

## 5. Software

### 5.1 Files

```
firmware/nrf52_swd_programmer/        Arduino sketch folder
  nrf52_swd_programmer.ino   setup/loop, button, LEDs, serial log, time services
  config.h                   every #define the user may change
  programmer.c/.h            one programming cycle (state machine below)
  nrf52_target.c/.h          connect, recover, halt, NVMC write, read-back, reset
  swd.c/.h                   bit-banged SWD: packets, WAIT/FAULT, DP/AP, MEM-AP
  swd_port.h                 direct GPIO register access for SWCLK/SWDIO/nRESET
  ihex.c/.h                  streaming Intel HEX parser
  hexsrc.h, hexsrc_sd.cpp    SD card SPI driver + Petit FatFs disk layer + file source
  log.h                      LOG() macro, compiled out with DEBUG_SERIAL 0
  pff.c/.h, pffconf.h, diskio.h   Petit FatFs R0.03a (ChaN), read-only
test/                        PC test: same C modules against a simulated target
```

### 5.2 Cycle

```
IDLE (LED states from last cycle kept)
  button low for 20 ms
START: PASS off, FAIL off, BUSY on
  1. mount SD, open HEX_FILENAME            fail -> SD / FILE error (target untouched)
  2. pulse nRESET, SWD connect, IDCODE      fail -> SWD error (3 attempts)
     power up debug, check CTRL-AP IDR      fail -> NOT_NRF52
  3. recover: CTRL-AP ERASEALL, wait        fail -> RECOVER
     if APPROTECTSTATUS still on: CTRL-AP reset, re-attach, check again  -> LOCKED
     halt core
  4. read FICR (part, variant, flash size)
  5. NVMC write mode; PROGRAM pass over the file
  6. NVMC read mode; write UICR table
  7. BUSY off
  8. VERIFY pass over the file, then read back UICR table
  9. release core, SYSRESETREQ, debug power down, float SWD, pulse nRESET
  PASS on            (any error: FAIL on, details on serial)
  wait for button release -> IDLE
```

### 5.3 SWD

* Bit-banged with direct `NRF_P0` register writes. Host changes SWDIO while
  SWCLK is high, drops SWCLK, target samples on the rising edge. Target data is
  sampled while SWCLK is low (same scheme as CMSIS-DAP).
* Connect: 56 ones, JTAG-to-SWD `0xE79E`, 56 ones, 8 idle, read IDCODE
  (`0x2BA01477`), clear sticky errors.
* WAIT is retried up to `SWD_WAIT_RETRIES`, FAULT clears sticky errors through
  ABORT and fails the operation. No ACK means nothing is connected.
* AHB-AP CSW: 32-bit, single auto-increment. Block reads use posted reads
  (each AP read returns the previous result, RDBUFF for the last). 512-byte
  aligned blocks never cross the 1 KB TAR wrap.

### 5.4 Recover (answer to Q4 and Q6)

"nRF recover" is the CTRL-AP (AP 1) ERASEALL: it erases flash, UICR and RAM
and lifts access-port protection, which works even when APPROTECT is set.
It replaces any NVMC erase. Older silicon reports protection lifted straight
away; some revisions need a CTRL-AP reset first, which the firmware does
only when APPROTECTSTATUS still says protected.

Note for newer nRF52832 silicon revisions (Rev 3 and later): these re-enable
protection on every reset unless UICR.APPROTECT (`0x10001208`) holds
`0x5A` (HwDisabled) and the application also writes APPROTECT.DISABLE. The
programmer does not need this (it always recovers first), but if the
programmed board must stay debuggable, add that UICR entry in `config.h`
(an example is in the file).

### 5.5 Programming

* NVMC CONFIG = write enable for the whole pass. Each word: write TAR and DRW,
  then poll NVMC.READY. Words equal to `0xFFFFFFFF` are skipped (already
  erased).
* HEX stream: types 00, 01, 02, 04 handled; 03 and 05 ignored; checksums
  checked; a missing EOF record is an error.
* One 512-byte buffer for the block at `addr & ~0x1FF`, pre-filled with `0xFF`.
  When data moves to a higher block, the buffer is flushed. A lower block is
  an error (R10). Data at `0x10001000`-`0x10001FFF` (UICR) is counted and
  ignored (R11). Any other address at or above the flash size is an error.
* Flush: all `0xFF` = skipped; otherwise written (program pass) or read back
  and compared (verify pass).
* A file with no flash data at all is an error (protects against the wrong file).

### 5.6 UICR

`UICR_ADDR_n` / `UICR_DATA_n`, n = 0..7, `UICR_ENTRY_COUNT` entries
(default 4: NRFFW[0] = `0x74000`, NRFFW[1] = `0x7E000`, PSELRESET[0] and [1]
= 21). Written after the flash with NVMC in write mode, then read back during
verify.

### 5.7 SD card and FAT

* SPI mode: CMD0, CMD8, ACMD41 (HCS), CMD58, CMD16 for byte-addressed cards,
  CMD17 single block reads. SDSC, SDHC/SDXC and MMC.
* Petit FatFs reads FAT12, FAT16 and FAT32 (1 GB cards are often FAT16 out of
  the box). Root directory, 8.3 name, upper-cased before lookup.
* One cached 512-byte sector, so the FAT and directory lookups do not re-read
  the card.

### 5.8 Errors

| Serial message | Cause |
|----------------|-------|
| SD card error | no card, card not answering, no FAT volume |
| HEX file not found | `HEX_FILENAME` not in root directory |
| no SWD connection to target | no ACK, wrong IDCODE, debug power-up failed |
| target is not an nRF52 | CTRL-AP missing |
| recover (ERASEALL) failed | ERASEALL did not finish in 3 s |
| target still protected after recover | APPROTECTSTATUS stays on |
| target core did not halt | DHCSR S_HALT not set |
| HEX file format error | bad character, length, checksum, record type, missing EOF |
| HEX file addresses not ascending | a record goes back to an earlier block |
| HEX data outside target flash | address beyond flash and not UICR |
| HEX file has no flash data | nothing to program |
| flash write failed | SWD error or NVMC not ready |
| UICR write failed | same, during UICR |
| flash verify failed | first bad address, expected and read value are logged |
| UICR verify failed | same, for UICR |

All errors turn on LED3 FAIL. LED1 is off and LED2 stays off.

## 6. Timing estimate (Q15)

Measured with the PC simulation (it counts SWD clock cycles exactly) for a
typical merged image: MBR + S132-sized SoftDevice + 40 KB application + 22 KB
bootloader = 210 KB, 420 blocks.

| SWCLK | Program (incl. UICR) | Verify | Whole cycle |
|-------|----------------------|--------|-------------|
| 1 MHz | 21 to 25 s | 2.6 s | about 25 to 28 s |
| 2 MHz (default) | 11 to 14 s | 1.3 s | about 13 to 16 s |
| 4 MHz | 5 to 9 s | 0.7 s | about 7 to 10 s |

Plus about 0.1 to 0.3 s for ERASEALL and about 0.5 s to read the HEX file
twice from the card. Program time scales with the amount of non-blank data;
a 100 KB image takes about half. The program step is dominated by the
per-word NVMC.READY poll (5 SWD transfers per word). If this ever needs to be
faster, the next step is a small flash loader in target RAM, which would cut
programming roughly in half to a third; it is not included because it cannot
be proven without hardware.

The actual SWCLK rate depends on the bit-bang loop and should be checked once
with a scope.

## 7. Build and test

* Arduino IDE: install the "Adafruit nRF52" board package, select
  "Adafruit Feather nRF52832", open `firmware/nrf52_swd_programmer`, upload.
* PC test: `cd test && make` builds the C modules with gcc against a
  cycle-level model of the target's SWD port, CTRL-AP, AHB-AP and NVMC, and
  runs programming, verify and error cases (see `test/test_main.c`).
* Bench test to do on hardware: blank target, programmed target, protected
  target, no card, no file, corrupted HEX, verify failure (for example pull
  SWDIO during verify), and measure SWCLK.

## 8. Out of scope

Other Nordic parts, a RAM flash loader, writing to the SD card, long file
names, BLE, programming several targets at once.
