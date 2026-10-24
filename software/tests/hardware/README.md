# Hardware tests: actual Nano and RF board

These are standalone Zephyr programs. They use the real SPI/GPIO adapter and
run only after being flashed onto the Nano. Compiling them does not mean the
board checks passed. They contain no simulated radio.

| Program | What runs on the board | Pass evidence |
|---|---|---|
| [spi_bringup/src/main.c](spi_bringup/src/main.c) | Initializes/resets the radio and reads PN/VN over SPI. Checks PN against `0x34` and prints VN; no register/buffer writes, IRQ-status clearing or RF transmission. | Actual terminal output with identification values and `BRING-UP PASS (0)`. |
| [radio_packet/src/main.c](radio_packet/src/main.c) | RF profile/PLL readiness, one Hello world TX per button press, CRC-checked packet RX. | Configuration output, external HackRF capture, then CRC-valid Hello world reception during replay. Local TX completion alone does not establish reception. |

The earlier low-level SPI diagnostic passed on the board. The newer public API
and RF packet program still need physical runs; hardware is currently unavailable.

## Build each separately

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"

west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/spi_bringup" -d /tmp/ocs-rf-spi-hardware
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/radio_packet" -d /tmp/ocs-rf-packet-hardware
```

Each application has its own `src/main.c`, `prj.conf` and `CMakeLists.txt`.
Both import the board configuration, `app.overlay`, bindings, headers and driver
sources from `software/`. The old `west build ... "$OCS_RF_REPO/software"`
entry point still builds the SPI diagnostic for compatibility.

## Connect, monitor and flash one program

With the Nano mounted correctly in the RF headers, connect its debugger USB to
the Mac. In another activated terminal, find the current serial port:

```sh
python -m serial.tools.list_ports
python -m serial.tools.miniterm /dev/cu.usbmodemXXXXXXXX 115200
```

Replace the placeholder with the detected Nano port. Then flash **one** program
from the Zephyr-workspace terminal:

```sh
# SPI board checks:
west flash -d /tmp/ocs-rf-spi-hardware -r openocd --verify

# OR the RF packet bench:
west flash -d /tmp/ocs-rf-packet-hardware -r openocd --verify
```

Flashing replaces the application on the Nano; the two programs do not run
simultaneously. Watch its terminal for measured results.

Before operating the RF packet bench, follow the antenna/attenuation setup and
Mayhem procedure in [the RF guide](../../docs/radio-packets.md). It starts by
listening; a fresh SW0 press triggers TX. For SPI checks and C terminology see
[the SPI guide](../../docs/spi-api.md). Software-only tests are [../software/](../software/README.md).
