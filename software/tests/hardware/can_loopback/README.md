# MCP251863 internal CAN loopback

This is the next test after [read-only CAN SPI bring-up](../can_bringup/README.md).
It sends one frame through the CAN controller's internal transmit/receive path,
reads the received frame back over SPI, and compares its identifier, length and
all eight data bytes. No second CAN node is needed.

## Hardware setup and boundaries

Keep the Nano mounted directly in both green-board headers, powered through the
Nano debugger USB. No new jumpers, voltage changes or external power are needed.
Leave the separate external CAN connector J5 unconnected for the first run;
do not unplug the two Nano mounting headers. Power/wiring assumptions are the
same as in the read-only test; software cannot measure the actual supply rails.

This application uses the read-only test's SERCOM0 pin mapping: MOSI PA04, SCK
PA05, MISO PA06, CS PA07, main INT PA03 and standby PA02. PA14 is the radio's
reset pin and is not driven by this test. STBY, INT and SW0 remain MCU inputs.

The internal transceiver pull-up must hold STBY HIGH. The test checks it before
writing and during waits. It refuses to take over a controller that is not
already in Configuration mode. Once SW0 is pressed, this test **does reset U4
and write its CAN registers and message RAM**. It is not the read-only probe.

Only Internal Loopback mode (`OPMOD=2`) is selected. The Microchip reference
manual states that TXCAN stays high in this mode; the receive signal is routed
internally. The test also requires physical STBY high and never requests Normal
mode or External Loopback mode. The RF driver is not linked and SERCOM1 is
disabled. This test does not send a 915 MHz signal.

This proves more of the digital controller than a register read, but it does
not establish operation of CANH/CANL, the cable/termination, another CAN node,
CAN FD frames or a combined CAN-to-radio application.

## What the test does

1. Boot and wait for a fresh SW0 press. Holding SW0 at startup does not run it.
2. Check STBY, CRC-protected status reads, oscillator readiness and existing
   Configuration mode. No writes occur if these preconditions fail.
3. Reset the controller while in Configuration mode. Initialize its 2 KB
   message RAM with word-aligned CRC-protected writes. Configure one TX FIFO,
   one RX FIFO, an exact standard-ID filter and the RX interrupt.
4. Set nominal bit timing for 500 kb/s using the schematic's 40 MHz crystal;
   handle the SYSCLK divide-by-two setting. The clock frequency and on-bus bit
   rate are not independently measured by internal loopback.
5. Request Internal Loopback, then verify `OPMOD=2` before using FIFO addresses.
   Validate that both 16-byte message objects fit in RAM and do not overlap.
6. Load ID `0x123`, DLC `8`, payload `CAN TEST`. Read back the TX object before
   requesting transmission. A further state read must still show `OPMOD=2`.
7. Request one internal transmission. Wait for completion and a received frame.
8. Check the actual active-low INT pin and receive status. Read RX RAM using
   CRC-protected SPI, then compare ID, DLC, frame flags and all payload bytes.
9. Release the received FIFO object and check that the interrupt clears. Check
   transmit errors and CAN error counters.
10. Disable interrupt enables, request Configuration mode and verify both the
    actual mode and disabled enables using CRC-protected reads. This cleanup
    is also attempted after errors, even if STBY has dropped low.

The operation has a 2000 ms software deadline plus up to 200 ms for cleanup.
Synchronous SPI calls can add time; this is not a hard real-time cancellation
guarantee. If cleanup fails, the app reports it and stops accepting repeats.
It cannot guarantee that a command reached a chip after a transport failure.

The installed Zephyr `can_mcp251xfd.c` maps `CAN_MODE_LOOPBACK` to **External
Loopback**, not Internal Loopback. This diagnostic therefore uses a small,
repository-local register sequence on top of Zephyr SPI/GPIO instead. It does
not patch the Zephyr checkout or access its driver's private state.
`CONFIG_CAN=n` prevents a production driver from initializing the chip before
the button/preflight checks. This bench code is not a replacement general CAN
driver; normal CAN networking should use the existing Zephyr CAN stack.

## Terms and files

| Term | Meaning |
|---|---|
| FIFO | A queue in the chip's message RAM; TX supplies frames and RX stores frames |
| ID | CAN frame identifier; this test uses standard 11-bit ID `0x123` |
| DLC | Data length code; `8` means eight bytes for this classic CAN frame |
| STBY | Physical transceiver standby input; HIGH keeps it in standby |
| OPMOD / REQOP | Actual operating mode / requested operating mode |
| IRQ / INT | Interrupt event / the active-low electrical interrupt pin |
| CRC | Error-detection calculation protecting SPI command/data transfers |

Classic CAN allows up to eight payload bytes per frame, so this test uses
`CAN TEST` rather than the radio's long Hello world payload. SPI chunks and CAN
frames are different things. IRQ is checked by polling its actual GPIO level;
the SAMD21 interrupt-service-routine path is not tested here.

| Location | Purpose |
|---|---|
| `src/main.c` | Real SPI/GPIO adapter, SW0 trigger and actual console results |
| `app.overlay` | Reuses the read-only test's mounted-board mapping |
| `prj.conf` | Zephyr SPI/GPIO/CRC options, with production CAN disabled |
| `CMakeLists.txt` | Builds the hardware app and both CAN diagnostic cores |
| `../../../src/can/mcp251863_loopback.c` | Internal mode, FIFOs, filtering, frame validation and cleanup |
| `../../../include/ocs/mcp251863_loopback.h` | Transport/GPIO callbacks and diagnostic result structure |
| `../../software/can_loopback/` | Seventeen software tests with a simulated chip/GPIO transport |

The core's `STEP` macro records the current stage, executes an operation, and
jumps to cleanup on error. Register words and CAN message objects use little-
endian byte order; the SPI command/address and CRC fields use their specified
wire order. The expected message is used for TX and comparison; the RX result
always comes from reading the chip's RX FIFO memory.

## Build, monitor and flash

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/can_loopback" -d /tmp/ocs-can-loopback
```

Keep the Nano serial monitor open at 115200 baud. Discover the nEDBG port with
`python -m serial.tools.list_ports -v` if it has changed. Flash from another
activated Zephyr terminal:

```sh
west flash -d /tmp/ocs-can-loopback -r openocd --verify
```

Flashing replaces the currently running application. Release SW0 at startup,
then press it. Conditional success output is:

```text
PASS verified OPMOD=2 (internal loopback) before TXREQ
RX actual: ID=0x123 DLC=8 data=43 41 4e 20 54 45 53 54
RX text: CAN TEST
PASS ID, DLC and all eight bytes matched
PASS RX interrupt asserted then cleared
PASS returned to Configuration mode with interrupts disabled
CANH/CANL physical communication remains untested
CAN INTERNAL LOOPBACK PASS (0)
```

The app prints a trailing space after each hex byte. On failure, preserve the
`FAIL stage=... error=... cleanup=...` line. A missing frame produces a timeout;
wrong frame bytes produce `-EBADMSG`; low STBY or a controller outside
Configuration mode produces `-EBUSY`. Do not interpret a software build/test
pass as a measured hardware result. Physical loopback has not yet been run
for this new application.

## References

- [Microchip family reference manual](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/ReferenceManuals/MCP25XXFD-CAN-FD-Controller-Module-Family-Reference-Manual-DS20005678E.pdf): §2.6.3 internal vs external loopback; §3 configuration; §4/6/7/8 message/FIFO operation.
- [MCP251863 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP251863-Data-Sheet-DS20006624A.pdf): CAN registers, message-object layout, SPI CRC and standby.
