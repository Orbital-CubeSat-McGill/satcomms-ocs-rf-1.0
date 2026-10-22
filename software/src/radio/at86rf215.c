#include <errno.h>
#include <string.h>

#include <ocs/at86rf215.h>

static int validate(
    struct at86rf215 *radio,
    uint16_t address,
    size_t length
)
{
    if (radio == NULL || radio->bus.transfer == NULL) {
        return -EINVAL;
    }

    if (address > 0x3FFFU ||
        length == 0 ||
        length > AT86RF215_IO_MAX) {
        return -EINVAL;
    }

    if (length > 0x4000U - address) {
        return -EINVAL;
    }

    return 0;
}

int at86rf215_read_block(
    struct at86rf215 *radio,
    uint16_t address,
    uint8_t *data,
    size_t length
)
{
    int result = validate(radio, address, length);

    if (result != 0 || data == NULL) {
        return -EINVAL;
    }

    uint8_t tx[AT86RF215_IO_MAX + 2] = {0};
    uint8_t rx[AT86RF215_IO_MAX + 2] = {0};

    tx[0] = (uint8_t)((address >> 8) & 0x3FU);
    tx[1] = (uint8_t)(address & 0xFFU);

    result = radio->bus.transfer(
        radio->bus.ctx,
        tx,
        rx,
        length + 2
    );

    if (result != 0) {
        return result;
    }

    memcpy(data, rx + 2, length);

    return 0;
}

int at86rf215_write_block(
    struct at86rf215 *radio,
    uint16_t address,
    const uint8_t *data,
    size_t length
)
{
    int result = validate(radio, address, length);

    if (result != 0 || data == NULL) {
        return -EINVAL;
    }

    uint8_t tx[AT86RF215_IO_MAX + 2] = {0};
    uint8_t rx[AT86RF215_IO_MAX + 2] = {0};

    tx[0] = (uint8_t)(0x80U | ((address >> 8) & 0x3FU));
    tx[1] = (uint8_t)(address & 0xFFU);

    memcpy(tx + 2, data, length);

    return radio->bus.transfer(
        radio->bus.ctx,
        tx,
        rx,
        length + 2
    );
}

int at86rf215_read_reg(
    struct at86rf215 *radio,
    uint16_t address,
    uint8_t *value
)
{
    return at86rf215_read_block(radio, address, value, 1);
}

int at86rf215_write_reg(
    struct at86rf215 *radio,
    uint16_t address,
    uint8_t value
)
{
    return at86rf215_write_block(radio, address, &value, 1);
}