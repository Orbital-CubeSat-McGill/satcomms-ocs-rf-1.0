#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <ocs/at86rf215_zephyr.h>
#include "at86rf215_packet.h"
#include "at86rf215_regs.h"

#define CRC_BYTES 4U
#define CLEANUP_MS 20
#define CONFIGURE_MS 50
BUILD_ASSERT(OCS_RADIO_MAX_PAYLOAD <= AT86RF215_IO_MAX);

static int read_block(struct at86rf215_packet *p, uint16_t address,
                       void *data, size_t length)
{
    int result = at86rf215_read_block(p->radio, address, data, length);
    if (result != 0) { p->configured = false; }
    return result;
}

static int write_block(struct at86rf215_packet *p, uint16_t address,
                        const void *data, size_t length)
{
    int result = at86rf215_write_block(p->radio, address, data, length);
    if (result != 0) { p->configured = false; }
    return result;
}

static int read_byte(struct at86rf215_packet *p, uint16_t address, uint8_t *value)
{
    return read_block(p, address, value, 1);
}

static int write_byte(struct at86rf215_packet *p, uint16_t address, uint8_t value)
{
    return write_block(p, address, &value, 1);
}

static int read_events(struct at86rf215_packet *p, uint8_t events[4])
{
    /* One snapshot clears all four sources sharing the physical IRQ pin. */
    return read_block(p, AT86RF215_REG_RF09_IRQS, events, 4);
}

static int wait_event(struct at86rf215_packet *p, unsigned int source,
                      uint8_t mask, int64_t deadline)
{
    for (;;) {
        uint8_t events[4];
        int result = read_events(p, events);
        if (result != 0) { return result; }
        if ((events[0] & AT86RF215_IRQ_TRXERR) != 0) { return -EIO; }
        if ((events[source] & mask) != 0) { return 0; }
        int64_t remaining = deadline - k_uptime_get();
        if (remaining <= 0) { return -ETIMEDOUT; }
        result = at86rf215_zephyr_wait_irq((int32_t)remaining);
        if (result != 0 && result != -ETIMEDOUT) {
            p->configured = false;
            return result;
        }
        /* Inspect latched status after a timeout or an old semaphore signal. */
    }
}

static int wait_state(struct at86rf215_packet *p, uint8_t wanted, int64_t deadline)
{
    for (;;) {
        uint8_t state;
        int result = read_byte(p, AT86RF215_REG_RF09_STATE, &state);
        if (result != 0) { return result; }
        if ((state & 7U) == wanted) { return 0; }
        if (k_uptime_get() >= deadline) { return -ETIMEDOUT; }
        k_msleep(1);
    }
}

static int finish(struct at86rf215_packet *p, int result)
{
    int stopped = write_byte(p, AT86RF215_REG_RF09_CMD, AT86RF215_CMD_TRXOFF);
    if (stopped == 0) {
        stopped = wait_state(p, AT86RF215_STATE_TRXOFF,
                             k_uptime_get() + CLEANUP_MS);
    }
    if (stopped != 0 || (result != 0 && result != -ETIMEDOUT &&
                         result != -EBADMSG && result != -EMSGSIZE)) {
        p->configured = false;
    }
    return result != 0 ? result : stopped;
}

static int prepare(struct at86rf215_packet *p, int64_t deadline)
{
    if (k_uptime_get() >= deadline) { return -ETIMEDOUT; }
    int result = write_byte(p, AT86RF215_REG_RF09_CMD, AT86RF215_CMD_TXPREP);
    if (result == 0) { result = wait_event(p, 0, AT86RF215_IRQ_TRXRDY, deadline); }
    if (result == 0) { result = wait_state(p, AT86RF215_CMD_TXPREP, deadline); }
    if (result == 0) {
        uint8_t pll;
        result = read_byte(p, AT86RF215_REG_RF09_PLL, &pll);
        if (result == 0 && (pll & AT86RF215_PLL_LOCKED) == 0) { result = -EIO; }
    }
    return result;
}

