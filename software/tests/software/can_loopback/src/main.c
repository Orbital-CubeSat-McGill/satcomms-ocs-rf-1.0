#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <ocs/mcp251863_loopback.h>

/* Chip/physical GPIO model only. The actual transport encoder, mode sequencing,
 * RAM/FIFO operations, RX validation and cleanup run as production code.
 */
static struct {
    uint8_t mem[4096];
    unsigned int writes, resets, tx_requests, safe_writes, ram_writes, mode_attempts;
    bool corrupt_crc, missing_rx, hold_mode, standby_low, drop_standby;
    bool irq_never_low, irq_stuck_low, fail_ram, fail_cleanup, entered;
    bool reject_cleanup_disable;
    uint32_t bad_rx_ua;
    int bad_frame;
} chip;

static uint16_t reference_crc(const uint8_t *data, size_t count)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < count; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            bool feedback = ((crc >> 15) ^ (data[i] >> bit)) & 1;
            crc <<= 1;
            if (feedback) { crc ^= 0x8005; }
        }
    }
    return crc;
}

static void reset_memory(void)
{
    memset(chip.mem, 0, sizeof(chip.mem));
    sys_put_le32(0x04980760, chip.mem);
    sys_put_le32(0x460, chip.mem + 0xe00);
    sys_put_le32(0x03, chip.mem + 0xe04); /* GPIO0/1 inputs. */
    sys_put_le32(1, chip.mem + 0x60); /* TX FIFO space. */
}

static int transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t size)
{
    ARG_UNUSED(ctx);
    uint16_t address = ((tx[0] & 15U) << 8) | tx[1];
    uint8_t command = tx[0] >> 4;
    memset(rx, 0, size);
    if (command == 0) {
        zassert_equal(size, 2); zassert_equal(address, 0);
        zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
        chip.resets++; chip.writes++; reset_memory();
        return 0;
    }
    if (command == 0xb) {
        size_t length = address >= 0x400 && address < 0xc00 ? tx[2] * 4 : tx[2];
        zassert_true(length > 0 && length <= 32);
        zassert_equal(size, length + 5);
        if (address >= 0x400 && address < 0xc00) {
            zassert_equal(address % 4, 0); zassert_true(address + length <= 0xc00);
        }
        zassert_true(address + length <= sizeof(chip.mem));
        memcpy(rx + 3, chip.mem + address, length);
        uint8_t input[35]; memcpy(input, tx, 3); memcpy(input + 3, rx + 3, length);
        uint16_t crc = reference_crc(input, length + 3);
        sys_put_be16(crc, rx + 3 + length);
        if (chip.corrupt_crc) { rx[3] ^= 1; }
        return 0;
    }
    zassert_true(command == 0xc || command == 0xa, "Unexpected unprotected SPI write");
    if (command == 0xa) {
        size_t count = tx[2] * 4;
        zassert_equal(size, count + 5);
        zassert_true(address >= 0x400 && address + count <= 0xc00);
        zassert_equal(address % 4, 0);
        zassert_true(count > 0 && count <= 32);
        zassert_equal(reference_crc(tx, size), 0);
        if (chip.fail_ram) { return -EIO; }
        memcpy(chip.mem + address, tx + 3, count);
        chip.ram_writes++; chip.writes++;
        return 0;
    }
    zassert_equal(size, 5);
    zassert_equal(reference_crc(tx, size), 0);
    zassert_true(address < 0x400, "No writes to oscillator, IOCON or transceiver controls");
    /* A rejected WRITE_SAFE can leave the register unchanged while the SPI
     * controller still reports a successful electrical transfer.
     */
    if (chip.reject_cleanup_disable && chip.entered && address == 0x1e && tx[2] == 0) {
        return 0;
    }
    if (address == 3) {
        unsigned int mode = tx[2] & 7U;
        zassert_true(mode == 2 || mode == 4, "Normal/external loopback mode requested");
        if (mode == 4 && chip.entered && chip.fail_cleanup) { return -EIO; }
        if (mode == 2) { chip.mode_attempts++; }
        if (mode == 2 && !chip.hold_mode) {
            chip.entered = true;
            uint32_t con = sys_get_le32(chip.mem);
            sys_put_le32((con & ~(7U << 21)) | (2U << 21), chip.mem);
            sys_put_le32(0, chip.mem + 0x64); /* FIFO1 UA: RAM-relative. */
            sys_put_le32(chip.bad_rx_ua ? chip.bad_rx_ua : 16, chip.mem + 0x70);
            chip.mem[0x5d] &= ~4U; chip.mem[0x69] &= ~4U;
        } else if (mode == 4) {
            uint32_t con = sys_get_le32(chip.mem);
            sys_put_le32((con & ~(7U << 21)) | (4U << 21), chip.mem);
        }
    }
    /* OPMOD is read-only even when the surrounding CiCON byte is written. */
    chip.mem[address] = address == 2 ? (chip.mem[2] & 0xe0U) | (tx[2] & 0x1fU) : tx[2];
    chip.writes++; chip.safe_writes++;
    if (address == 0x5d && (tx[2] & 2U)) {
        zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 2);
        zassert_equal(chip.mem[0x5c] & 0x80, 0x80);
        zassert_equal(chip.mem[0x68] & 0x81, 1);
        zassert_equal(chip.mem[0x1d0], 0x82);
        zassert_equal(sys_get_le32(chip.mem + 0x1f0), 0x123);
        zassert_equal(sys_get_le32(chip.mem + 0x1f4), 0x400007ff);
        chip.tx_requests++;
        chip.mem[0x5d] &= ~2U;
        if (!chip.missing_rx) {
            memcpy(chip.mem + 0x410, chip.mem + 0x400, 16);
            if (chip.bad_frame == 1) { chip.mem[0x410] ^= 1; }
            if (chip.bad_frame == 2) { chip.mem[0x414] = 7; }
            if (chip.bad_frame == 3) { chip.mem[0x418] ^= 1; }
            if (chip.bad_frame == 4) { chip.mem[0x412] = 0xaa; chip.mem[0x413] = 0x80; }
            chip.mem[0x6c] |= 1; chip.mem[0x1c] |= 2;
        }
    }
    if (address == 0x69 && (tx[2] & 1U)) {
        chip.mem[0x6c] &= ~1U; chip.mem[0x1c] &= ~2U;
    }
    return 0;
}

