# Board Information

## Red Board (SAMD21 Curiosity Nano)
Our chip is the ATSAMD21G17D, however, in zephyr it's defined as a 17a.
This is done because zephyr 4.5.0-rc1 does not include an entry for the D, research needed to 
ensure compatibility and differences.

## Pin Assignments
| Peripheral                | Pin                 | Connected To                 | Implemented? | Verified on Board? |
|---------------------------|---------------------|------------------------------|--------------|--------------------|
| LED (Nano user LED)       | PB10                | N/a                          | Yes          | No                 |
| Switch (Nano user button) | PB11                | N/a                          | Yes          | No                 |
| Radio SPI MISO            | PA19 (SERCOM1 PAD3) | AT86RF215 MOSI               | Yes          | No                 |
| Radio SPI SS              | PA18 (SERCOM1 PAD2) | AT86RF215 SELN               | Yes          | No                 |
| Radio SPI SCK             | PA17 (SERCOM1 PAD1) | AT86RF215 SCLK               | Yes          | No                 |
| Radio SPI MOSI            | PA16 (SERCOM1 PAD0) | AT86RF215 MISO               | Yes          | No                 |
| Radio reset               | PA01                | AT86RF215 RSTN (active low)  | Yes          | No                 |
| Radio IRQ                 | PA20                | AT86RF215 IRQ                | Yes          | No                 |
| Radio clock out           | PA23                | AT86RF215 CLKO               | No           | No                 |
| CAN SPI MISO              | TBD (SERCOM0 PAD?)  | CAN controller, net CAN-MISO | No           | No                 |
| CAN SPI MOSI              | TBD (SERCOM0 PAD?)  | CAN controller, net CAN-MOSI | No           | No                 |
| CAN SPI SCK               | TBD (SERCOM0 PAD?)  | CAN controller, net CAN-SCL  | No           | No                 |
| CAN SPI CS                | TBD (SERCOM0 PAD?)  | CAN controller, net CAN-CS   | No           | No                 |
| CAN standby               | PA14                | CAN controller, net CAN-STBY | No           | No                 |
| CAN interrupt             | PA15                | CAN controller, net CAN-INT  | No           | No                 |
| CAN interrupt 0           | PA24                | CAN controller, net CAN-INT0 | No           | No                 |
| CAN interrupt 1           | PA25                | CAN controller, net CAN-INT1 | No           | No                 |              