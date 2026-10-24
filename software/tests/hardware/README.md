# Hardware tests: actual Nano and RF board

These are standalone Zephyr programs. They use the real SPI/GPIO adapter and
run only after being flashed onto the Nano. Compiling them does not mean the
board checks passed. They contain no simulated radio.

| Program | What runs on the board | Pass evidence |
|---|---|---|
| [spi_bringup/src/main.c](spi_bringup/src/main.c) | Initializes/resets the radio and reads PN/VN over SPI. Checks PN against `0x34` and prints VN; no register/buffer writes, IRQ-status clearing or RF transmission. | Actual terminal output with identification values and `BRING-UP PASS (0)`. |
| [radio_packet/src/main.c](radio_packet/src/main.c) | RF profile/PLL readiness, ten 2043-byte packets per SW0 press with one-second delays, CRC-checked packet RX. Payload repeats Hello world text. | Configuration output, external HackRF capture, then a matching CRC-valid 2043-byte reception during replay. Local TX completion alone does not establish reception. |
| [can_bringup/](can_bringup/README.md) | SW0-triggered read-only MCP251863 SPI/CRC/clock checks; standby sampled as an input. | Actual register output and `CAN SPI CHECK PASS (0)`; does not prove CANH/CANL or loopback. |
| [can_loopback/](can_loopback/README.md) | SW0-triggered internal CAN frame and RX interrupt check; requires STBY high. Resets/configures controller/RAM; never requests Normal or External Loopback mode. | Actual received ID/bytes, IRQ assert/clear, verified Configuration cleanup, and `CAN INTERNAL LOOPBACK PASS (0)`. |

The earlier low-level SPI diagnostic passed on the board. On October 24, 2026,
the operator reported observing the enlarged-packet bench transmission on an SDR.
Payload decoding and CRC-valid receive/replay remain unverified.
The operator also reported `CAN SPI CHECK PASS (0)`, DEVID `0`, OSC `0x460`,
CiCON `0x04980760` and standby HIGH on October 24. Internal CAN loopback and
physical CANH/CANL operation still need their own measured results.

## Build each separately

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"

west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/spi_bringup" -d /tmp/ocs-rf-spi-hardware
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/radio_packet" -d /tmp/ocs-rf-packet-hardware
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/can_bringup" -d /tmp/ocs-can-bringup
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/can_loopback" -d /tmp/ocs-can-loopback
```

Each application has its own `src/main.c`, `prj.conf` and `CMakeLists.txt`.
The radio apps import `software/app.overlay`. The CAN apps use their own overlays
and diagnostic cores; loopback reuses the read-only probe's pin mapping. All use the board configuration and
headers from `software/`. The old `west build ... "$OCS_RF_REPO/software"`
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

# OR the read-only CAN SPI probe, after following its setup guide:
west flash -d /tmp/ocs-can-bringup -r openocd --verify

# OR the internal CAN loopback bench, after following its setup guide:
west flash -d /tmp/ocs-can-loopback -r openocd --verify
```

Flashing replaces the application on the Nano; these applications do not run
simultaneously. Watch its terminal for measured results. The CAN probe's setup,
pin mapping and limits are in [can_bringup/README.md](can_bringup/README.md).

Before operating the RF packet bench, follow the antenna/attenuation setup and
Mayhem procedure in [the RF guide](../../docs/radio-packets.md). It starts by
listening; a fresh SW0 press triggers TX. For SPI checks and C terminology see
[the SPI guide](../../docs/spi-api.md). Software-only tests are [../software/](../software/README.md).
