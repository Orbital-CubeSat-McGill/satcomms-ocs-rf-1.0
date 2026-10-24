#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <ocs/mcp251863_probe.h>

/* Hand-checked READ_CRC fixtures: header + four little-endian data bytes.
 * CRC-16: polynomial 0x8005, seed 0xffff, MSB first, no final XOR.
 * Only the physical transport is replaced; the real probe parses/checks it.
 */
static const uint8_t commands[][3] = {
    {0xb0, 0x00, 4}, {0xbe, 0x00, 4}, {0xbe, 0x14, 4}
};
static const uint8_t replies[][6] = {
    {0x60, 0x07, 0x98, 0x04, 0xb2, 0x8c},
    {0x70, 0x14, 0x00, 0x00, 0xad, 0x04},
    {0x00, 0x00, 0x00, 0x00, 0x8c, 0x17}
};
static struct {
    unsigned int calls;
    int fail_call;
    int corrupt_call;
    int stuck;
} model;

static int transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t length)
{
    zassert_equal_ptr(ctx, &model);
    unsigned int index = model.calls++;
    zassert_true(index < ARRAY_SIZE(commands), "Unexpected extra SPI command");
    zassert_equal(length, 9);
    /* Rejects any reset/write opcode, wrong address or truncated CRC read. */
    zassert_mem_equal(tx, commands[index], 3);
    const uint8_t zeros[6] = {0};
    zassert_mem_equal(tx + 3, zeros, sizeof(zeros));
    memset(rx, 0, length);
    if ((int)index == model.fail_call) {
        memset(rx, 0x99, length);
        return -EIO;
    }
    if (model.stuck >= 0) { memset(rx, model.stuck, length); return 0; }
    memcpy(rx + 3, replies[index], 6);
    if ((int)index == model.corrupt_call) { rx[3] ^= 1; }
    return 0;
}

static const struct mcp251863_probe_bus bus = { .ctx = &model, .transfer = transfer };
static void before_each(void *fixture)
{
    ARG_UNUSED(fixture);
    model.calls = 0;
    model.fail_call = -1;
    model.corrupt_call = -1;
    model.stuck = -1;
}

ZTEST(can_probe, test_reads_only_crc_protected_status_and_identity)
{
    struct mcp251863_probe_snapshot s;
    zassert_ok(mcp251863_probe_read(&bus, &s));
    zassert_equal(s.con, 0x04980760);
    zassert_equal(s.osc, 0x00001470);
    zassert_equal(s.devid, 0, "Zero is a legitimate DEVID value");
    zassert_equal(model.calls, 3);
    zassert_true(mcp251863_probe_clock_ready(&s));
}

ZTEST(can_probe, test_invalid_arguments_do_not_touch_spi)
{
    struct mcp251863_probe_snapshot s;
    const struct mcp251863_probe_bus missing = {0};
    zassert_equal(mcp251863_probe_read(NULL, &s), -EINVAL);
    zassert_equal(mcp251863_probe_read(&missing, &s), -EINVAL);
    zassert_equal(mcp251863_probe_read(&bus, NULL), -EINVAL);
    zassert_equal(model.calls, 0);
}

ZTEST(can_probe, test_crc_error_at_each_register_preserves_output)
{
    for (int i = 0; i < 3; i++) {
        struct mcp251863_probe_snapshot s = {1, 2, 3}, expected = s;
        model.calls = 0; model.corrupt_call = i;
        zassert_equal(mcp251863_probe_read(&bus, &s), -EBADMSG);
        zassert_mem_equal(&s, &expected, sizeof(s));
        zassert_equal(model.calls, i + 1);
    }
}

ZTEST(can_probe, test_transport_error_at_each_register_preserves_output)
{
    for (int i = 0; i < 3; i++) {
        struct mcp251863_probe_snapshot s = {1, 2, 3}, expected = s;
        model.calls = 0; model.fail_call = i;
        zassert_equal(mcp251863_probe_read(&bus, &s), -EIO);
        zassert_mem_equal(&s, &expected, sizeof(s));
        zassert_equal(model.calls, i + 1);
    }
}

ZTEST(can_probe, test_stuck_zero_or_one_miso_is_not_a_presence_pass)
{
    for (int value = 0; value <= 255; value += 255) {
        struct mcp251863_probe_snapshot s = {1, 2, 3}, expected = s;
        model.calls = 0; model.stuck = value;
        zassert_equal(mcp251863_probe_read(&bus, &s), -EBADMSG);
        zassert_mem_equal(&s, &expected, sizeof(s));
        zassert_equal(model.calls, 1);
    }
}

ZTEST(can_probe, test_oscillator_and_pll_readiness_rules)
{
    struct mcp251863_probe_snapshot s = {0};
    zassert_false(mcp251863_probe_clock_ready(NULL));
    zassert_false(mcp251863_probe_clock_ready(&s));
    s.osc = 0x400;
    zassert_true(mcp251863_probe_clock_ready(&s));
    s.osc = 0x404; /* OSCDIS overrides a stale ready bit. */
    zassert_false(mcp251863_probe_clock_ready(&s));
    s.osc = 0x401; /* PLL selected but not locked. */
    zassert_false(mcp251863_probe_clock_ready(&s));
    s.osc = 0x501;
    zassert_true(mcp251863_probe_clock_ready(&s));
}

ZTEST_SUITE(can_probe, NULL, NULL, before_each, NULL, NULL);
