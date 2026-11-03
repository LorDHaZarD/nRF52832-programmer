# nRF52832 programmer

Stand-alone production programmer for the Nordic nRF52832. An Adafruit Feather
nRF52832 reads an Intel HEX file from an SD card, recovers the target over SWD
(erase and remove read-back protection), programs the flash in 512-byte blocks
(blank blocks skipped), writes fixed UICR values, verifies, and shows the
result on three LEDs.

* [Specification](docs/SPEC.md)
* [Design questions and answers](docs/QUESTIONS.md)

> **Status:** the firmware passes the PC simulation test suite and a syntax
> check against the Adafruit nRF52 core, but it has not yet been run on real
> hardware. Please report bench results (and the measured SWCLK rate) as
> GitHub issues.

## Wiring

| Feather nRF52832 | Connects to |
|------------------|-------------|
| 12 SCK, 13 MO, 14 MI, 11 | SD card CLK, DI, DO, CS (an Adalogger FeatherWing uses these) |
| 27 | target SWDCLK |
| 30 (A6) | target SWDIO |
| 29 (A5) | target nRESET (P0.21) |
| 7 | push button to GND |
| 15 | LED1 BUSY (active high, with resistor) |
| 16 | LED2 PASS (active high, with resistor) |
| 2 (A0) | LED3 FAIL (active high, with resistor) |
| GND (and 3V if needed) | target GND (and VDD) |

Every pin, clock, the file name and the UICR table are `#define`s in
[`config.h`](firmware/nrf52_swd_programmer/config.h).

## Build and flash the programmer

1. Arduino IDE: add the Adafruit board URL
   `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`,
   install **Adafruit nRF52** in the Boards Manager.
2. Open `firmware/nrf52_swd_programmer/nrf52_swd_programmer.ino`.
3. Board: **Adafruit Feather nRF52832**. Pick the serial port, Upload.

No extra libraries are needed (Petit FatFs is included in the sketch folder).

## Use

1. Copy the HEX file to the root of a FAT16 or FAT32 SD card, named as in
   `HEX_FILENAME` (default `FILENAME.HEX`).
2. Connect the target and press the button.
3. LED1 is on while erasing and programming, then goes off for the verify.
   LED2 = pass, LED3 = fail. The serial monitor (115200 baud) shows each
   step, the target part, block counts, timing and the reason for any failure.

A typical 200 KB image takes roughly 10 to 15 seconds at the default SWD
speed (see the spec, section 6).

## Test without hardware

```
cd test
make
```

This compiles the programmer's C modules with gcc and runs them against a
cycle-level simulation of the nRF52832 debug port, CTRL-AP, memory access
port and flash controller: full program and verify runs (protected and
unprotected targets), slow flash, a stuck flash bit, bad HEX files, missing
card, file or target.

## License

MIT, see [LICENSE](LICENSE).

Third-party code: Petit FatFs R0.03a (`pff.c`, `pff.h`, `diskio.h`,
`pffconf.h`), Copyright (C) ChaN, under its own permissive license, which is
kept in the header of `pff.c`. It is compatible with MIT.
