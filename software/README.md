# OCS RF software bring-up

The application is configured for a SAMD21 Curiosity Nano mounted directly on
the RF board headers. See `software/boards/ocs_rf_1/README.md` for the traced
pin mapping and the distinction between schematic labels and actual MCU pins.

## Separate test locations

| Location | Runs on | Purpose |
|---|---|---|
| [tests/software/](tests/software/README.md) | QEMU on the Mac; no boards | 40 automated cases using simulated transports/chips. |
| [tests/hardware/spi_bringup/](tests/hardware/spi_bringup/) | Nano mounted on the RF board | Actual SPI register/memory, reset and GPIO interrupt checks; no RF transmission. |
| [tests/hardware/radio_packet/](tests/hardware/radio_packet/) | Nano + RF board, with HackRF for the RF check | Actual radio configuration, button-operated TX and packet RX. |

See [the test index](tests/README.md) and [hardware test instructions](tests/hardware/README.md).
The driver library stays in `src/radio/`; test programs have their own `src/main.c`.
Building `software/` is retained as a compatibility entry point for the SPI
hardware test. New commands below use the explicit hardware-test directories.

## Simple SPI API

Application code can include `<ocs/radio.h>` and call `ocs_radio_init()`,
`ocs_radio_read()`, `ocs_radio_write()`, `ocs_radio_read_reg()` and
`ocs_radio_write_reg()`. No radio structure or transport callback is required.
These synchronous functions access the AT86RF215's registers and memory over
wired SPI; they do not send or receive wireless packets.

See [the SPI API guide](docs/spi-api.md) for a complete Hello world example,
function arguments, errors, C terminology and testing instructions. The existing
bring-up program now exercises this interface, including the part-number and
reversible Hello world memory checks.

## RF packet driver and HackRF bench

The [radio packet guide](docs/radio-packets.md) documents the new
`ocs_radio_configure()`, `ocs_radio_send()` and `ocs_radio_receive()` functions,
the 915 MHz/2-FSK/50 kb/s/CRC32 profile, and a separate button-operated sample
at `tests/hardware/radio_packet`. Build it into `/tmp/ocs-rf-packet-hardware` and use that build
directory when flashing it. The default application below remains the SPI
diagnostic. Physical RF operation has not yet been verified.

## Activate the existing Mac environment

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"
```

## Build for the Nano mounted directly on the RF board

```sh
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/spi_bringup" -d /tmp/ocs-rf-spi-hardware
```

This uses app.overlay with DIPO=0, DOPO=2, PA14 reset and PA20 IRQ.
Do not use an old pre-correction firmware image for the mounted board.

## Run the software tests without a radio

```sh
west twister -T "$OCS_RF_REPO/software/tests/software" -p qemu_cortex_m3 --inline-logs --outdir /tmp/ocs-rf-software-tests
```

Expected: forty cases pass across the core, SPI API and packet suites. The
test executables replace the physical transport; they do not prove RF hardware
operation. The suites cover initialization, invalid inputs, error recovery,
interrupt rejection, serialization, radio states and packet handling.

## Flash and observe the physical tests

Connect the Nano's debugger USB to the Mac. Find its serial port and open the
console in one terminal before flashing from another activated terminal:

```sh
python -m serial.tools.list_ports
python -m serial.tools.miniterm /dev/cu.usbmodemXXXXXXXX 115200
```

Replace the placeholder serial port with the actual discovered port. Flash:

```sh
west flash -d /tmp/ocs-rf-spi-hardware -r openocd --verify
```

Look for `BRING-UP PASS (0)`. If a stage fails, keep the exact console output.
No antenna or CAN connection is needed for these non-transmitting checks.
The Nano's SW0 is a user button, not a dedicated reset button. Reflashing with
the console open restarts the checks and captures the startup messages.

## Verified hardware result: October 22, 2026

The mounted Nano was identified by OpenOCD as SAMD21G17D (128KB flash, 16KB
RAM). The image was flashed and verified, then the serial console reported
PN=0x34, VN=0x03 and `BRING-UP PASS (0)`. All bring-up stages passed.
The ten software tests also passed in QEMU. RF packets and CAN remain untested.
This records the earlier low-level driver run. The new simple API needs its own
physical run; follow the flash instructions above with the boards connected.
