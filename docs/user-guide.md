# User guide

## Installing the firmware on the Feather

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. In **File > Preferences**, add this to "Additional boards manager URLs":
   `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`
3. In **Tools > Board > Boards Manager**, install **Adafruit nRF52**.
4. Open `firmware/nrf52_swd_programmer/nrf52_swd_programmer.ino`.
5. Edit `config.h` if your pins, file name or UICR values differ from the
   defaults (see [configuration.md](configuration.md)).
6. Select **Tools > Board > Adafruit nRF52 > Adafruit Feather nRF52832** and
   the Feather's serial port, then **Upload**.

On Linux, `adafruit-nrfutil` must be installed for uploads
(`pip3 install adafruit-nrfutil`). On Windows and macOS the board package
includes it.

## Programming a board

1. Put the SD card with the HEX file in the programmer.
2. Connect the target and power it.
3. Press the button.
4. Wait for LED1 BUSY to go off and LED2 PASS or LED3 FAIL to come on.
5. Disconnect the target. Repeat from step 2 for the next board.

The HEX file is read again on every press, so a new file can be copied to the
card between boards (power the Feather down or press its reset button before
removing the card).

## What the LEDs mean

| LED1 BUSY | LED2 PASS | LED3 FAIL | Meaning |
|-----------|-----------|-----------|---------|
| off | off | off | idle, ready (after power up) |
| on | off | off | erasing and programming |
| off | off | off | verifying (a few seconds) |
| off | on | off | done, the board passed; ready for the next one |
| off | off | on | an error; see the serial log; ready to try again |

PASS or FAIL stays on until the next button press. Holding the button down
does not start a second cycle; it has to be released first.

## Serial log

Open the serial monitor at 115200 baud (Arduino IDE **Tools > Serial
Monitor**, or any terminal program). A good cycle looks like this (timings are
examples). The card is opened three times per cycle (check, program pass,
verify pass), so the card type line appears three times:

```
nRF52832 SWD programmer ready
HEX file: FILENAME.HEX
Press the button to program the target.

--- Programming cycle started ---
SD card type SDSC v2
FILENAME.HEX: 590112 bytes
SWD connected, IDCODE 0x2BA01477
Access port protection ON before recover
Recover: CTRL-AP ERASEALL
ERASEALL done in 95 ms
Target nRF52832 variant AAE0, flash 512 KB, page 4096 bytes
SD card type SDSC v2
Programmed 420 blocks, skipped 3 blank blocks in 11850 ms
Ignored 8 bytes of UICR data in the HEX file
UICR 0x10001014 = 0x00074000
UICR 0x10001018 = 0x0007E000
UICR 0x10001200 = 0x00000015
UICR 0x10001204 = 0x00000015
SD card type SDSC v2
Verified 420 blocks and 4 UICR words in 1420 ms
Result: PASS (13980 ms, 0 SWD waits)
```

Set `DEBUG_SERIAL` to `0` in `config.h` to remove all log output.

## Troubleshooting

| Serial message | Likely cause | What to try |
|----------------|--------------|-------------|
| `SD card not found or not answering` | no card, bad contact, wrong CS pin | reseat the card; check `SD_PIN_CS`; try another card |
| `pf_mount failed (6)` | card not FAT16/FAT32 (for example exFAT) | reformat as FAT32 or FAT16 |
| `pf_open(...) failed (3)` / `HEX file not found` | name differs from `HEX_FILENAME`, file in a folder, long file name | rename to an 8.3 name in the root directory |
| `SWD connect attempt N failed (IDCODE 0x00000000)` | target not powered, SWDIO/SWCLK swapped or open, no common ground | check power, wiring and ground |
| `SWD connect attempt N failed` with another IDCODE | not an nRF52 target, or signal integrity | check the part; shorten wires; raise `SWD_HALF_PERIOD_DELAY` |
| `CTRL-AP IDR ... expected 0x02880000` | target is not an nRF52 series part | check the part number |
| `recover (ERASEALL) failed` | target lost power during erase | check the supply, retry |
| `target still protected after recover` | protection could not be removed | power cycle the target and retry; check that nRESET is not held low |
| `HEX file error -2 at line N` | checksum error on line N | regenerate or re-copy the file |
| `HEX file error -1 at line N` | not a HEX file, or a corrupt line | check the file |
| `HEX file error -5` | file truncated (no end-of-file record) | re-copy the file |
| `HEX addresses go backwards at 0x...` | file not in ascending order | regenerate with `mergehex` or `srec_cat` |
| `HEX data at 0x... is outside flash` | file built for a larger part | check the build target |
| `HEX file has no flash data` | wrong file (UICR only, or empty) | check the file |
| `Write failed at block 0x...` | SWD error during programming | check wiring and power; slow SWCLK |
| `Verify error at 0x...: expected ..., read ...` | flash did not take the data, or read error | retry; if it repeats at the same address the chip may be faulty |
| `UICR verify error` | UICR did not program | retry; check `UICR_ADDR_n` values are inside UICR |

## Newer nRF52832 silicon and debug access

Recent nRF52832 revisions turn read-back protection back on at every reset
unless UICR.APPROTECT holds `0x5A` and the application firmware also disables
protection at startup. This programmer always recovers the chip first, so it
can reprogram such boards any time. If other tools (a J-Link, nrfjprog) must
be able to attach to programmed boards without a recover, add the UICR entry
shown in `config.h` and make sure the application writes
`NRF_APPROTECT->DISABLE`.
