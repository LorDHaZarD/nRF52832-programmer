# nRF52832 SWD Programmer on AVR: Specification (draft 1)

Status: draft, waiting on answers in [QUESTIONS.md](QUESTIONS.md). Every item marked
**[Q n]** depends on the answer to question n. Where a question is open, the
spec states the default the implementation will use if the answer is "use default".

---

## 1. Purpose

A stand-alone production programmer. An Atmel AVR ("host") reads an Intel HEX file
from an SD card and programs it into the flash of a Nordic nRF52832 ("target") over
SWD, writes four fixed UICR values, verifies, and signals the result on two LEDs.
One button press programs one target.

## 2. Requirements (from the brief)

| ID | Requirement |
|----|-------------|
| R1 | Source code in C, runs on an Atmel AVR. |
| R2 | Programs nRF52832 flash over SWD. |
| R3 | HEX file read from a 1 GB SD card over the AVR's hardware SPI (MOSI, MISO, SCK, SS). |
| R4 | SD card holds a FAT32 file system; the file name is set by `#define` (default `FILENAME.HEX`). |
| R5 | All clock rates and every I/O pin are set by `#define`. |
| R6 | Idle loop waits for a push button (active low). |
| R7 | Flash is programmed in 512-byte blocks; a block that is entirely `0xFF` is skipped. |
| R8 | After flash, write UICR: `0x10001014 = 0x00074000`, `0x10001018 = 0x0007E000`, `0x10001200 = 0x00000015`, `0x10001204 = 0x00000015`. |
| R9 | LED1 (active high, "BUSY") is on while programming. |
| R10 | After programming, LED1 off, then verify flash against the HEX file. |
| R11 | If verify passes, LED2 (active high, "PASS") turns on; return to idle. |
| R12 | LED pins set by `#define`. |

For reference, the UICR values mean:

* `0x10001014` UICR.NRFFW[0] (BOOTLOADERADDR) = `0x74000`, the bootloader start.
* `0x10001018` UICR.NRFFW[1] (MBR params page) = `0x7E000`.
* `0x10001200/0x1204` UICR.PSELRESET[0/1] = 21, so P0.21 is the pin reset. Both
  registers must hold the same value or the setting is ignored.

These are the standard values for a Nordic Secure DFU bootloader layout on the
nRF52832, so the HEX file is most likely a merged SoftDevice + application +
bootloader image (relevant to [Q 9]).

## 3. Hardware

### 3.1 Host

Default target part **[Q 1]**: ATmega328P at 16 MHz, 5 V (Arduino Uno / Nano class).
The design must fit in 2 KB of SRAM, which drives the choices below. A larger part
(ATmega644P/1284P/2560) only needs pin and register `#define` changes.

### 3.2 Connections (defaults, all in `config.h`)

| Signal | Default AVR pin (ATmega328P) | Notes |
|--------|------------------------------|-------|
| SD MOSI | PB3 | fixed by hardware SPI |
| SD MISO | PB4 | fixed by hardware SPI |
| SD SCK | PB5 | fixed by hardware SPI |
| SD CS (SS) | PB2 | Hardware SS; kept as an output so SPI stays master. |
| SWCLK | PD2 | bit-banged |
| SWDIO | PD3 | bit-banged, bidirectional |
| Target nRESET | PD4 (optional, [Q 7]) | open-drain style: driven low or released |
| Button | PD5 | input, internal pull-up on, active low |
| LED1 BUSY | PD6 | active high |
| LED2 PASS | PD7 | active high |
| Debug UART TX | PD1 | optional, [Q 13] |

The brief's pin names are honoured via `#define`: each signal has a `_PORT`, `_DDR`,
`_PIN` and `_BIT` define so moving a pin is a one-line change.

### 3.3 Voltage levels **[Q 5]**

The nRF52832 runs at 1.7 V to 3.6 V and its pins are **not 5 V tolerant**. SD cards
are 3.3 V only. If the AVR runs at 5 V, SWCLK, SWDIO, nRESET, SD CS, SD MOSI and SD
SCK need level shifting, and SWDIO needs a bidirectional shifter (or a
series-resistor + open-drain scheme). Default assumption: the AVR runs at 3.3 V
(e.g. 8 MHz 3.3 V Pro Mini) so no shifters are needed. The firmware is the same
either way; this is a hardware note.

### 3.4 Target power

Default assumption: the target board is powered by its own supply or by the
programmer's 3.3 V rail, and shares ground with the host. The firmware does not
switch target power.

## 4. Clocks (all `#define`)

