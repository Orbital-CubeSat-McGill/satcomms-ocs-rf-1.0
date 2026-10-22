#pragma once

#include <stddef.h>
#include <stdint.h>

#define AT86RF215_IO_MAX 32U

struct at86rf215_bus {
    void *ctx;

    int (*transfer)(
        void *ctx,
        const uint8_t *tx,
        uint8_t *rx,
        size_t length
    );
};

struct at86rf215 {
    struct at86rf215_bus bus;
};

int at86rf215_read_block(
    struct at86rf215 *radio,
    uint16_t address,
    uint8_t *data,
    size_t length
);

int at86rf215_write_block(
    struct at86rf215 *radio,
    uint16_t address,
    const uint8_t *data,
    size_t length
);

int at86rf215_read_reg(
    struct at86rf215 *radio,
    uint16_t address,
    uint8_t *value
);

int at86rf215_write_reg(
    struct at86rf215 *radio,
    uint16_t address,
    uint8_t value
);