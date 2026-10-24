#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <ocs/at86rf215.h>

#include "at86rf215_regs.h"

struct fake_spi {
    uint8_t last_tx[AT86RF215_IO_MAX + 2];
    uint8_t reply[AT86RF215_IO_MAX];
    size_t length;
    unsigned int calls;
    int result;
};

static struct fake_spi fake;

static int fake_transfer(
    void *ctx,
    const uint8_t *tx,
    uint8_t *rx,
    size_t length
)
{
    struct fake_spi *state = ctx;

    zassert_true(
        length >= 3 && length <= sizeof(state->last_tx),
        "Bad length"
    );

    state->calls++;
    state->length = length;

    memcpy(state->last_tx, tx, length);

    memset(rx, 0, length);
    memcpy(rx + 2, state->reply, length - 2);

    return state->result;
}

static struct at86rf215 radio = {
    .bus = {
        .ctx = &fake,
        .transfer = fake_transfer
    }
};

static void reset_fake(void)
{
    memset(&fake, 0, sizeof(fake));
}

ZTEST(radio_core, test_read_part_number)
{
    reset_fake();

    fake.reply[0] = 0x34;

    uint8_t value = 0;

    int result = at86rf215_read_reg(
        &radio, AT86RF215_REG_RF_PN, &value
    );

    const uint8_t expected[] = {0x00, 0x0D, 0x00};

    zassert_equal(result, 0, "Read failed");
    zassert_equal(value, 0x34, "Wrong value");
    zassert_equal(fake.length, 3, "Wrong length");
    zassert_equal(fake.calls, 1, "Wrong call count");

    zassert_mem_equal(
        fake.last_tx, expected, sizeof(expected), "Wrong command"
    );
}

ZTEST(radio_core, test_write_register)
{
    reset_fake();

    const uint8_t expected[] = {0x81, 0x00, 0x03};

    int result = at86rf215_write_reg(
        &radio, AT86RF215_REG_RF09_IRQM, 0x03
    );

    zassert_equal(result, 0, "Write failed");

    zassert_mem_equal(
        fake.last_tx, expected, sizeof(expected), "Wrong write"
    );
}

ZTEST(radio_core, test_block_read)
{
    reset_fake();

    fake.reply[0] = 0x34;
    fake.reply[1] = 0x03;

    uint8_t data[2] = {0};

    int result = at86rf215_read_block(
        &radio, 0x000D, data, sizeof(data)
    );

    const uint8_t expected[] = {0x34, 0x03};

    zassert_equal(result, 0, "Read failed");
    zassert_equal(fake.length, 4, "Wrong length");

    zassert_mem_equal(
        data, expected, sizeof(expected), "Wrong data"
    );
}

ZTEST(radio_core, test_block_write)
{
    reset_fake();

    const uint8_t data[] = {0x11, 0x22, 0x33};

    const uint8_t expected[] = {
        0xA8, 0x00, 0x11, 0x22, 0x33
    };

    int result = at86rf215_write_block(
        &radio, 0x2800, data, sizeof(data)
    );

    zassert_equal(result, 0, "Write failed");
    zassert_equal(fake.calls, 1, "Block was split");
    zassert_equal(fake.length, sizeof(expected), "Wrong length");

    zassert_mem_equal(
        fake.last_tx, expected, sizeof(expected), "Wrong bytes"
    );
}

ZTEST(radio_core, test_maximum_length)
{
    reset_fake();

    uint8_t data[AT86RF215_IO_MAX] = {0};

    zassert_equal(
        at86rf215_read_block(&radio, 0x2000, data, sizeof(data)),
        0,
        "Maximum valid length rejected"
    );

    zassert_equal(
        fake.length,
        AT86RF215_IO_MAX + 2,
        "Wrong length"
    );
}

ZTEST(radio_core, test_invalid_ranges)
{
    reset_fake();

    uint8_t data[AT86RF215_IO_MAX + 1] = {0};

    zassert_equal(
        at86rf215_read_reg(&radio, 0x4000, data),
        -EINVAL,
        "Bad address"
    );

    zassert_equal(
        at86rf215_read_block(&radio, 0, data, 0),
        -EINVAL,
        "Zero length"
    );

    zassert_equal(
        at86rf215_read_block(&radio, 0, data, sizeof(data)),
        -EINVAL,
        "Oversized read"
    );

    zassert_equal(
        at86rf215_read_block(&radio, 0x3FFF, data, 2),
        -EINVAL,
        "Address overflow"
    );

    zassert_equal(
        at86rf215_write_block(&radio, 0x3FFF, data, 2),
        -EINVAL,
        "Write address overflow"
    );

    zassert_equal(fake.calls, 0, "Invalid request reached SPI");
}

ZTEST(radio_core, test_missing_pointers)
{
    reset_fake();

    uint8_t data = 0;
    struct at86rf215 missing_bus = {0};

    zassert_equal(
        at86rf215_read_reg(NULL, 0, &data),
        -EINVAL,
        "NULL radio"
    );

    zassert_equal(
        at86rf215_read_reg(&radio, 0, NULL),
        -EINVAL,
        "NULL output"
    );

    zassert_equal(
        at86rf215_read_reg(&missing_bus, 0, &data),
        -EINVAL,
        "No bus"
    );

    zassert_equal(
        at86rf215_write_block(&radio, 0, NULL, 1),
        -EINVAL,
        "NULL input"
    );

    zassert_equal(fake.calls, 0, "Invalid request reached SPI");
}

ZTEST(radio_core, test_read_error_preserves_output)
{
    reset_fake();

    fake.result = -EIO;
    fake.reply[0] = 0x99;

    uint8_t value = 0xA5;

    zassert_equal(
        at86rf215_read_reg(&radio, 0x000D, &value),
        -EIO,
        "Error lost"
    );

    zassert_equal(value, 0xA5, "Failed read changed output");
}

ZTEST(radio_core, test_write_error)
{
    reset_fake();

    fake.result = -EIO;

    zassert_equal(
        at86rf215_write_reg(&radio, 0x0100, 1),
        -EIO,
        "Error lost"
    );
}

ZTEST(radio_core, test_higher_address)
{
    reset_fake();

    uint8_t value = 0;

    zassert_equal(
        at86rf215_read_reg(&radio, AT86RF215_REG_BBC0_PC, &value),
        0,
        "Read failed"
    );

    zassert_equal(fake.last_tx[0], 0x03, "Upper address lost");
    zassert_equal(fake.last_tx[1], 0x01, "Lower address wrong");
}

ZTEST_SUITE(radio_core, NULL, NULL, NULL, NULL, NULL);