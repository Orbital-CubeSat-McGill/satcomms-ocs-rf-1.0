# Using the OCS radio SPI API

Include one header:

```c
#include <ocs/radio.h>
```

This API is a set of five public functions for the single AT86RF215 mounted
on the OCS RF board. It hides the radio structure, context pointer, command
bytes, chip-select handling and Zephyr SPI structures. The device tree still
supplies the board's actual pins and SPI speed.

For real RF packets, see [the packet radio API](radio-packets.md). Any raw SPI
write attempt invalidates that layer's packet configuration; configure again
before sending or receiving packets. Raw memory readback itself remains a
wired SPI operation.

## Functions

| Function | Purpose |
|---|---|
| `ocs_radio_init()` | Configure/reset the radio and check its part number is `0x34`. |
| `ocs_radio_read(address, data, length)` | Read bytes from radio registers or memory into your array. |
| `ocs_radio_write(address, data, length)` | Write bytes from your array into radio registers or memory. |
| `ocs_radio_read_reg(address, &value)` | Read one register byte. |
| `ocs_radio_write_reg(address, value)` | Write one register byte. |

Every function returns an `int`: `0` means success; a negative value means
failure. Check it before using received data or continuing a dependent action.

Call `ocs_radio_init()` once before using the other functions. Calling it again
resets the radio again and discards its previous radio configuration. If it
fails, read/write calls return `-ENODEV` until initialization succeeds.

## Read the part-number register

This code belongs inside a Zephyr thread, such as `main()`:

```c
int result = ocs_radio_init();
if (result != 0) {
    printk("Radio initialization failed: %d\n", result);
    return result;
}

uint8_t part_number;
result = ocs_radio_read_reg(OCS_RADIO_REG_PN, &part_number);
if (result != 0) {
    printk("Part-number read failed: %d\n", result);
    return result;
}

printk("Radio part number: 0x%02x\n", (unsigned int)part_number);
```

`OCS_RADIO_REG_PN` is address `0x000D` inside the radio chip. It is unrelated
to the Mac's USB serial name. The AT86RF215 supplies this read-only value; the
API does not write `0x34` into it. Initialization reads it as a presence check.

`uint8_t` stores one unsigned byte. `&part_number` means "the address of this
variable": the function uses that address to store the byte it reads.
`%02x` displays a hexadecimal number using at least two digits.

## Complete Hello world example

The project's default `tests/hardware/spi_bringup/src/main.c` remains the fuller diagnostic program. To
use the API in another application, the following is a complete minimal main
file. The API tests exercise its write/read operations against a simulated
chip; they do not execute this example's main function.

```c
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ocs/radio.h>

int main(void)
{
    /* Give an already-open serial monitor time to see the application. */
    k_msleep(1500);

    int result = ocs_radio_init();
    if (result != 0) {
        printk("FAIL radio init: %d\n", result);
        return result;
    }

    const char message[] = "Hello world";
    char received[sizeof(message)] = {0};
    size_t length = sizeof(message) - 1;

    result = ocs_radio_write(OCS_RADIO_TX_BUFFER, message, length);
    if (result != 0) {
        printk("FAIL SPI write: %d\n", result);
        return result;
    }

    result = ocs_radio_read(OCS_RADIO_TX_BUFFER, received, length);
    if (result != 0) {
        printk("FAIL SPI read: %d\n", result);
        return result;
    }

    printk("SPI received: %s\n", received);

    if (memcmp(message, received, length) != 0) {
        printk("FAIL received bytes differ\n");
        return -1;
    }

    printk("PASS Hello world over wired SPI\n");
    return 0;
}
```

This example leaves the message in the TX memory buffer. It does not command
the radio to transmit it. The default bring-up program additionally saves and
restores the original memory so it can test without leaving its pattern there.

The code means:

- `const char message[]` creates an array of characters that this code will
  not change. C appends a zero byte, `\0`, to mark the end of the string.
- `sizeof(message)` is 12: eleven message bytes plus that final zero byte.
- `char received[sizeof(message)] = {0}` allocates twelve writable bytes and
  initializes all of them to zero.
- `size_t` is the C type used for sizes. `length` is 11, so the ending zero
  is not transferred. The twelfth received-array byte stays zero, making
  the `%s` print safe for this example.
- `OCS_RADIO_TX_BUFFER` is the radio-memory address `0x2800`.
- `ocs_radio_write()` sends a write command, that address and the message.
- `ocs_radio_read()` sends a separate read command for that same address
  and copies bytes returned over SPI into `received`.
- `memcmp()` compares exactly `length` bytes. Zero means they all match.
- `printk()` writes console output through the Nano's debugger serial
  connection to your Mac. Printing is separate from the SPI transfer.

