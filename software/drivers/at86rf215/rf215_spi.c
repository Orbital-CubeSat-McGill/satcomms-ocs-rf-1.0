#include "errno.h"
#include "rf215.h"
#include "rf215_internal.h"

#define RF215_MODE_READ     0xBF
#define RF215_MODE_WRITE    0xBF
#define RF215_ADDR_MAX      0x3FFE

int rf215_blk_write(
    const struct device *dev,
    uint16_t address,
    const uint8_t *buf,
    size_t length
) {
    const struct rf215_config *cfg = dev->config;
    struct rf215_data *data = dev->data;

    if (data==NULL || length == 0 || address > RF215_ADDR_MAX || (size_t)address + length > RF215_ADDR_MAX + 1) {
        return -EINVAL;
    }

    uint8_t hdr[2] = {RF215_MODE_WRITE & (address >> 8), address & 0xFF};

    const struct spi_buf tx_bufs[] = {
        { .buf = hdr, .len = sizeof(hdr)},
        { .buf = (void *)buf, .len = length}
    };

    const struct spi_buf_set tx = {
        .buffers = tx_bufs,
        .count = ARRAY_SIZE(tx_bufs)
    };

    k_mutex_lock(&data->bus_lock, K_FOREVER);
    int return_code = spi_write_dt(&cfg->rf215_spi, &tx);
    k_mutex_unlock(&data->bus_lock);

    return return_code;
}

int rf215_blk_read(
    const struct device *dev,
    uint16_t address,
    uint8_t *buf,
    size_t length
) {
    const struct rf215_config *cfg = dev->config;
    struct rf215_data *data = dev->data;

    if (data==NULL || length == 0 || address > RF215_ADDR_MAX || (size_t)address + length > RF215_ADDR_MAX + 1) {
        return -EINVAL;
    }

    uint8_t hdr[2] = {RF215_MODE_READ | (address >> 8), address & 0xFF};

    const struct spi_buf tx_bufs[] = {
        { .buf = hdr, .len = sizeof(hdr)},
        {.buf = NULL, .len = length}
    };

    const struct spi_buf rx_bufs[] = {
        {.buf = NULL, .len = 2},
        {.buf = (void *)buf, .len = length}
    };

    const struct spi_buf_set rx = {
        .buffers = rx_bufs,
        .count = ARRAY_SIZE(rx_bufs)
    };

    const struct spi_buf_set tx = {
        .buffers = tx_bufs,
        .count = ARRAY_SIZE(tx_bufs)
    };

    k_mutex_lock(&data->bus_lock, K_FOREVER);
    int return_code = spi_transceive_dt(&cfg->rf215_spi, &tx, &rx);
    k_mutex_unlock(&data->bus_lock);

    return return_code;
}



