# Configuration reference

Everything that can be changed is a `#define` in
[`firmware/nrf52_swd_programmer/config.h`](../firmware/nrf52_swd_programmer/config.h).
Rebuild and upload the sketch after a change.

Pin numbers are Arduino pin numbers. On the Feather nRF52832 they are equal to
the P0.xx GPIO numbers.

## File

| Define | Default | Description |
|--------|---------|-------------|
| `HEX_FILENAME` | `"FILENAME.HEX"` | Name of the HEX file in the SD card's root directory. 8.3 format, any case. |

## SD card (SPI)

| Define | Default | Description |
|--------|---------|-------------|
| `SD_PIN_MOSI` | `13` | SPI data out to the card. Any GPIO works (the nRF52 routes SPI to any pin). |
| `SD_PIN_MISO` | `14` | SPI data in from the card. |
| `SD_PIN_SCK` | `12` | SPI clock. |
| `SD_PIN_CS` | `11` | Card chip select. `11` matches the Adalogger FeatherWing. |
| `SD_SPI_INIT_HZ` | `250000` | SPI clock while the card is identified. Must be 100 to 400 kHz. |
| `SD_SPI_FAST_HZ` | `8000000` | SPI clock for reading. 8 MHz is the nRF52832 maximum; lower it for long wires. |

## SWD and target reset

| Define | Default | Description |
|--------|---------|-------------|
| `SWD_PIN_SWCLK` | `27` | SWD clock to the target. |
| `SWD_PIN_SWDIO` | `30` | SWD data, bidirectional. |
| `TARGET_PIN_RESET` | `29` | Target nRESET. Driven low to reset, otherwise high impedance. |
| `SWD_HALF_PERIOD_DELAY` | `2` | Delay-loop count per half SWCLK period. `0` is fastest (roughly 3 to 4 MHz), `2` roughly 2 MHz, `10` roughly 1 MHz. Measure with a scope for exact values. |
| `SWD_WAIT_RETRIES` | `2000` | How many SWD WAIT responses to retry before giving up on one transfer. |
| `TARGET_RESET_PULSE_MS` | `10` | Length of the nRESET pulse. |

## Operator interface

| Define | Default | Description |
|--------|---------|-------------|
| `BUTTON_PIN` | `7` | Start button to GND. The internal pull-up is enabled. |
| `BUTTON_DEBOUNCE_MS` | `20` | The button must read low (and later high) this long. |
| `LED_BUSY_PIN` | `15` | LED1, on during erase and programming. Active high. |
| `LED_PASS_PIN` | `16` | LED2, on after a successful verify. Active high. |
| `LED_FAIL_PIN` | `2` | LED3, on after any error. Active high. |

## Debug log

| Define | Default | Description |
|--------|---------|-------------|
| `DEBUG_SERIAL` | `1` | `1` prints progress and errors on the Feather's serial port. `0` removes all log code. |
| `DEBUG_BAUD` | `115200` | Serial speed. |

## Target memory

| Define | Default | Description |
|--------|---------|-------------|
| `PROGRAM_BLOCK_SIZE` | `512` | Bytes per program and verify block. Must be a multiple of 4 that divides 1024 (so a block never crosses the debug port's 1 KB address wrap). |

## UICR values

The programmer ignores UICR data in the HEX file and writes this table after
the flash. Each entry has an address and a 32-bit value. Up to 8 entries.

| Define | Default | Register |
|--------|---------|----------|
| `UICR_ADDR_0` / `UICR_DATA_0` | `0x10001014` / `0x00074000` | NRFFW[0]: bootloader start address |
| `UICR_ADDR_1` / `UICR_DATA_1` | `0x10001018` / `0x0007E000` | NRFFW[1]: MBR parameter page |
| `UICR_ADDR_2` / `UICR_DATA_2` | `0x10001200` / `0x00000015` | PSELRESET[0]: P0.21 is the reset pin |
| `UICR_ADDR_3` / `UICR_DATA_3` | `0x10001204` / `0x00000015` | PSELRESET[1]: must equal PSELRESET[0] |
| `UICR_ENTRY_COUNT` | `4` | Number of entries in use (1 to 8). |

To add an entry, define `UICR_ADDR_4` and `UICR_DATA_4` (then `_5`, `_6`,
`_7`) and raise `UICR_ENTRY_COUNT`. Example, keeping debug access open on
newer silicon:

```c
#define UICR_ADDR_4             0x10001208UL    /* APPROTECT */
#define UICR_DATA_4             0x0000005AUL    /* HwDisabled */
#define UICR_ENTRY_COUNT        5
```

A value of `0xFFFFFFFF` is the erased state, so that entry is effectively not
written (and verifies as `0xFFFFFFFF`).

## Petit FatFs

[`pffconf.h`](../firmware/nrf52_swd_programmer/pffconf.h) enables FAT12, FAT16
and FAT32, read only. There is normally no reason to change it.
