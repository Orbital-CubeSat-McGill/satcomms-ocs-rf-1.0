# 915 MHz packet radio: green board and HackRF/Mayhem

## Status and purpose

This adds a real RF packet driver above the wired SPI API. The separate packet
bench application prepares the radio, receives packets, and sends one packet
when the Nano's SW0 button is pressed. The original application under
`software/tests/hardware/spi_bringup/src/main.c` remains the SPI/memory/interrupt diagnostic.

All 40 software tests pass in QEMU, and both applications compile for SAMD21.
Physical RF operation is **not yet verified**: the boards were
unavailable during implementation. No capture/replay success is claimed.

## How the pieces work

The SAMD21 runs Zephyr and your C application. It sends SPI commands to the
AT86RF215. The AT86RF215 handles modulation, radio-frequency conversion,
packet framing and the CRC. The HackRF is an independent radio that can record
the transmitted waveform and later transmit that recording back.

| Term | Meaning here |
|---|---|
| RF | Radio frequency: the actual electromagnetic signal. |
| PHY | Physical layer: the rules converting packet bits into a radio signal. |
| 2-FSK | Two frequency shifts represent the two binary symbols. |
| 50 kb/s | 50,000 uncoded payload-channel bits per second, before framing overhead. |
| Baseband / BBC0 | The AT86RF215 packet-processing core used with its RF09 radio. |
| PLL | Frequency synthesizer that locks the radio to the configured channel. |
| CRC / FCS | A calculated error-detection value / the field carrying that value. |
| Preamble / SFD | A synchronization pattern / the delimiter marking the frame start. |
| IRQ | The signal telling the SAMD21 that a radio event occurred. |
| TRXOFF | The radio's inactive state between operations. |
| TXPREP / TRXRDY | Prepared radio state / the event indicating readiness. |
| TXFE / RXFE | Transmit frame finished / receive frame finished. |
| I/Q samples | Recorded waveform samples, rather than decoded message bytes. |

## First supported profile

- AT86RF215 part number `0x34`, version `0x03` (your previously identified chip).
- RF09/BBC0, 915.000 MHz in the sample.
- 2-FSK, 50 ksymbol/s, modulation index 1 (nominal deviation 25 kHz), BT=0.5.
- Uncoded IEEE framing, SFD0, 16-octet preamble; TX whitening disabled.
- Hardware-generated CRC-32, hardware receive CRC filter, plus software checks
  of CRC status, received FCS type and frame length.
- Application payload: 1..32 bytes. The CRC adds four on-air bytes.
- Sample transmit power setting 0: the chip's minimum setting, **not 0 dBm**.
  Actual output power and frequency accuracy require measurement.

Version 1 requires different filter settings and errata handling; this initial
RF profile returns `-ENOTSUP` for it. The older raw SPI API remains separate.

## Public API

Include `<ocs/radio.h>`. The existing initialization and register/buffer calls
remain available, and these three functions add packet behavior:

```c
int ocs_radio_configure(const struct ocs_radio_config *config);
int ocs_radio_send(const void *data, size_t length, int32_t timeout_ms);
int ocs_radio_receive(void *data, size_t capacity, size_t *length,
                      int32_t timeout_ms);
```

Example statements inside a Zephyr thread:

```c
const struct ocs_radio_config config = {
    .frequency_hz = 915000000U,
    .tx_power_level = 0
};

int result = ocs_radio_init();
if (result == 0) {
    result = ocs_radio_configure(&config);
}
if (result == 0) {
    const char message[] = "Hello world";
    result = ocs_radio_send(message, sizeof(message) - 1, 100);
}
if (result != 0) {
    printk("Radio error: %d\n", result);
}
```

To receive, after successful initialization/configuration:

```c
uint8_t data[OCS_RADIO_MAX_PAYLOAD];
size_t length;
int result = ocs_radio_receive(data, sizeof(data), &length, 5000);

if (result == 0) {
    printk("Received %u CRC-valid payload bytes\n", (unsigned int)length);
} else if (result == -ETIMEDOUT) {
    printk("No matching packet arrived before the timeout\n");
} else {
    printk("Receive error: %d\n", result);
}
```

Include `<errno.h>` when comparing errno names. The complete buildable bench
application is `software/tests/hardware/radio_packet/src/main.c`.