For binary data, use an array such as `uint8_t data[] = {0x00, 0xFF, 0x80};`
and pass `sizeof(data)` without subtracting one. Binary data can contain zero
bytes; do not print arbitrary binary data with `%s`.

## Limits and errors

- Each read/write must contain 1 to `OCS_RADIO_MAX_TRANSFER` bytes, currently
  32. Longer messages are rejected; the API does not split them automatically.
- Addresses and their whole transfer must fit within `0x0000..0x3FFF`.
  Use locations documented for the radio: this numeric check does not make
  every address writable. For example, the part-number register is read-only.
- The caller owns the buffer and must provide at least `length` accessible
  bytes. The API cannot determine the allocated size from a pointer.
- `-EINVAL` means an invalid address, length or null data pointer after
  successful initialization.
- `-ENODEV` means initialization has not succeeded, or initialization found
  an unavailable/unrecognized chip.
- `-EWOULDBLOCK` means the caller is in an interrupt service routine (ISR).
  Use a thread or work item; the existing interrupt callback only signals
  another thread with a semaphore.
- Other negative transport errors are forwarded. The API adds no transfer
  timeout of its own. A failed read leaves your output buffer unchanged.
- A failed write might have partially reached the chip. A successful write
  reports transfer completion, not readback verification; read and compare
  if you need to verify stored bytes.

`void *data` means "a pointer to memory whose element type is unspecified."
It lets a read accept either a character array or a byte array. The accompanying
length says how many bytes may be written. `const void *data` on a write means
the function reads your bytes without modifying your source buffer. These are
data-buffer pointers; the lower-level `void *ctx` transport context is hidden.

## Threads and ownership

Calls block until they complete. A Zephyr mutex allows only one API call to
use the radio at a time, including initialization. A mutex is a lock shared
between threads; other callers wait for its owner to finish.

Several calls are not one atomic operation. Another thread can write between
your write and your read. Assign a single thread ownership of compound radio
procedures, or coordinate them at the application level. Calls to the older
low-level driver, including reset/IRQ setup, bypass this API mutex; do not mix
them concurrently with these functions. The diagnostic main uses those
advanced GPIO/IRQ functions only from its single main thread.

## Files and implementation layers

| File | Responsibility |
|---|---|
| `include/ocs/radio.h` | Public functions, addresses, limits and contracts. |
| `src/radio/ocs_radio.c` | Owns the single radio instance; initializes/probes it and serializes callers. |
| `src/radio/at86rf215.c` | Encodes chip SPI commands and validates transfer sizes/addresses. |
| `src/radio/at86rf215_packet.c` | Programs the RF profile and handles complete packet operations. |
| `src/radio/at86rf215_zephyr.c` | Connects the commands to physical Zephyr SPI/reset/IRQ drivers. |
| `app.overlay` and board files | Select actual pins and peripheral settings. |
| `tests/hardware/spi_bringup/src/main.c` | Exercises the public API on real hardware, plus GPIO/IRQ checks. |
| `tests/software/radio_api/` | Exercises the real public API and protocol with a simulated chip. |
| `tests/software/radio_core/` | Exercises the lower-level command encoding and validation. |

The firmware build links the real board adapter. Only the QEMU test
executables supply chip models instead of that adapter.
The existing application CMake file already includes the public header path
and all four driver sources. Another Zephyr application needs those sources,
the include path, GPIO/SPI configuration, binding and device-tree setup too.

## Build and test on this Mac

Run from the Zephyr workspace:

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"

west twister -T "$OCS_RF_REPO/software/tests/software" -p qemu_cortex_m3 --inline-logs --outdir /tmp/ocs-rf-api-tests
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/spi_bringup" -d /tmp/ocs-rf-spi-hardware
```

There are ten core cases, nine SPI API cases and twenty-one packet cases. The
SPI API cases exercise identity and
register access, text/binary buffers, invalid inputs, initialization failure
and recovery, wrong-chip rejection, transport errors, ISR rejection, and two
threads contending for the same radio. These simulated tests cannot establish
physical wiring or radio operation.

With the boards connected, find the current port and open a monitor in a
separate activated terminal:

```sh
python -m serial.tools.list_ports
python -m serial.tools.miniterm /dev/cu.usbmodemXXXXXXXX 115200
```

Replace the placeholder with the detected Nano port. Then flash from the
Zephyr-workspace terminal:

```sh
west flash -d /tmp/ocs-rf-spi-hardware -r openocd --verify
```

Look for `PASS simple API init/presence check`, the actual PN/VN values,
`SPI received: Hello world` and `BRING-UP PASS (0)`. Physical verification
requires fresh serial output from this API version, not an older saved log.