static int standby(void *ctx)
{
    ARG_UNUSED(ctx);
    return !(chip.standby_low || (chip.drop_standby && chip.entered));
}
static int interrupt(void *ctx)
{
    ARG_UNUSED(ctx);
    if (chip.irq_never_low) { return 0; }
    if (chip.irq_stuck_low) { return 1; }
    return (chip.mem[0x1c] & 2) != 0 && (chip.mem[0x1e] & 2) != 0;
}
static const struct mcp251863_loopback_io io = {
    .bus = {.transfer = transfer}, .standby_high = standby, .interrupt_active = interrupt
};
static void before_each(void *fixture)
{
    ARG_UNUSED(fixture); memset(&chip, 0, sizeof(chip)); reset_memory();
}

ZTEST(can_loopback, test_frame_roundtrip_irq_and_cleanup)
{
    struct mcp251863_loopback_result r;
    zassert_ok(mcp251863_loopback_run(&io, &r, 1000));
    zassert_equal(r.id, 0x123); zassert_equal(r.length, 8);
    zassert_mem_equal(r.data, "CAN TEST", 8);
    zassert_true(r.irq_asserted); zassert_true(r.irq_cleared);
    zassert_equal(chip.resets, 1); zassert_equal(chip.tx_requests, 1);
    zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
    zassert_equal(sys_get_le32(chip.mem + 0x1c) >> 16, 0);
    zassert_equal(r.cleanup_error, 0);
}
ZTEST(can_loopback, test_invalid_inputs_do_not_touch_hardware)
{
    struct mcp251863_loopback_result r;
    struct mcp251863_loopback_io bad = io; bad.standby_high = NULL;
    zassert_equal(mcp251863_loopback_run(NULL, &r, 10), -EINVAL);
    zassert_equal(mcp251863_loopback_run(&bad, &r, 10), -EINVAL);
    zassert_equal(mcp251863_loopback_run(&io, NULL, 10), -EINVAL);
    zassert_equal(mcp251863_loopback_run(&io, &r, 0), -EINVAL);
    zassert_equal(chip.writes, 0);
}
ZTEST(can_loopback, test_low_standby_refuses_all_writes)
{
    struct mcp251863_loopback_result r; chip.standby_low = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 20), -EBUSY);
    zassert_equal(chip.writes, 0);
}
ZTEST(can_loopback, test_bad_spi_crc_refuses_all_writes)
{
    struct mcp251863_loopback_result r; chip.corrupt_crc = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 20), -EBADMSG);
    zassert_equal(chip.writes, 0);
}
ZTEST(can_loopback, test_active_controller_is_not_taken_over)
{
    struct mcp251863_loopback_result r; sys_put_le32(0, chip.mem);
    zassert_equal(mcp251863_loopback_run(&io, &r, 20), -EBUSY);
    zassert_equal(chip.writes, 0);
}
ZTEST(can_loopback, test_clock_not_ready_refuses_all_writes)
{
    struct mcp251863_loopback_result r; sys_put_le32(0x60, chip.mem + 0xe00);
    zassert_equal(mcp251863_loopback_run(&io, &r, 20), -EAGAIN);
    zassert_equal(chip.writes, 0);
}
ZTEST(can_loopback, test_mode_timeout_never_requests_transmission)
{
    struct mcp251863_loopback_result r; chip.hold_mode = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 250), -ETIMEDOUT);
    zassert_equal(chip.mode_attempts, 1);
    zassert_equal(chip.tx_requests, 0);
    zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
}
ZTEST(can_loopback, test_missing_receive_times_out_and_stops)
{
    struct mcp251863_loopback_result r; chip.missing_rx = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 250), -ETIMEDOUT);
    zassert_equal(chip.tx_requests, 1);
    zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
}
ZTEST(can_loopback, test_wrong_identifier_length_or_payload_is_rejected)
{
    for (int fault = 1; fault <= 3; fault++) {
        before_each(NULL); chip.bad_frame = fault;
        struct mcp251863_loopback_result r;
        zassert_equal(mcp251863_loopback_run(&io, &r, 1000), -EBADMSG);
        zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
    }
}
ZTEST(can_loopback, test_standard_identifier_ignores_unused_eid_storage)
{
    struct mcp251863_loopback_result r; chip.bad_frame = 4;
    zassert_ok(mcp251863_loopback_run(&io, &r, 1000));
    zassert_equal(r.id, 0x123);
    zassert_mem_equal(r.data, "CAN TEST", 8);
}
ZTEST(can_loopback, test_missing_interrupt_is_not_a_pass)
{
    struct mcp251863_loopback_result r; chip.irq_never_low = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 1000), -EIO);
    zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
}
ZTEST(can_loopback, test_stuck_interrupt_is_not_a_pass)
{
    struct mcp251863_loopback_result r; chip.irq_stuck_low = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 1000), -EIO);
    zassert_equal(chip.tx_requests, 0);
}
ZTEST(can_loopback, test_bad_fifo_pointer_cannot_escape_message_ram)
{
    struct mcp251863_loopback_result r; chip.bad_rx_ua = 0x7ff;
    zassert_equal(mcp251863_loopback_run(&io, &r, 1000), -ERANGE);
    zassert_equal(chip.tx_requests, 0);
}
ZTEST(can_loopback, test_ram_failure_stops_before_transmission)
{
    struct mcp251863_loopback_result r; chip.fail_ram = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 1000), -EIO);
    zassert_equal(chip.tx_requests, 0);
    zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
}
ZTEST(can_loopback, test_standby_drop_after_mode_change_prevents_transmission)
{
    struct mcp251863_loopback_result r; chip.drop_standby = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 1000), -EBUSY);
    zassert_equal(chip.tx_requests, 0);
    zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
}
ZTEST(can_loopback, test_cleanup_failure_is_reported)
{
    struct mcp251863_loopback_result r; chip.fail_cleanup = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 1000), -EIO);
    zassert_equal(r.cleanup_error, -EIO);
}
ZTEST(can_loopback, test_rejected_cleanup_disable_is_not_a_pass)
{
    struct mcp251863_loopback_result r; chip.reject_cleanup_disable = true;
    zassert_equal(mcp251863_loopback_run(&io, &r, 1000), -EIO);
    zassert_equal(r.cleanup_error, -EIO);
    zassert_equal((sys_get_le32(chip.mem) >> 21) & 7, 4);
    zassert_equal(sys_get_le32(chip.mem + 0x1c) >> 16, 2);
}
ZTEST_SUITE(can_loopback, NULL, NULL, before_each, NULL, NULL);
