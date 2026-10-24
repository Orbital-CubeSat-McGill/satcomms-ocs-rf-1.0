#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <ocs/mcp251863_probe.h>

#define PROBE_NODE DT_NODELABEL(can_probe)
static const struct spi_dt_spec spi = SPI_DT_SPEC_GET(
    PROBE_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const struct gpio_dt_spec standby = GPIO_DT_SPEC_GET(PROBE_NODE, standby_gpios);
static const struct gpio_dt_spec interrupt = GPIO_DT_SPEC_GET(PROBE_NODE, int_gpios);
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

static int run_checks(void)
{
    const struct mcp251863_probe_bus bus = {.ctx = (void *)&spi, .transfer = transfer};
    uint32_t first_devid = 0;
    for (unsigned int i = 0; i < 10; i++) {
        /* Never enable the physical CAN transceiver for this probe. Its STBY
         * input has a pull-up inside U4; our MCU pin remains an input.
         */
        int level = gpio_pin_get_dt(&standby);
        if (level < 0) { return level; }
        if (level == 0) {
            printk("FAIL STBY is low; probe stopped without changing STBY\n");
            return -EBUSY;
        }
        struct mcp251863_probe_snapshot s;
        int result = mcp251863_probe_read(&bus, &s);
        if (result != 0) {
            printk("FAIL CRC-protected SPI read %u/10: %d\n", i + 1, result);
            return result;
        }
        if (i == 0) {
            first_devid = s.devid;
            printk("DEVID[0xe14]=0x%08x ID=%u REV=%u\n",
                   (unsigned int)s.devid, (unsigned int)((s.devid >> 4) & 15U),
                   (unsigned int)(s.devid & 15U));
            printk("OSC[0xe00]=0x%08x CiCON[0x000]=0x%08x OPMOD=%u\n",
                   (unsigned int)s.osc, (unsigned int)s.con,
                   (unsigned int)((s.con >> 21) & 7U));
            printk("STBY=HIGH (sampled only), INT active=%d\n",
                   gpio_pin_get_dt(&interrupt));
        }
        if (s.devid != first_devid) {
            printk("FAIL DEVID changed between reads\n");
            return -EIO;
        }
        if (!mcp251863_probe_clock_ready(&s)) {
            printk("PASS SPI CRC; FAIL oscillator/PLL readiness, OSC=0x%08x\n",
                   (unsigned int)s.osc);
            return -EAGAIN;
        }
        k_msleep(20);
    }
    printk("PASS 10 CRC-protected status/identity snapshots and clock readiness\n");
    printk("CAN controller SPI is responding; CANH/CANL operation is not tested\n");
    return 0;
}

int main(void)
{
    k_msleep(1500);
    printk("OCS CAN read-only bring-up: SAMD21 + MCP251863\n");
    printk("SPI mode 0, 100 kHz; no CAN/RF frames, register writes or reset commands\n");
    if (!spi_is_ready_dt(&spi) || !gpio_is_ready_dt(&standby) ||
        !gpio_is_ready_dt(&interrupt) || !gpio_is_ready_dt(&button)) {
        printk("FAIL SPI/GPIO device unavailable\n");
        return -ENODEV;
    }
    int result = gpio_pin_configure_dt(&standby, GPIO_INPUT);
    if (result == 0) { result = gpio_pin_configure_dt(&interrupt, GPIO_INPUT); }
    if (result == 0) { result = gpio_pin_configure_dt(&button, GPIO_INPUT); }
    if (result != 0) { printk("FAIL GPIO inputs: %d\n", result); return result; }

    int previous = gpio_pin_get_dt(&button);
    if (previous < 0) { return previous; }
    printk("Ready: release SW0, then press it to run the read-only CAN SPI checks\n");
    int64_t next_status = k_uptime_get() + 5000;
    for (;;) {
        int pressed = gpio_pin_get_dt(&button);
        if (pressed < 0) { return pressed; }
        if (pressed != 0 && previous == 0) {
            k_msleep(30);
            pressed = gpio_pin_get_dt(&button);
            if (pressed < 0) { return pressed; }
            if (pressed != 0) {
                result = run_checks();
                printk("CAN SPI CHECK %s (%d)\n", result == 0 ? "PASS" : "FAIL", result);
                printk("Release and press SW0 to run again\n");
            }
        }
        previous = pressed;
        if (k_uptime_get() >= next_status) {
            printk("Alive: CAN probe waiting; SW0 runs read-only checks\n");
            next_status = k_uptime_get() + 5000;
        }
        k_msleep(20);
    }
    return 0;
}
