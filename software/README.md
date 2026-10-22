# OCS RF software bring-up

The application is configured for a SAMD21 Curiosity Nano mounted directly on
the RF board headers. See `software/boards/ocs_rf_1/README.md` for the traced
pin mapping and the distinction between schematic labels and actual MCU pins.

## Activate the existing Mac environment

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"
```

## Build for the Nano mounted directly on the RF board

```sh
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software" -d /tmp/ocs-rf-mounted
```

This uses app.overlay with DIPO=0, DOPO=2, PA14 reset and PA20 IRQ.
Do not use an old pre-correction firmware image for the mounted board.

## Run the software tests without a radio

```sh
west twister -T "$OCS_RF_REPO/software/tests/radio_core" -p qemu_cortex_m3 --inline-logs --outdir /tmp/ocs-rf-software-tests
```

Expected: ten executed cases pass. They use a fake SPI transport, not RF hardware.

## Flash and observe the physical tests

Connect the Nano's debugger USB to the Mac. Find its serial port and open the
console in one terminal before flashing from another activated terminal:

```sh
python -m serial.tools.list_ports
python -m serial.tools.miniterm /dev/cu.usbmodemXXXXXXXX 115200
```

Replace the placeholder serial port with the actual discovered port. Flash:

```sh
west flash -d /tmp/ocs-rf-mounted -r openocd --verify
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
