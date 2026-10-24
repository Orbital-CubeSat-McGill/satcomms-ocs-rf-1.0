#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <ocs/at86rf215_zephyr.h>

#define RADIO_NODE DT_NODELABEL(at86rf215)

struct radio_hardware {
    struct spi_dt_spec spi;
    struct gpio_dt_spec reset;
    struct gpio_dt_spec irq;
    struct gpio_callback callback;
};

static struct radio_hardware hardware = {
    .spi = SPI_DT_SPEC_GET(
        RADIO_NODE,
        SPI_WORD_SET(8) | SPI_TRANSFER_MSB
    ),
    .reset = GPIO_DT_SPEC_GET(RADIO_NODE, reset_gpios),
    .irq = GPIO_DT_SPEC_GET(RADIO_NODE, irq_gpios)
};

K_SEM_DEFINE(radio_irq_sem, 0, 1);
static bool irq_callback_registered;

static int zephyr_transfer(
    void *ctx,
    const uint8_t *tx,
    uint8_t *rx,
    size_t length
)
{
    struct radio_hardware *hw = ctx;

    struct spi_buf tx_buffer = {
        .buf = (void *)tx,
        .len = length
    };

    struct spi_buf rx_buffer = {
        .buf = rx,
        .len = length
    };

    struct spi_buf_set tx_buffers = {
        .buffers = &tx_buffer,
        .count = 1
    };

    struct spi_buf_set rx_buffers = {
        .buffers = &rx_buffer,
        .count = 1
    };

    return spi_transceive_dt(
        &hw->spi,
        &tx_buffers,
        &rx_buffers
    );
}

static void radio_irq_callback(
    const struct device *port,
    struct gpio_callback *callback,
    gpio_port_pins_t pins
)
{
    ARG_UNUSED(port);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);

    k_sem_give(&radio_irq_sem);
}

int at86rf215_zephyr_reset(void)
{
    k_sem_reset(&radio_irq_sem);

    int result = gpio_pin_set_dt(&hardware.reset, 1);

    if (result != 0) {
        return result;
    }

    k_msleep(1);

    result = gpio_pin_set_dt(&hardware.reset, 0);

    if (result != 0) {
        return result;
    }

    k_msleep(10);

    return 0;
}

int at86rf215_zephyr_init(struct at86rf215 *radio)
{
    if (radio == NULL) {
        return -EINVAL;
    }

    if (!spi_is_ready_dt(&hardware.spi) ||
        !gpio_is_ready_dt(&hardware.reset)) {
        return -ENODEV;
    }

    int result = gpio_pin_configure_dt(
        &hardware.reset,
        GPIO_OUTPUT_ACTIVE
    );

    if (result != 0) {
        return result;
    }

    radio->bus.ctx = &hardware;
    radio->bus.transfer = zephyr_transfer;

    return at86rf215_zephyr_reset();
}

int at86rf215_zephyr_irq_init(void)
{
    if (!gpio_is_ready_dt(&hardware.irq)) {
        return -ENODEV;
    }

    int result = gpio_pin_configure_dt(
        &hardware.irq,
        GPIO_INPUT
    );

    if (result != 0) {
        return result;
    }

    if (!irq_callback_registered) {
        gpio_init_callback(
            &hardware.callback,
            radio_irq_callback,
            BIT(hardware.irq.pin)
        );
        result = gpio_add_callback(hardware.irq.port, &hardware.callback);
        if (result != 0) {
            return result;
        }
        /* A retry must not insert the same callback node a second time. */
        irq_callback_registered = true;
    }

    return gpio_pin_interrupt_configure_dt(
        &hardware.irq,
        GPIO_INT_EDGE_TO_ACTIVE
    );
}

int at86rf215_zephyr_wait_irq(int32_t timeout_ms)
{
    if (timeout_ms < 0) {
        return -EINVAL;
    }

    int result = k_sem_take(
        &radio_irq_sem,
        K_MSEC(timeout_ms)
    );

    return result == -EAGAIN ? -ETIMEDOUT : result;
}

int at86rf215_zephyr_irq_level(void)
{
    return gpio_pin_get_dt(&hardware.irq);
}
