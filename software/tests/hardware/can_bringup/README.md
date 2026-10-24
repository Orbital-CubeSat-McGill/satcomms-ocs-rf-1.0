# MCP251863: read-only SPI presence check

This standalone Zephyr application checks the CAN controller's digital SPI
interface and clock status. It does not test the CANH/CANL physical transceiver,
message RAM, interrupts as events, internal loopback, or a CAN network.

## Hardware and scope

The Nano is plugged directly into the green board's two headers and powers it
through those headers from the Nano debugger USB. Keep this mounting intact.
No USB-to-CAN adapter or second CAN node is required for this first test.
For the first bench run, leave the separate CAN connector J5 unconnected.
J5 is not one of the two Nano mounting headers.

The supplied schematic calls U4 MCP251863 and shows VDD/VIO at 3.3 V and VCC
at 5 V. Actual supply voltages have not been measured in this session; software
cannot verify power rails, shorts, soldering, header orientation or PCB revision.
This program does not change the Nano's voltage setting or require wiring changes.

| Signal | RF-board header | Actual Nano pin | Behavior |
|---|---|---|---|
| CS | J2.7 | PA07 | Active-low GPIO chip select |
| MISO / U4 SDO | J2.8 | PA06 / SERCOM0 PAD2 | MCU input |
| SCK | J2.9 | PA05 / SERCOM0 PAD1 | MCU clock output |
| MOSI / U4 SDI | J2.10 | PA04 / SERCOM0 PAD0 | MCU data output |
| INT | J2.13 | PA03 | Input only; level printed, no interrupt handler |
| STBY | J2.14 | PA02 | Input only; must read HIGH before probing |

Connector positions come from the supplied RF-board schematic and Microchip's
standard Nano pinout. The schematic's PA14/PA15 labels on the CAN control nets
are misleading for direct mounting. PA14 is the existing radio reset connection;
this app does not drive PA14 or PA15. GPIO0/INT0 and GPIO1/INT1 are not configured.

U4 internally pulls STBY toward VIO. The application samples PA02 without
enabling an MCU pull or driving it. If STBY reads low, the check stops with
`-EBUSY` instead of changing standby. High standby disables normal CAN bus
transmission/reception; it does not prevent controller SPI register access.

## What happens

1. Boot Zephyr and configure SPI mode 0 at 100 kHz. Configure STBY, INT and
   SW0 as inputs. The controller driver may initialize the MCU SPI pins/CS,
   but no MCP251863 SPI command is sent until a fresh SW0 press.
2. Leave SW0 released at startup, then press it. Ten snapshots are read, with
   20 ms pauses. Standby is checked before each snapshot.
3. Each snapshot reads only CiCON (`0x000`), OSC (`0xE00`) and DEVID (`0xE14`)
   using READ_CRC (opcode `0xB`). CRC-16 covers command/address, byte count,
   data and the received CRC. Polynomial is `0x8005`, initial state `0xFFFF`.
4. Print the actual register words, device ID/revision fields, operating mode,
   and interrupt level. DEVID can be zero; unlike the AT86RF215, this test does
   not require a `0x34` part number. CRC validation rejects a stuck-low or
   stuck-high MISO response.
5. Require stable DEVID across the ten snapshots and oscillator readiness.
   PLL readiness is required only if the existing configuration enables it.
6. Print `CAN SPI CHECK PASS (0)` only when those checks pass. Release/press
   SW0 again to repeat. A pass establishes MCP251863-compatible SPI responses
   and reported clock readiness, not physical CAN communication.

There are no register writes, reset commands, message RAM writes, CAN send
operations or AT86RF215 commands in this executable. `CONFIG_CAN=n` and a
diagnostic-only devicetree compatible prevent the production CAN driver's
register-writing initialization from running. The radio SPI controller is
disabled in this application's overlay; no radio driver is linked.

## Files

| File | Purpose |
|---|---|
| `src/main.c` | Real SPI/GPIO adapter, SW0 trigger and measured console results |
| `app.overlay` | This app's mounted-board pin mapping; independent of the RF overlay |
| `prj.conf` | Enables SPI/GPIO/CRC; disables the production CAN stack |
| `CMakeLists.txt` | Builds only this app and its read-only probe core |
| `../../../src/can/mcp251863_probe.c` | Builds READ_CRC commands and checks responses |
| `../../../include/ocs/mcp251863_probe.h` | Small transport/snapshot interface |
| `../../../dts/bindings/can/microchip,mcp251863-probe.yaml` | Diagnostic SPI child properties |
| `../../software/can_probe/` | Simulated protocol/error tests, never linked into the board app |

## Build, monitor, then flash

Build without touching the hardware:

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/can_bringup" -d /tmp/ocs-can-bringup
```

Find the Nano's nEDBG port, not the PortaPack port:

```sh
python -m serial.tools.list_ports -v
python -m serial.tools.miniterm /dev/cu.usbmodemXXXXXXXX 115200
```

Replace the placeholder with the detected nEDBG port. Keep the monitor open.
After checking the setup above, flash from another activated Zephyr terminal:

```sh
west flash -d /tmp/ocs-can-bringup -r openocd --verify
```

Flashing replaces the radio application on the Nano. It does not create a
combined CAN/RF application. Release SW0, then press it to run the probe.
On October 24, 2026, the operator supplied an actual successful run: DEVID `0`,
OSC `0x460`, CiCON `0x04980760`, `OPMOD=4`, STBY high, ten valid CRC-protected
snapshots and `CAN SPI CHECK PASS (0)`. That establishes the read-only SPI/clock
stage; it does not establish physical CANH/CANL communication.

## Errors and next tests

| Result | Meaning |
|---|---|
| `0` | All ten SPI/clock snapshots passed |
| `-16` / `-EBUSY` | STBY sampled LOW; no SPI probe performed for that snapshot |
| `-74` / `-EBADMSG` | SPI response CRC mismatch; investigate wiring, power and signal quality |
| `-11` / `-EAGAIN` | SPI CRC passed, but reported oscillator/PLL state is not ready |
| `-5` / `-EIO` | Transport failure or DEVID changed between snapshots |
| `-19` / `-ENODEV` | Zephyr MCU SPI/GPIO device unavailable before probing U4 |

After this passes, use the separate [internal-loopback test](../can_loopback/README.md).
It explicitly selects internal mode because the installed Zephyr driver's generic
loopback option selects external mode. Physical CANH/CANL validation then needs known termination,
bit rate, transceiver power/standby, and another CAN node. No claim about those
stages follows from this read-only probe.

## References

- [MCP251863 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP251863-Data-Sheet-DS20006624A.pdf): §4 OSC/DEVID/CiCON, §5.3 SPI CRC, §8.1 transceiver standby.
- [SAMD21 Curiosity Nano guide, page 11](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/UserGuides/SAMD21-Curiosity-Nano-Evaluation-Kit-User-Guide-DS70005409D.pdf).