int at86rf215_packet_configure(struct at86rf215_packet *p,
                               const struct ocs_radio_config *config)
{
    if (config == NULL || config->frequency_hz < 863000000U ||
        config->frequency_hz > 928000000U ||
        config->frequency_hz % 25000U != 0 || config->tx_power_level > 31) {
        return -EINVAL;
    }
    p->configured = false;
    uint8_t identity[2];
    int result = read_block(p, OCS_RADIO_REG_PN, identity, 2);
    if (result != 0) { return result; }
    if (identity[0] != 0x34) { return -ENODEV; }
    /* v1 needs different filters and errata handling; support actual VN03. */
    if (identity[1] != 3) { return -ENOTSUP; }
    result = at86rf215_zephyr_reset();
    if (result != 0) { return result; }
    result = at86rf215_zephyr_irq_init();
    if (result != 0) { return finish(p, result); }

    uint16_t base = (uint16_t)(config->frequency_hz / 25000U);
    /* Mode 0: f=CCF0*25kHz + CN*CS*25kHz. CN=0; write CNM last. */
    uint8_t channel[] = {1, (uint8_t)base, (uint8_t)(base >> 8), 0, 0};
    result = write_block(p, AT86RF215_REG_RF09_CS,
                                   channel, sizeof(channel));
    /* Datasheet tables 6-51, 6-54, 6-62: VN03, 50ksymbol/s, h=1. */
    const struct { uint16_t address; uint8_t value; } settings[] = {
        {AT86RF215_REG_RF09_RXBWC, 0x00},
        {AT86RF215_REG_RF09_RXDFE, 0x2A}, /* RCUT=1, SR=10. */
        {AT86RF215_REG_RF09_AGCC, 0x01}, /* Automatic gain enabled. */
        {AT86RF215_REG_RF09_AGCS, 0x20}, /* Target=1. */
        {AT86RF215_REG_RF09_TXCUTC, 0xC0}, /* PARAMP=3, LPFCUT=0. */
        {AT86RF215_REG_RF09_TXDFE, 0x88}, /* RCUT=4, SR=8 (VN03). */
        {AT86RF215_REG_RF09_PAC, (uint8_t)(0x60 | config->tx_power_level)},
        {AT86RF215_REG_RF09_IRQM, 0x12}, /* TRXRDY, TRXERR. */
        {AT86RF215_REG_RF24_IRQM, 0},
        {AT86RF215_REG_BBC0_IRQM, 0x12}, /* TXFE, RXFE. */
        {AT86RF215_REG_BBC1_IRQM, 0},
        {AT86RF215_REG_BBC0_PC, 0x55} /* FCS filter, auto CRC32, BBEN, FSK. */
    };
    for (size_t i = 0; result == 0 && i < ARRAY_SIZE(settings); i++) {
        result = write_byte(p, settings[i].address, settings[i].value);
    }
    /* C0..SFD1H: BT=.5, h=1, 2-FSK, 50k, no FEC, thresholds, IEEE,
     * 16-octet preamble, standard SFD patterns (LSB first). Receive accepts SFD0.
     */
    const uint8_t fsk[] = {0x16, 0, 0x40, 0x85, 0, 16, 0x09, 0x72, 0xF6, 0x72};
    if (result == 0) {
        result = write_block(p, AT86RF215_REG_BBC0_FSKC0,
                                       fsk, sizeof(fsk));
    }
    if (result == 0) {
        result = write_byte(p, AT86RF215_REG_BBC0_FSKPHRTX, 0); /* SFD0, no DW. */
    }
    uint8_t events[4];
    if (result == 0) { result = read_events(p, events); }
    if (result == 0) { result = prepare(p, k_uptime_get() + CONFIGURE_MS); }
    result = finish(p, result);
    p->configured = (result == 0);
    return result;
}

