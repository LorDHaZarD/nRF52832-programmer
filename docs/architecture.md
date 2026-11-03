# Firmware architecture

This page is for engineers who need to change or port the firmware. For the
requirements and the reasoning behind them, see [SPEC.md](SPEC.md).

## Modules

```
nrf52_swd_programmer.ino   Arduino setup()/loop(): button, LEDs, serial log,
        |                  millis()/delay() for the C modules
        v
programmer.c               one cycle: open file, connect, recover, program,
   |        |              UICR, verify, reset; block assembler
   |        v
   |    ihex.c             streaming Intel HEX parser (no line buffer)
   |    hexsrc_sd.cpp      SD card SPI driver, Petit FatFs disk layer,
   |        |              hexsrc_open/read (the file source)
   |        v
   |    pff.c              Petit FatFs (read-only FAT12/16/32)
   v
nrf52_target.c             nRF52 operations: connect, recover, halt, FICR,
   |                       NVMC write, read-back, reset and run
   v
swd.c                      SWD packets, WAIT/FAULT handling, DP and AP
   |                       registers, MEM-AP word and block access
   v
swd_port.h                 GPIO: SWCLK, SWDIO, nRESET (direct NRF_P0 writes),
                           or the simulator when PROGRAMMER_SIM is defined
```

Everything below the `.ino` and `hexsrc_sd.cpp` is plain C99 with no Arduino
dependencies, which is what lets the PC test compile the same files.

## The cycle

`prog_run()` in `programmer.c` runs these steps and returns a `prog_err_t`:

1. `hexsrc_open()`: initialise the card, mount, open the file. Done first so a
   missing file never erases a working board.
2. `nrf52_connect()`: pulse nRESET, SWD line reset and JTAG-to-SWD switch, read
   IDCODE (`0x2BA01477`), power up the debug domain, check the CTRL-AP IDR
   (`0x02880000`). Three attempts with longer settle times.
3. `nrf52_recover()`: CTRL-AP ERASEALL, poll ERASEALLSTATUS, check
   APPROTECTSTATUS. If protection is still reported, pulse CTRL-AP RESET,
   re-attach and check again. Then set the AHB-AP CSW and halt the core.
4. `nrf52_read_info()`: FICR part, variant, page size and flash size.
5. Program pass: NVMC CONFIG = write enable, stream the file through the block
   assembler, NVMC CONFIG = read only.
6. UICR: write each `UICR_ADDR_n` / `UICR_DATA_n`.
7. `ui_busy_led(0)`: LED1 off.
8. Verify pass: stream the file again, read back and compare each non-blank
   block; read back the UICR table.
9. `nrf52_reset_and_run()`: clear DHCSR.C_DEBUGEN, AIRCR.SYSRESETREQ, drop the
   debug power request, float the SWD pins, pulse nRESET.

On an error the cycle stops at once, powers down the debug port and floats the
SWD pins. The target is left erased or partly programmed.

## Block assembler

The HEX file is never held in memory. `on_data()` in `programmer.c` receives
each data record from the parser and copies its bytes into a 512-byte buffer
that covers the block `addr & ~0x1FF`. The buffer starts as all `0xFF`, so gaps
in the file read as erased flash.

* A byte for a higher block flushes the buffer and starts a new one.
* A byte for a lower block is an error (the file must be ascending).
* Bytes in `0x10001000` to `0x10001FFF` (UICR) are counted and dropped.
* Bytes at or above the flash size are an error.

`block_flush()` skips an all-`0xFF` block. Otherwise the program pass writes it
word by word (skipping `0xFFFFFFFF` words) and the verify pass reads 128 words
with one auto-incrementing block read and compares them.

## SWD details

* Bit timing: the host sets SWDIO and drops SWCLK, the target samples on the
  rising edge. When the target drives, the host samples while SWCLK is low.
  This is the CMSIS-DAP scheme.
* A transfer is 8 request bits, a turnaround, 3 ACK bits, then 33 data bits
  (32 + parity) and the turnarounds in the right places, plus 2 idle cycles.
* WAIT is retried up to `SWD_WAIT_RETRIES` times, then the transfer is aborted
  with DP ABORT.DAPABORT. FAULT clears the sticky flags through DP ABORT and
  returns an error. No ACK (`0b111`) means nothing is driving SWDIO.
* DP SELECT is cached, so it is written only when the AP or register bank
  changes.
* AP reads are posted. `swd_ap_read()` issues the AP read and then reads
  DP RDBUFF. `swd_mem_read_block()` pipelines: N AP reads and one RDBUFF read
  give N words.
* Flash words are written as TAR + DRW, then NVMC.READY is polled. This costs
  5 SWD transfers per word and dominates programming time; see SPEC section 6.

## Register map used on the target

| Register | Address | Use |
|----------|---------|-----|
| CTRL-AP RESET | AP 1, 0x000 | fallback reset after ERASEALL |
| CTRL-AP ERASEALL | AP 1, 0x004 | recover |
| CTRL-AP ERASEALLSTATUS | AP 1, 0x008 | 1 while erasing |
| CTRL-AP APPROTECTSTATUS | AP 1, 0x00C | 1 = protection off |
| CTRL-AP IDR | AP 1, 0x0FC | `0x02880000` |
| NVMC.READY | `0x4001E400` | 1 = ready |
| NVMC.CONFIG | `0x4001E504` | 0 read, 1 write |
| FICR.CODEPAGESIZE | `0x10000010` | page size |
| FICR.CODESIZE | `0x10000014` | number of pages |
| FICR.INFO.PART | `0x10000100` | `0x52832` |
| FICR.INFO.VARIANT | `0x10000104` | ASCII, for example `AAE0` |
| DHCSR | `0xE000EDF0` | halt and release the core |
| AIRCR | `0xE000ED0C` | system reset |

## Porting to another host

* **Other GPIO hardware:** rewrite the inline functions in `swd_port.h`
  (`swclk_lo/hi`, `swdio_set/get`, `swdio_drive/float`, `swd_half_delay`,
  `target_reset_assert/release`, `swd_port_init/release`).
* **Other file source:** implement `hexsrc_open`, `hexsrc_read` and
  `hexsrc_size` (see `hexsrc.h`). The PC test's `test/sim_host.c` is a
  20-line example that reads a normal file.
* **Time and output:** provide `port_millis`, `port_delay_ms`, `ui_busy_led`
  and (if `DEBUG_SERIAL`) `log_printf`.

`programmer.c`, `nrf52_target.c`, `swd.c` and `ihex.c` need no changes.

## Possible speed-up

Programming could be roughly 2 to 3 times faster with a small flash loader:
copy each block into target RAM with auto-incrementing writes, then let the
target CPU program it. This is not implemented because it can only be proven
on hardware; the NVMC-polling method is simple and safe.
