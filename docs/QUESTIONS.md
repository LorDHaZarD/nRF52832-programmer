# Design questions and answers

These questions were asked before implementation, against the first draft of
the spec (which still assumed an AVR host). The answers at the end changed
the design; [SPEC.md](SPEC.md) rev 2 is the current reference.

Each question offered a default. Section references point into the first
draft of the spec.

---

**Q1. Which AVR part, clock and supply voltage?** (spec 3.1, 4)
Pin defaults, SPI register names and the RAM budget depend on it.
Default: ATmega328P, 16 MHz crystal. If you use a board (Uno, Nano, Pro Mini 3.3 V,
Mega 2560...), name it.

> Answer:

**Q2. Toolchain?**
Default: avr-gcc + avr-libc with a Makefile and an `avrdude` flash target. Plain C99
so it also imports into Microchip Studio. Say so if you need a Microchip Studio
project file, MPLAB X, or an Arduino IDE sketch instead.

> Answer:

**Q3. FAT library?**
Default: Petit FatFs by ChaN (BSD-style license, read-only, tiny RAM, FAT12/16/32),
vendored into the repo with my own SD SPI driver under it. Alternatives: full FatFs
(needs a 512-byte sector buffer, too much for a 328P alongside the block buffer), or
a minimal FAT32-only reader written from scratch (no third-party code).
Also note: 1 GB cards normally ship as FAT16. Petit FatFs reads both, so "FAT32"
does not need to be enforced. OK?

> Answer:

**Q4. Erase strategy?** (spec 5.3)
Default: NVMC ERASEALL once before programming. It wipes all flash and UICR, which
is needed anyway because UICR can only be written when erased. Alternative: erase
only the 4 KB pages the HEX file touches, plus ERASEUICR. That keeps other flash
contents (for example stored settings or bonding data) but takes more code and is
slower for a full image.

> Answer:

**Q5. What voltage does the AVR run at, and is there level shifting to the nRF52832 and the SD card?** (spec 3.3)
The nRF52832 and SD cards are 3.3 V parts and not 5 V tolerant. This does not change
the code, but if SWDIO goes through a shifter I need to know what kind (a
direction-sensing shifter like TXS0108 can be flaky with SWD; a series resistor or
74LVC1T45 with a direction pin changes the code).
Default: AVR at 3.3 V, direct wiring, no shifters.

> Answer:

**Q6. What if the target has read-back protection (APPROTECT) enabled?** (spec 5.3)
Default: if the AHB-AP is locked, run CTRL-AP ERASEALL automatically (this is the
only way to unlock it, and it wipes the chip), then continue. Alternative: stop
with an error and do nothing.

> Answer:

**Q7. Is the target's nRESET pin wired to the programmer?** (spec 3.2, 5.3)
Default: not wired. The firmware resets the target over SWD (SYSRESETREQ) after
verify so the new UICR settings take effect and the firmware starts. If it is
wired, I add a `#define` pin and also use it to recover a target that will not
connect.

> Answer:

**Q8. Can the HEX file be out of address order?** (spec 5.4)
Default: assume it comes from nrfjprog/mergehex/nRF Connect (ascending order) and
error out if a later record writes non-0xFF data into a block already written.
Alternative: support any order, at the cost of a second pass over the file per
out-of-order region.

> Answer:

**Q9. Can the HEX file contain UICR data (addresses 0x10001000 to 0x100013FF)?**
Merged images from Nordic tools often include UICR records (the bootloader HEX
usually sets NRFFW[0] and NRFFW[1] itself). Default: write UICR records from the
file too, then the four fixed values from the brief override the same addresses.
If the file sets one of those addresses to a different value, the brief's value
wins and no error is raised. Alternatives: ignore UICR data in the file, or treat
it as an error.

> Answer:

**Q10. What does verify cover?** (spec 5.4)
Default: every non-blank 512-byte block from the file, plus the four UICR words.
Blank (all 0xFF) blocks are not read back, since ERASEALL already guarantees them.
Alternative: also read back blank blocks (slower, about 512 KB over bit-banged SWD,
which is roughly 10 to 20 seconds more).

> Answer:

**Q11. LED and button behaviour outside the happy path?** (spec 5.1, 5.7, 5.8)
The brief says what happens on success but not on failure. Defaults:
* At the start of a cycle, LED2 (PASS) turns off.
* On any error: LED1 off, LED2 off, then LED1 blinks N times (N = error code from
  spec 5.8), a pause, and repeat until the button is pressed again, which starts a
  new cycle.
* LED2 stays on after a pass until the next button press.
* The button must be released before the next cycle can start.
Alternatives: a third "FAIL" LED pin, or both LEDs blinking together on any error
with no code.

> Answer:

**Q12. File name.**
Default: `#define HEX_FILENAME "FILENAME.HEX"`, 8.3 format, root directory of the
card, case-insensitive. Long file names and subdirectories are not supported. Do
you want a different default name, or a subdirectory?

> Answer:

**Q13. Debug serial output?**
Default: yes, an optional UART log (`#define DEBUG_UART 1`, 115200 baud on TX) that
prints each step, IDCODE, block counts and errors. It is compiled out when set to 0.

> Answer:

**Q14. Any fixed pin assignments for SWCLK, SWDIO, button, LED1, LED2?** (spec 3.2)
Default: the table in spec 3.2 (spec references this as part of Q1) (PD2 SWCLK, PD3 SWDIO, PD5 button, PD6 LED1, PD7
LED2, PB2 SD CS). All are `#define`s either way.

> Answer:

**Q15. Any cycle time target?**
Bit-banged SWD on a 16 MHz AVR runs around 0.5 to 1 MHz, so a full 512 KB image
takes very roughly 30 to 60 seconds to program plus the same to verify, and a
typical 150 to 300 KB image proportionally less. Is that acceptable?
Default: yes, optimise for correctness first, with the SWD inner loop written
tightly in C.

> Answer:

**Q16. Anything not in the brief?**
For example: a beep, a counter of programmed boards, writing a serial number per
board, checking the HEX file against a CRC, or programming several targets.
Default: none.

> Answer:

---

## Answers (from Dave, 2026-11-03)

| Q | Answer | Where it landed |
|---|--------|-----------------|
| 1 | Host changes from AVR to an nRF52832 on an Adafruit Feather. | SPEC 2, 3 |
| 2 | Arduino IDE sketch. | `firmware/nrf52_swd_programmer/` |
| 3 | Petit FatFs is OK. | vendored, `pffconf.h` |
| 4 | Do an nRF recover, since protection bits may be set. | SPEC 5.4 |
| 5 | 3.3 V, direct wired. | SPEC 3.2 |
| 6 | Protection may be set, see Q4. | SPEC 5.4 |
| 7 | Target reset pin is wired to the programmer. | `TARGET_PIN_RESET` |
| 8 | HEX file is in ascending address order. | SPEC 5.5 |
| 9 | Ignore UICR records in the file; write the given values; address and data as `#define`s. | `UICR_ADDR_n` / `UICR_DATA_n` |
| 10 | Ignore blank blocks on verify. | SPEC 5.5 |
| 11 | Add a third LED as a FAIL indicator. | `LED_FAIL_PIN` |
| 12 | 8.3 name in the root directory, will be changed by `#define`. | `HEX_FILENAME` |
| 13 | Serial debug messages, Feather default port and 115200 baud. | `DEBUG_SERIAL`, `DEBUG_BAUD` |
| 14 | Use pins available on the Feather. | SPEC 3.1 |
| 15 | Estimate the cycle time; typical image is much smaller than 512 KB. | SPEC 6 |
| 16 | Nothing else. | |