int at86rf215_packet_send(struct at86rf215_packet *p,
                          const void *data, size_t length, int64_t deadline)
{
    if (!p->configured) { return -EACCES; }
    if (data == NULL || length == 0 || length > OCS_RADIO_MAX_PAYLOAD) {
        return -EINVAL;
    }
    uint8_t events[4];
    int result = read_events(p, events); /* Discard stale completion flags. */
    uint16_t total = (uint16_t)(length + CRC_BYTES);
    uint8_t frame_length[] = {(uint8_t)total, (uint8_t)(total >> 8)};
    /* TXFLL resets the chip's buffer-write tracking. Set length FIRST. */
    if (result == 0) {
        result = write_block(p, AT86RF215_REG_BBC0_TXFLL,
                                       frame_length, sizeof(frame_length));
    }
    if (result == 0) {
        result = write_block(p, OCS_RADIO_TX_BUFFER, data, length);
    }
    /* Section 6.13.4 requires FCS space for underrun tracking even with
     * automatic CRC. Hardware replaces these placeholders with the real FCS.
     * A separate transfer keeps the 32-byte payload within the SPI limit.
     */
    const uint8_t crc_padding[CRC_BYTES] = {0};
    if (result == 0) {
        result = write_block(p, (uint16_t)(OCS_RADIO_TX_BUFFER + length),
                             crc_padding, sizeof(crc_padding));
    }
    if (result == 0) { result = prepare(p, deadline); }
    if (result == 0 && k_uptime_get() >= deadline) { result = -ETIMEDOUT; }
    if (result == 0) {
        result = write_byte(p, AT86RF215_REG_RF09_CMD, AT86RF215_CMD_TX);
    }
    if (result == 0) { result = wait_event(p, 2, AT86RF215_BBC_IRQ_TXFE, deadline); }
    if (result == 0) {
        uint8_t status;
        result = read_byte(p, AT86RF215_REG_BBC0_PS, &status);
        if (result == 0 && (status & AT86RF215_PS_TXUR) != 0) { result = -EIO; }
    }
    return finish(p, result);
}

int at86rf215_packet_receive(struct at86rf215_packet *p,
                             void *data, size_t capacity, size_t *length,
                             int64_t deadline)
{
    if (!p->configured) { return -EACCES; }
    if (data == NULL || length == NULL || capacity == 0) { return -EINVAL; }
    uint8_t payload[OCS_RADIO_MAX_PAYLOAD];
    size_t payload_length = 0;
    uint8_t events[4];
    int result = read_events(p, events);
    if (result == 0 && k_uptime_get() >= deadline) { result = -ETIMEDOUT; }
    if (result == 0) {
        result = write_byte(p, AT86RF215_REG_RF09_CMD, AT86RF215_CMD_RX);
    }
    if (result == 0) { result = wait_event(p, 2, AT86RF215_BBC_IRQ_RXFE, deadline); }
    if (result == 0) {
        uint8_t control;
        result = read_byte(p, AT86RF215_REG_BBC0_PC, &control);
        if (result == 0 && (control & AT86RF215_PC_FCSOK) == 0) { result = -EBADMSG; }
    }
    if (result == 0) {
        uint8_t header;
        result = read_byte(p, AT86RF215_REG_BBC0_FSKPHRRX, &header);
        /* Reject CRC16, mode switch and SFD1; never subtract the wrong FCS size. */
        if (result == 0 && (header & 0xC8U) != 0) { result = -EBADMSG; }
    }
    if (result == 0) {
        uint8_t frame_length[2];
        result = read_block(p, AT86RF215_REG_BBC0_RXFLL,
                                      frame_length, sizeof(frame_length));
        if (result == 0) {
            uint16_t total = ((uint16_t)frame_length[1] << 8) | frame_length[0];
            if (total <= CRC_BYTES || total > OCS_RADIO_MAX_PAYLOAD + CRC_BYTES ||
                total - CRC_BYTES > capacity) {
                result = -EMSGSIZE;
            } else {
                payload_length = total - CRC_BYTES;
            }
        }
    }
    if (result == 0) {
        result = read_block(p, OCS_RADIO_RX_BUFFER,
                                      payload, payload_length);
    }
    result = finish(p, result);
    if (result == 0) {
        memcpy(data, payload, payload_length);
        *length = payload_length;
    }
    return result;
}