| Define | Default | Meaning |
|--------|---------|---------|
| `F_CPU` | `16000000UL` | AVR core clock. |
| `SD_SPI_INIT_DIV` | 128 | SPI divisor during card init (must give 100 to 400 kHz). |
| `SD_SPI_FAST_DIV` | 2 | SPI divisor after init (8 MHz at 16 MHz F_CPU). |
| `SWD_HALF_PERIOD_US` | 0 | Extra delay per SWCLK half period; 0 = as fast as the bit-bang loop runs (roughly 1 MHz at 16 MHz). |
| `DEBOUNCE_MS` | 20 | Button debounce time. |

## 5. Software architecture

```
src/
  config.h       all #defines: pins, clocks, file name, UICR table, timeouts
  main.c         idle loop, state machine, LED handling
  swd.c/.h       bit-banged SWD PHY + DP/AP layer (ACK, WAIT retry, parity)
  nrf52.c/.h     connect, halt, NVMC erase/write, UICR write, read-back, reset
  ihex.c/.h      streaming Intel HEX parser -> 512-byte block assembler
  sd.c/.h        SD card SPI driver (SDSC + SDHC, CMD0/8/55/41/58/17)
  pff/           Petit FatFs (ChaN) for FAT read, or alternative [Q 3]
Makefile         avr-gcc build, avrdude flash target [Q 2]
```

### 5.1 State machine

```
IDLE --button pressed (debounced, low)--> START
START: LED1 on, LED2 off
  mount SD, open file           -> fail: ERROR(E_SD / E_FILE)
  SWD connect, read IDCODE      -> fail: ERROR(E_SWD)
  halt core, erase              -> fail: ERROR(E_ERASE)
  PROGRAM pass (stream HEX)     -> fail: ERROR(E_HEX / E_WRITE)
  write UICR                    -> fail: ERROR(E_UICR)
LED1 off
  VERIFY pass (re-stream HEX, read target, compare; also UICR) -> fail: ERROR(E_VERIFY)
  reset target                  [Q 7]
  LED2 on
  wait for button release -> IDLE
ERROR(code): LED indication per [Q 11] -> IDLE
```

### 5.2 SWD layer

* Bit-banged on two GPIOs. The host changes SWDIO while SWCLK is low and the
  target samples it on the rising edge; the target changes SWDIO after the rising
  edge and the host samples it while SWCLK is high, before the next falling edge.
* Connect sequence: >= 50 clocks with SWDIO high, JTAG-to-SWD sequence `0xE79E`,
  >= 50 clocks high, >= 2 idle clocks, read DP IDCODE. Expected `0x2BA01477`.
* Packet: 8-bit request (start, APnDP, RnW, A[2:3], parity, stop, park), turnaround,
  3-bit ACK, data + parity, turnaround. ACK `WAIT` is retried up to a limit;
  `FAULT` clears sticky errors via DP ABORT and fails the operation.
* Power-up: write DP CTRL/STAT `CDBGPWRUPREQ | CSYSPWRUPREQ`, wait for both ACKs.
* AHB-AP (AP 0): CSW = 32-bit, auto-increment single. TAR auto-increment wraps at
  1 KB boundaries; 512-byte aligned blocks never cross one, so one TAR write per block.

### 5.3 Target operations (nRF52832)

| Step | Registers |
|------|-----------|
| Halt core | DHCSR `0xE000EDF0` = `0xA05F0003` |
| Access-port protection | If AHB-AP is locked (APPROTECT), use CTRL-AP (AP 1): ERASEALL `0x004` = 1, poll ERASEALLSTATUS `0x008` until 0, then reconnect. **[Q 6]** |
| NVMC | base `0x4001E000`: READY `+0x400`, CONFIG `+0x504` (0 read, 1 write, 2 erase), ERASEPAGE `+0x508`, ERASEALL `+0x50C`, ERASEUICR `+0x514` |
| Erase | Default **[Q 4]**: NVMC ERASEALL (erases all flash and UICR), poll READY. |
| Write block | CONFIG = 1, TAR = block address, write 128 words through DRW, poll READY after the block (and whenever the AP returns WAIT), CONFIG = 0. Words equal to `0xFFFFFFFF` inside a non-blank block are skipped. |
| UICR | CONFIG = 1, write the four words, poll READY, CONFIG = 0. Needs UICR erased first (ERASEALL covers this). |
| Verify | CONFIG = 0, TAR = block address, read 128 words through DRW (first read is posted, read RDBUFF at the end). |
| Reset | AIRCR `0xE000ED0C` = `0x05FA0004` (SYSRESETREQ), and/or pulse nRESET **[Q 7]**. UICR changes take effect only after reset. |

