# Board Information

## Red Board (SAMD21 Curiosity Nano)
Our chip is the ATSAMD21G17D, however, in zephyr it's defined as a 17a.
This is done because zephyr 4.5.0-rc1 does not include an entry for the D, research needed to 
ensure compatibility and differences.

## Pin Assignments
| Peripheral                | Pin                 | Connected To                 | Implemented? | Verified on Board? |
|---------------------------|---------------------|------------------------------|--------------|--------------------|
| LED (Nano user LED)       | PB10                | N/a                          | Yes          | Yes                |
| Switch (Nano user button) | PB11                | N/a                          | Yes          | Yes                |
| Radio SPI MOSI            | PA19 (SERCOM1 PAD3) | AT86RF215 MOSI               | Yes          | Yes                |
| Radio SPI SS              | PA18 (SERCOM1 PAD2) | AT86RF215 SELN               | Yes          | Yes                |
| Radio SPI SCK             | PA17 (SERCOM1 PAD1) | AT86RF215 SCLK               | Yes          | Yes                |
| Radio SPI MISO            | PA16 (SERCOM1 PAD0) | AT86RF215 MISO               | Yes          | Yes                |
| Radio reset               | PA01                | AT86RF215 RSTN (active low)  | Yes          | Yes                |
| Radio IRQ                 | PA20                | AT86RF215 IRQ                | Yes          | No                 |
| Radio clock out           | PB23                | AT86RF215 CLKO               | No           | Yes                |
| CAN SPI MISO              | PA05 (SERCOM0 PAD1) | CAN controller, net CAN-MISO | Yes          | No                 |
| CAN SPI MOSI              | PA11 (SERCOM0 PAD3) | CAN controller, net CAN-MOSI | Yes          | No                 |
| CAN SPI SCK               | PA10 (SERCOM0 PAD2) | CAN controller, net CAN-SCL  | Yes          | No                 |
| CAN SPI CS                | PA04 (SERCOM0 CS)   | CAN controller, net CAN-CS   | Yes          | No                 |
| CAN standby               | PA14                | CAN controller, net CAN-STBY | Yes          | No                 |
| CAN interrupt             | PA15                | CAN controller, net CAN-INT  | Yes          | No                 |
| CAN interrupt 0           | PA24                | CAN controller, net CAN-INT0 | No           | No                 |
| CAN interrupt 1           | PA25                | CAN controller, net CAN-INT1 | No           | No                 |              