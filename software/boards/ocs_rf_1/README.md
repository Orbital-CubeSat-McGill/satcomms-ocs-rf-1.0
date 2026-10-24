# OCS RF 1 board configuration

The controller is a SAMD21 Curiosity Nano with an ATSAMD21G17D. The existing
Zephyr target is `ocs_rf_1/samd21g17a`; the installed Zephyr checkout reports
4.4.99. A successful build for this configured target does not establish full
G17D/G17A compatibility. On October 22, the actual G17D booted this application
and passed its radio SPI/GPIO checks using the configured target.

## Directly mounted radio connections

The application now defaults to the Nano plugged directly into the RF board's
two headers, as confirmed on October 22, 2026. Do not add SPI/reset jumper
rerouting on top of these existing header connections.

| Radio signal | RF-board connector | Actual Nano pin | Application setting |
|---|---|---|---|
| MISO, radio output | J1 pin 11 | PA16 / SERCOM1 PAD0 | DIPO=0 |
| MOSI, radio input | J1 pin 12 | PA19 / SERCOM1 PAD3 | DOPO=2 |
| SCLK | J1 pin 13 | PA17 / SERCOM1 PAD1 | DOPO=2 |
| SELN, active low | J1 pin 14 | PA18 GPIO | cs-gpios from board DTS |
| IRQ, active high after reset | J1 pin 16 | PA20 GPIO | irq-gpios from app.overlay |
| RSTN, active low | J2 pin 23 | PA14 GPIO | reset-gpios from app.overlay |

This follows the RF schematic on local `main` at `bef9116`, the supplied
schematic images and Microchip Curiosity Nano header order (DS70005409D,
page 11). The top-level RF schematic crosses the sheet's SERCOM1 MOSI/MISO
labels. The reset label `PA01-RST` is attached to J2 pin 23, which is PA14 on
the standard Nano header. Labels alone are not reliable MCU pin assignments.
Confirm physical board revision and orientation against the connector positions.

The generic board DTS still supplies its original DIPO=3/DOPO=0 defaults and
the same peripheral-C pinmux group. `software/app.overlay` overrides DIPO/DOPO
for the mounted RF board and selects PA14 reset. Both data pins keep their
pinmux selections; their input/output roles change inside SERCOM1.

For separate boards with the previous jumper layout, the application overlay
would instead need DIPO=3, DOPO=0 and reset PA01. That is not the current setup.

## Power and console

- J2 pin 1 is +5V/VBUS; J2 pin 6 is +3V3/VTG; J2 pin 5 is ground.
- Confirm Nano VTG is 3.3V. Do not join +5V and +3V3 or parallel an external
  supply with the Nano regulator without following the Nano power procedure.
- The Nano's debugger USB connection supplies programming and virtual serial.
- The console is SERCOM5: PA22 TX and PB22 RX, 115200 baud.
- PB10 is the active-low Nano LED; PB11 is its user button, not a reset button.

## Test scope

The current application initializes/resets the radio and reads PN/VN over SPI.
It checks PN against `0x34` with single-register and block reads, and prints VN.
It no longer writes test patterns or runs state/IRQ/repeated-read checks.

The earlier full diagnostic also tested inactive RF09 state, reversible
register/RAM writes, reset WAKEUP interrupts and 100 repeated ID reads.
On October 22, 2026, that earlier sequence passed on the connected board:
PN=0x34, VN=0x03, RF09/RF24 WAKEUP status=0x01, and `BRING-UP PASS (0)`.

The ten Ztest cases use a fake transport and exercise protocol encoding,
length/address/pointer validation and error propagation. QEMU does not emulate
the actual radio. No CAN driver, RF packet transmission/reception, modulation,
antenna performance or link range is tested by this application.

The CAN wiring has additional MCU-label/header-position inconsistencies in the
schematic and needs a separate trace before CAN is enabled. The external
26MHz reference must also meet the AT86RF215 datasheet's TCXO requirements;
software cannot repair a missing or unsuitable reference clock.

See `software/README.md` for build, test and flash commands.