Flash range checked: `0x00000000` to `0x0007FFFF` (512 KB). Data outside flash and
outside UICR is an error.

### 5.4 Intel HEX streaming and 512-byte blocks

The AVR cannot buffer the image (the target has 512 KB of flash, the host 2 KB of
RAM), so the HEX file is processed as a stream with one 512-byte block buffer:

1. Parse records: type 00 data, 01 EOF, 02 extended segment address, 04 extended
   linear address; 03/05 (start address) are ignored. Every record's checksum is checked.
2. Keep one 512-byte buffer for the block at `addr & ~0x1FF`, pre-filled with `0xFF`.
   Data bytes land in it. When a byte belongs to a different block, the current
   block is flushed and a new one is started.
3. Flush: if all 512 bytes are `0xFF`, skip (R7). Otherwise write it.
4. Gaps inside a block stay `0xFF`, which matches erased flash.

If the HEX file is not in ascending address order, a block can be flushed twice.
That is safe after a full erase only if the two halves do not overlap, because
writing `0xFF` words is skipped. The default is to detect a backwards jump into an
already-written block and accept it only if the overlapping words were skipped
(`0xFF`); otherwise error **[Q 8]**. nrfjprog/mergehex output is ascending, so this
is an edge case.

Verify re-opens the file, runs the same parser and block assembler, and for each
non-blank block reads 128 words from the target and compares. Blank blocks are not
read back by default **[Q 10]**.

### 5.5 SD card and file system

* SPI mode, CMD0, CMD8, ACMD41 (with HCS), CMD58, CMD16 (512 for SDSC), CMD17 single
  block reads. Supports SDSC (typical for 1 GB) and SDHC.
* FAT read with Petit FatFs (by default) **[Q 3]**: about 4 to 6 KB of flash, about
  50 bytes of RAM, no sector buffer, read-only, root directory, 8.3 names.
  Supports FAT12/16/32.
* The file name is `#define HEX_FILENAME "FILENAME.HEX"` (8.3, root directory) **[Q 12]**.

Note: 1 GB cards are almost always formatted **FAT16** from the factory, and many
tools refuse to format a 1 GB card as FAT32. The firmware will read both, so this
only matters for documentation.

### 5.6 RAM budget (ATmega328P)

| Item | Bytes |
|------|-------|
| 512-byte block buffer | 512 |
| Petit FatFs work area + read buffer for one HEX line (max 44 bytes for 16-byte records, sized 64) | ~110 |
| Stack, globals | ~300 |
| Total | ~950 of 2048 |

Verify reuses the same block buffer.

### 5.7 Button

Input with internal pull-up. Pressed = low for `DEBOUNCE_MS` continuously. After a
cycle, the firmware waits for release before arming again, so holding the button
does not restart programming **[Q 11]**.

### 5.8 Error handling and codes

| Code | Meaning |
|------|---------|
| E_SD | SD init failed / no card |
| E_FILE | mount failed or file not found |
| E_SWD | no SWD ACK or wrong IDCODE |
| E_LOCKED | APPROTECT set and recovery not allowed / failed |
| E_ERASE | erase timeout |
| E_HEX | bad HEX record, checksum, address out of range |
| E_WRITE | NVMC timeout or SWD fault while writing |
| E_UICR | UICR write failed |
| E_VERIFY | flash or UICR mismatch |

How each is shown on the two LEDs is **[Q 11]**. Default: LED1 and LED2 both off, then
LED1 blinks N times (N = code number), pause, repeat until the button is pressed.

## 6. Build

Default **[Q 2]**: avr-gcc + avr-libc with a Makefile (`make`, `make flash` via
avrdude). Compiles cleanly with `-Wall -Wextra -Os`. The code is plain C99 so it
also builds in Microchip Studio.

## 7. Test plan

* Host-side unit tests (gcc on a PC) for the HEX parser and block assembler,
  using a sample merged HEX file, including blank-block skipping and gap filling.
* Compile check for the chosen AVR part in CI-like `make`.
* Bench test (by Dave, needs hardware): blank chip, programmed chip, locked chip,
  missing card, missing file, corrupted HEX, verify failure (e.g. disconnect
  mid-verify).

## 8. Out of scope (unless asked)

Programming other Nordic parts, SWD speeds above what bit-banging gives, writing to
the SD card, long file names, USB/PC interface, target power switching.