`config` is a structure grouping radio settings. The dot selects a field;
`&config` passes its address. `915000000U` is an unsigned integer in hertz.
The power field is a setting from 0 to 31. Configuration checks that frequency
is within the board path's 863..928 MHz range and on a 25 kHz grid; this is not
a regulatory policy or a promise that every permitted hardware frequency is
appropriate for your particular test.

On receive, `data` is writable storage, `sizeof(data)` is its capacity in bytes,
and `&length` gives the function somewhere to store the received payload count.
Use the data and count only after a successful call. They remain unchanged on
failure. Binary payloads may include zero bytes; the sample prints bounded
text and hexadecimal bytes instead of treating arbitrary packets as strings.

## What send and receive actually do

Configuration resets the chip, selects the profile, programs interrupt masks,
briefly enters TXPREP, verifies TRXRDY/state/PLL lock, and returns to TRXOFF.
It does not issue a transmit command.

Sending clears old interrupt flags and programs the total frame length first.
Writing the length resets the chip's buffer-write tracking. The driver then
writes your payload and four placeholder CRC bytes into TX memory, prepares
the transmitter, starts TX, waits for TXFE and checks for buffer under-run.
The placeholders are required for underrun tracking even when automatic CRC
is enabled (datasheet section 6.13.4). The chip generates the actual CRC;
your array must contain only the payload.

Receiving clears old events, starts RX and waits for RXFE. The driver checks
CRC status and the received header, checks length/capacity, reads the payload
into temporary storage, and stops the radio. Only then does it publish bytes
and length to the caller. CRC bytes are excluded from the returned payload.

Every operation uses the same mutex as raw SPI calls, covering the whole
procedure. Interrupt callbacks only signal a semaphore; SPI transfers and
packet processing run in the calling thread. The radio is half duplex, so a
second caller waits; there is no background receive queue in this version.

## Errors and recovery

- `0`: operation succeeded. Send success means the local radio completed its
  transmission; it is not an acknowledgement from the HackRF.
- `-ENODEV`: initialize first, or the expected chip is unavailable.
- `-EACCES`: configure first, or a fault/raw write invalidated configuration.
- `-EINVAL`: invalid arguments, zero/nonpositive timeout, frequency or power.
- `-ENOTSUP`: unsupported chip version for this RF profile.
- `-ETIMEDOUT`: a mutex/event/state wait expired; it can also be forwarded
  from the transport. Normal RX timeout is expected when no packet arrives.
- `-EBADMSG`: bad CRC or unexpected FCS/header format.
- `-EMSGSIZE`: received frame is outside the supported size or exceeds capacity.
- `-EIO` and other negative transport errors: radio, transfer or IRQ failure.
- `-EWOULDBLOCK`: called from an interrupt; call from a thread instead.

The driver attempts TRXOFF cleanup. Normal packet timeouts preserve a usable
configuration when cleanup succeeds. Transport/radio faults or failed cleanup
invalidate it; configure again before sending/receiving. Any raw SPI **write
attempt** also invalidates packet configuration, because it may alter the PHY
or its state. A failed write can have partially reached the chip.

Timeouts include waiting for the mutex and radio events/state. Underlying
synchronous SPI transfers can add time, and cleanup allows up to 20 ms for
the stop-state wait. The API is not a hard real-time cancellation mechanism.
If the transport is broken, software cannot guarantee that a stop command
reached the chip. Read the exact error and correct the hardware fault.

## Build the two applications

Activate the existing environment, then run from the Zephyr workspace:

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"

# All simulated tests: ten core, nine SPI API, twenty-one packet cases.
west twister -T "$OCS_RF_REPO/software/tests/software" -p qemu_cortex_m3 --inline-logs --outdir /tmp/ocs-rf-packet-tests

# Existing SPI-only diagnostic application.
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/spi_bringup" -d /tmp/ocs-rf-spi-hardware

