#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define AT86RF215_NODE DT_NODELABEL(at86rf215)
static const struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(AT86RF215_NODE,
                                          SPI_WORD_SET(8) | SPI_TRANSFER_MSB);
static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_GET(AT86RF215_NODE, reset_gpios);

void read_at86rf215_part_reg(void)
{
    /* 1. Hardware Reset Sequence */
    if (gpio_is_ready_dt(&reset_pin)) {
        /* Configure as output and set to active (pulls pin LOW based on GPIO_ACTIVE_LOW) */
        gpio_pin_configure_dt(&reset_pin, GPIO_OUTPUT_ACTIVE);
        k_sleep(K_MSEC(10));

        /* Set to inactive (pulls pin HIGH, releasing the transceiver from reset) */
        gpio_pin_set_dt(&reset_pin, 0);
        k_sleep(K_MSEC(10)); // Give the chip time to boot
    } else {
        printk("Error: Reset GPIO not ready.\n");
        return;
    }

    /* 2. SPI Transaction */
    if (!spi_is_ready_dt(&spi_dev)) {
        printk("Error: SPI device not ready.\n");
        return;
    }

    uint8_t tx_data[3] = { 0x00, 0x0D, 0x00 };
    uint8_t rx_data[3] = { 0 };

    const struct spi_buf tx_spi_buf = { .buf = tx_data, .len = sizeof(tx_data) };
    const struct spi_buf_set tx = { .buffers = &tx_spi_buf, .count = 1 };

    const struct spi_buf rx_spi_buf = { .buf = rx_data, .len = sizeof(rx_data) };
    const struct spi_buf_set rx = { .buffers = &rx_spi_buf, .count = 1 };

    int err = spi_transceive_dt(&spi_dev, &tx, &rx);

    if (err < 0) {
        printk("SPI transaction failed with error: %d\n", err);
        return;
    }

    printk("AT86RF215 Part Register (0x000D): 0x%02X\n", rx_data[2]);
}

int main(void)
{
    /* Small delay to ensure terminal is ready if connecting right after flash */
    k_sleep(K_MSEC(1000));

    printk("Starting AT86RF215 test...\n");

    /* Call the function we just wrote */
    read_at86rf215_part_reg();

    /* Keep the main thread alive */
    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}