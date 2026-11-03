# Testing

## PC simulation tests

```
cd test
make
```

Needs `gcc`, `make` and `python3`. The same command runs in GitHub Actions on
every push and pull request.

### What it does

`make` compiles `swd.c`, `nrf52_target.c`, `ihex.c` and `programmer.c` from the
sketch folder with `-DPROGRAMMER_SIM`. In that mode `swd_port.h` sends every
SWCLK edge and SWDIO change to `test/sim_target.c`, a cycle-level model of an
nRF52832:

* SW-DP: line reset, JTAG-to-SWD switch, IDCODE, ABORT, CTRL/STAT power-up
  handshake, SELECT, RDBUFF, request parity, ACK OK/WAIT/FAULT, turnarounds.
* AHB-AP: CSW, TAR, DRW with posted reads and the 1 KB auto-increment wrap.
* CTRL-AP: ERASEALL with a busy period, APPROTECTSTATUS, RESET, IDR.
* NVMC: writes only when CONFIG = 1, bits can only go from 1 to 0, READY low
  for a while after each write, WAIT responses while busy.
* Flash starts filled with non-`0xFF` "old firmware", so a missed erase shows.

`test/make_test_hex.py` generates the HEX files and the expected flash images.

### What it checks

| Case | Expected |
|------|----------|
| Merged image with gaps, a blank block, `0xFF` words, an unaligned record, lower-case lines, start-address records, UICR records, no final newline | PASS; flash equals the expected image; block counts match; exactly the 4 UICR words from `config.h` set |
| Same, target protected | PASS after recover |
| Same, protection lifts only after CTRL-AP reset | PASS, one CTRL-AP reset |
| Slow flash (10 WAITs and 5 busy READY polls per word) | PASS |
| One flash bit stuck at 1 | "flash verify failed" at the right address, BUSY LED off |
| Bad checksum, garbage, missing EOF | "HEX file format error" |
| Addresses going backwards | "HEX file addresses not ascending" |
| Data beyond 512 KB | "HEX data outside target flash" |
| Only UICR data | "HEX file has no flash data" |
| Missing file, missing card | error, target never erased |
| Nothing on the SWD pins | "no SWD connection to target" |
| Parser fed 1, 4 or 7 bytes at a time | same result as one buffer |
| 210 KB typical image | PASS and a timing estimate |

Every run also checks that SWDIO was never driven by both sides at once, that
no write parity error occurred, that every memory access used 32-bit
auto-increment, and that no flash word was written twice or with the NVMC in
the wrong mode.

### What it cannot check

The model follows the Arm and Nordic documentation, so it cannot catch a case
where the real silicon behaves differently from the documentation, and it
does not cover the SD card driver, Petit FatFs or real signal timing. Those
need the bench tests below.

## Bench test checklist

Run these once on real hardware before production use, and after any change
to `swd.c`, `swd_port.h` or `hexsrc_sd.cpp`.

| # | Test | Pass condition |
|---|------|----------------|
| 1 | Measure SWCLK with a scope during programming | clean edges; note the frequency |
| 2 | Blank (new) target | PASS; the application runs after the cycle |
| 3 | Already programmed target | PASS |
| 4 | Target with read-back protection set (`nrfjprog --rbp ALL`) | PASS |
| 5 | Read back with `nrfjprog --readcode` and compare to the HEX file | identical |
| 6 | Read UICR with `nrfjprog --memrd 0x10001014 --n 8` and `--memrd 0x10001200 --n 8` | `0x74000`, `0x7E000`, `0x15`, `0x15` |
| 7 | Press the target's reset button (P0.21) after programming | target resets |
| 8 | No SD card | FAIL, "SD card error", target not erased |
| 9 | Card without the file | FAIL, "HEX file not found", target not erased |
| 10 | Corrupt one character of the HEX file | FAIL, "HEX file format error" |
| 11 | No target connected | FAIL, "no SWD connection" |
| 12 | Disconnect SWDIO during verify | FAIL, verify error |
| 13 | Hold the button down through a cycle | exactly one cycle |
| 14 | FAT16 card and FAT32 card | both work |
| 15 | 20 boards in a row | 20 PASS, note the average cycle time |

Please report results, especially the measured SWCLK and cycle time, as GitHub
issues so the docs can be updated.