# Separate RF packet bench application.
west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/tests/hardware/radio_packet" -d /tmp/ocs-rf-packet-hardware
```

The packet sample's CMake file imports the same board, binding and mounted-board
overlay from `software/`. It links the real hardware adapter, not the test model.
The configured G17A target is the existing target for your actual G17D; the new
RF path needs fresh physical verification on that hardware.

## Prepare the physical RF setup

1. Mount the Nano correctly in both RF-board headers and connect its debugger
   USB to the Mac. Use the existing 3.3 V setup.
2. Use the RF09/sub-GHz antenna connector (J3 in your supplied schematic), with
   an appropriate 915 MHz antenna/load. Use an appropriate antenna on HackRF too.
3. Start with radio power level 0 and the HackRF RX amplifier off, low gain,
   and antenna bias power off. Adjust receiver gain if necessary.
4. **Do not join the two RF connectors with an unattenuated coax cable.**
   HackRF specifies a maximum input of -5 dBm. A conducted setup needs suitable
   50-ohm attenuation and a checked power budget before connection.

Your schematic's 26 MHz TCXO/reference wiring still needs hardware review.
A failed PLL/configuration test may require checking the reference waveform,
supply and XTAL2 connection against the datasheet. No software clock repair or
component modification is claimed here.

## Flash and check readiness first

Open a serial monitor in one activated terminal, selecting the detected Nano:

```sh
python -m serial.tools.list_ports
python -m serial.tools.miniterm /dev/cu.usbmodemXXXXXXXX 115200
```

In the Zephyr-workspace terminal:

```sh
west flash -d /tmp/ocs-rf-packet-hardware -r openocd --verify
```

Expected readiness output includes actual PN/VN and these messages only if
configuration and the PLL checks succeed:

```text
PASS RF configuration: 915 MHz, 2-FSK 50 kb/s, CRC32
PLL readiness checked; radio returns to TRXOFF between operations
Ready: receiving. Press SW0 for about half a second to send once.
```

Keep the button released initially. A button held at boot must be released
before it can trigger a transmission. The sample receives in repeated 100 ms
windows and prints a waiting message about every five seconds.

## Mayhem capture, then replay back to the green board

This is a proposed operator-assisted validation procedure, not a result that
has already happened. Exact menus depend on your installed Mayhem version.

1. Open **Capture**. Use center frequency **915.100 MHz**, sample rate
   **500 kHz**, format **C16**, and a suitable SD card. The wanted radio signal
   is at 915.000 MHz; this deliberate 100 kHz offset keeps it away from the
   receiver's center/DC artifact while inside the capture bandwidth.
2. Start recording. Press and hold Nano SW0 for about half a second, then
   release it. Each new press sends one `Hello world` packet. Record several
   presses over a few seconds and confirm local `TX complete` messages.
3. Stop Capture. Preserve the resulting `.C16` file **and matching `.TXT`
   metadata**. Check the reported dropped samples; start with a capture
   reporting no drops. A waterfall burst alone does not identify the payload.
4. Leave the green board receiving with SW0 released. Open **Replay** and
   select your own captured file. Retain its **915.100 MHz center frequency**
   and **500 kHz sample rate**. Do not change Replay to 915.000 MHz: that would
   shift the recorded off-center signal to the wrong radio frequency.
5. Start with low replay gain and the TX amplifier off. Replay your capture;
   repeat if necessary so packets overlap the green board's receive windows.
6. Look for actual terminal output:

```text
RX CRC OK (11 bytes): Hello world
RX hex: 48 65 6c 6c 6f 20 77 6f 72 6c 64
PASS RF receive: expected Hello world
```

7. Stop Replay. Matching packet messages should stop. That is a useful negative
   control: silence should produce waiting/timeouts, not fabricated payloads.

The HackRF/PortaPack does not need to understand the packet format to record
and retransmit its waveform. In this test the AT86RF215 performs the actual
packet decoding and CRC validation when the recording is replayed. Independent
decoding on the Mac would be an additional test, not something the waterfall
or Capture screen establishes.

## What the software tests prove

The packet tests run the real public API, mutex, SPI command encoder and packet
engine with a test-only chip/IRQ model. They check the programmed profile,
CRC-inclusive TX lengths, length-before-buffer ordering and required FCS
placeholders (including a 32-byte payload), stale events, PLL/IRQ/transport faults,
timeouts, contention, under-run, CRC/header rejection, receive sizes, output
preservation, cleanup invalidation, raw-write invalidation and ISR rejection.
They do not emulate the analog radio, oscillator, antenna, waveform or HackRF.

## References

- [AT86RF215 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/Atmel-42415-WIRELESS-AT86RF215_Datasheet.pdf): sections 6.3, 6.9, 6.10, 6.13, 7.2; tables 6-51, 6-54, 6-62; version/errata table.
- [HackRF One specifications and maximum input power](https://hackrf.readthedocs.io/en/stable/hackrf_one.html).
- [Mayhem Capture](https://github.com/portapack-mayhem/mayhem-firmware/wiki/Capture).
- [Mayhem Replay](https://github.com/portapack-mayhem/mayhem-firmware/wiki/Replay).
