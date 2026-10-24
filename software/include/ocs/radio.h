#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCS_RADIO_MAX_TRANSFER 32U
#define OCS_RADIO_REG_PN       0x000DU
#define OCS_RADIO_REG_VN       0x000EU
#define OCS_RADIO_RX_BUFFER    0x2000U
#define OCS_RADIO_TX_BUFFER    0x2800U
/* The 11-bit PHY frame length includes the four-byte CRC-32. */
#define OCS_RADIO_MAX_PAYLOAD  2043U

struct ocs_radio_config {
    uint32_t frequency_hz;
    uint8_t tx_power_level; /* Chip setting 0..31, not dBm. Start at 0. */
};

/**
 * Initialize and reset the board's AT86RF215, then verify its part number.
 * Call once before accessing it. Calling again resets the radio again.
 * A failed initialization disables access until initialization succeeds.
 *
 * All functions in this header are synchronous and thread-only. Calls through
 * this API share a mutex; a sequence of several calls is not one transaction.
 * Do not concurrently access the same radio through the low-level API.
 *
 * Returns 0 on success, -ENODEV for an unavailable/unrecognized radio, or a
 * negative transport error. Calling from an ISR returns -EWOULDBLOCK.
 */
int ocs_radio_init(void);

/**
 * Read 1..OCS_RADIO_MAX_TRANSFER bytes starting at a radio address.
 * data points to writable storage of at least length bytes. Addresses must
 * stay within 0x0000..0x3FFF. Failed reads leave data unchanged.
 *
 * Returns 0 on success, -EINVAL for invalid arguments, -ENODEV before/after
 * failed initialization, -EWOULDBLOCK in an ISR, or a negative transport error.
 * The address selects radio registers/memory, not a Mac serial port.
 */
int ocs_radio_read(uint16_t address, void *data, size_t length);

/**
 * Write 1..OCS_RADIO_MAX_TRANSFER bytes starting at a radio address.
 * data points to readable storage of at least length bytes. Address limits
 * and error returns match ocs_radio_read(). Data can contain zero bytes.
 * A transport error does not guarantee that no bytes reached the chip.
 *
 * This writes registers/memory over SPI. It does not transmit RF packets.
 * Choose writable locations; success alone does not verify their contents.
 * Any raw write attempt invalidates packet configuration; configure again
 * before using send/receive. Internal packet-engine writes do not do this.
 */
int ocs_radio_write(uint16_t address, const void *data, size_t length);

/** Read one register byte; errors match ocs_radio_read(). */
int ocs_radio_read_reg(uint16_t address, uint8_t *value);

/** Write one register byte; errors match ocs_radio_write(). */
int ocs_radio_write_reg(uint16_t address, uint8_t value);

/**
 * Reset/configure RF09/BBC0: VN03, 2-FSK, 50 kb/s, h=1, BT=0.5, CRC-32,
 * uncoded IEEE frames, 16-byte preamble and no TX whitening.
 * frequency_hz must be 863..928 MHz on a 25 kHz grid; power is 0..31.
 * Requires successful init; returns -ENOTSUP for unsupported silicon.
 * Successful configuration verifies TXPREP/PLL readiness and leaves TRXOFF.
 * Reconfigure after init or an error that invalidates packet configuration.
 */
int ocs_radio_configure(const struct ocs_radio_config *config);

/**
 * Transmit 1..OCS_RADIO_MAX_PAYLOAD payload bytes; hardware appends CRC-32.
 * Maximum payload is 2043 bytes: 2047-byte PSDU minus four CRC bytes.
 * Packet buffers are transferred in chunks; the raw SPI limit stays 32 bytes.
 * At 50 kb/s the largest frame takes about 331 ms on air; allow e.g. 1000 ms.
 * Returns 0 after TXFE, no under-run and successful TRXOFF cleanup. This
 * indicates local transmission completion, not acknowledgement by a peer.
 * Requires configure; -EACCES means packet configuration is unavailable.
 * Positive timeout_ms bounds mutex/event/state waits; synchronous SPI and
 * cleanup (up to 20 ms of state waiting) can add time. -ETIMEDOUT on timeout.
 * ISR calls return -EWOULDBLOCK. Errors attempt TRXOFF; failed cleanup or
 * radio/transport faults invalidate configuration. These are real RF calls.
 */
int ocs_radio_send(const void *data, size_t length, int32_t timeout_ms);

/**
 * Receive one CRC-valid frame and return its payload without the four-byte CRC.
 * On success stores bytes into data and their count into length. On failure
 * leaves both unchanged. -EBADMSG for bad CRC/header, -EMSGSIZE for a frame
 * outside the supported size or too large for capacity. Capacity must be >0.
 * Other requirements/errors match send. A normal timeout recovers to TRXOFF.
 */
int ocs_radio_receive(void *data, size_t capacity, size_t *length,
                      int32_t timeout_ms);

#ifdef __cplusplus
}
#endif
