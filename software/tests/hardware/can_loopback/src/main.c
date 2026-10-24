#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <ocs/mcp251863_loopback.h>

#define NODE DT_NODELABEL(can_probe)
static const struct spi_dt_spec spi = SPI_DT_SPEC_GET(
    NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const struct gpio_dt_spec standby = GPIO_DT_SPEC_GET(NODE, standby_gpios);
static const struct gpio_dt_spec interrupt = GPIO_DT_SPEC_GET(NODE, int_gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static int transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t length)
{
    const struct spi_dt_spec *spec = ctx;
    const struct spi_buf tx_buf = {.buf = (void *)tx, .len = length};
    const struct spi_buf rx_buf = {.buf = rx, .len = length};
    const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
    return spi_transceive_dt(spec, &tx_set, &rx_set);
}
static int standby_high(void *ctx)
{
    ARG_UNUSED(ctx);
    return gpio_pin_get_dt(&standby);
}
static int interrupt_active(void *ctx)
{
    ARG_UNUSED(ctx);
    return gpio_pin_get_dt(&interrupt);
}

static int run_checks(void)
{
    const struct mcp251863_loopback_io io = {
        .bus = {.ctx = (void *)&spi, .transfer = transfer},
        .standby_high = standby_high, .interrupt_active = interrupt_active
    };
    struct mcp251863_loopback_result result = {0};
    int ret = mcp251863_loopback_run(&io, &result, 2000);
    if (ret != 0) {
        printk("FAIL stage=%s error=%d cleanup=%d\n",
               result.stage ? result.stage : "arguments", ret, result.cleanup_error);
        if (result.cleanup_error != 0) {
            printk("Cleanup was not confirmed: stop testing and power-cycle before retrying\n");
        }
    } else {
        printk("PASS verified OPMOD=2 (internal loopback) before TXREQ\n");
        printk("RX actual: ID=0x%03x DLC=%u data=", result.id, result.length);
        for (size_t i = 0; i < result.length; i++) { printk("%02x ", result.data[i]); }
        printk("\nRX text: ");
        for (size_t i = 0; i < result.length; i++) { printk("%c", result.data[i]); }
        printk("\nPASS ID, DLC and all eight bytes matched\n");
        printk("PASS RX interrupt asserted then cleared\n");
        printk("PASS returned to Configuration mode with interrupts disabled\n");
        printk("CANH/CANL physical communication remains untested\n");
    }
    printk("CAN INTERNAL LOOPBACK %s (%d)\n", ret == 0 ? "PASS" : "FAIL", ret);
    return result.cleanup_error != 0 ? result.cleanup_error : 0;
}

int main(void)
{
    k_msleep(1500);
    printk("OCS CAN internal-loopback bench: SAMD21 + MCP251863\n");
    printk("SW0 runs a controller reset/configuration and one internal CAN frame\n");
    printk("STBY is sampled HIGH; no normal CAN mode or RF driver\n");
    if (!spi_is_ready_dt(&spi) || !gpio_is_ready_dt(&standby) ||
        !gpio_is_ready_dt(&interrupt) || !gpio_is_ready_dt(&button)) {
        printk("FAIL SPI/GPIO device unavailable\n");
        return -ENODEV;
    }
    int ret = gpio_pin_configure_dt(&standby, GPIO_INPUT);
    if (ret == 0) { ret = gpio_pin_configure_dt(&interrupt, GPIO_INPUT); }
    if (ret == 0) { ret = gpio_pin_configure_dt(&button, GPIO_INPUT); }
    if (ret != 0) { printk("FAIL GPIO inputs: %d\n", ret); return ret; }
    int previous = gpio_pin_get_dt(&button);
    if (previous < 0) { return previous; }
    printk("Ready: release SW0, then press it for the internal-loopback test\n");
    int64_t next_status = k_uptime_get() + 5000;
    for (;;) {
        int pressed = gpio_pin_get_dt(&button);
        if (pressed < 0) { return pressed; }
        if (pressed != 0 && previous == 0) {
            k_msleep(30);
            pressed = gpio_pin_get_dt(&button);
            if (pressed < 0) { return pressed; }
            if (pressed != 0) {
                ret = run_checks();
                if (ret != 0) { return ret; }
                printk("Release SW0, then press again to repeat\n");
            }
        }
        previous = pressed;
        if (k_uptime_get() >= next_status) {
            printk("Alive: internal-loopback bench waiting for SW0\n");
            next_status = k_uptime_get() + 5000;
        }
        k_msleep(20);
    }
    return 0;
}
