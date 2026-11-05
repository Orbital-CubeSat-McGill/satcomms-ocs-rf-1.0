/*
 * Hardware test: does a block read of the four IRQ status registers acknowledge
 * them? The datasheet (section 5.3) says yes, the upstream driver says no.
 *
 * A chip reset sets WAKEUP on both radios and raises the IRQ pin. The driver's
 * interrupt is turned off first, so only this test reads the status registers.
 * Console is 115200 8N1. The radio is never told to transmit.
 *
 * single: reads RF09_IRQS then RF24_IRQS. This is the control: the pin has to
 *         stay high after the first read, because RF24 is still pending. If it
 *         does not, this test can't see an uncleared IRQ and block means nothing.
 * block:  reads all four at once, then again. The pin should be low and the
 *         second read all zero. If the pin is high, block reads don't clear.
 *
 * driver: turns the driver's interrupt back on and resets the chip again. The
 *         driver's own ISR and IRQ work should post the WAKEUP events for both
 *         radios and leave the pin low.
 *
 * Only WAKEUP is exercised, so this says nothing about the BBCn registers.
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"

#define RF_RST_CMD_RESET 0x07
#define RESET_WAIT_MS    10
#define PIN_SETTLE_US    100

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(at86rf215));

static int irq_pin(const struct rf215_config *cfg)
{
    k_busy_wait(PIN_SETTLE_US);
    return gpio_pin_get_dt(&cfg->rf215_irq);
}

/* Chip reset, leaving WAKEUP pending on both radios. Returns the pin level. */
static int chip_reset(const struct rf215_config *cfg)
{
    uint8_t cmd = RF_RST_CMD_RESET;

    rf215_blk_write(radio, REG_RF_RST, &cmd, 1);
    k_msleep(RESET_WAIT_MS);
    return irq_pin(cfg);
}

static bool test_single(const struct rf215_config *cfg)
{
    uint8_t rf09 = 0xAA, rf24 = 0xAA;
    int before = chip_reset(cfg);

    rf215_blk_read(radio, REG_RF09_IRQS, &rf09, 1);
    int after_rf09 = irq_pin(cfg);

    rf215_blk_read(radio, REG_RF24_IRQS, &rf24, 1);
    int after_rf24 = irq_pin(cfg);

    bool pass = before == 1 && rf09 == RF_WAKEUP && after_rf09 == 1 &&
                rf24 == RF_WAKEUP && after_rf24 == 0;

    printk("single %s: pin=%d rf09=0x%02x pin=%d rf24=0x%02x pin=%d\n",
           pass ? "PASS" : "FAIL", before, rf09, after_rf09, rf24, after_rf24);
    return pass;
}

static bool test_block(const struct rf215_config *cfg)
{
    uint8_t first[4] = {0xAA, 0xAA, 0xAA, 0xAA};
    uint8_t second[4] = {0xAA, 0xAA, 0xAA, 0xAA};
    int before = chip_reset(cfg);

    rf215_blk_read(radio, REG_RF09_IRQS, first, sizeof(first));
    int after_first = irq_pin(cfg);

    rf215_blk_read(radio, REG_RF09_IRQS, second, sizeof(second));
    int after_second = irq_pin(cfg);

    bool pass = before == 1 && first[0] == RF_WAKEUP && first[1] == RF_WAKEUP &&
                after_first == 0 && after_second == 0 &&
                (second[0] | second[1] | second[2] | second[3]) == 0;

    printk("block  %s: pin=%d first=%02x %02x %02x %02x pin=%d second=%02x %02x %02x %02x pin=%d\n",
           pass ? "PASS" : "FAIL", before,
           first[0], first[1], first[2], first[3], after_first,
           second[0], second[1], second[2], second[3], after_second);
    return pass;
}

static bool test_driver(const struct rf215_config *cfg)
{
    struct rf215_data *data = radio->data;
    const uint32_t wakeup = RF09_EVT_WAKEUP | RF24_EVT_WAKEUP;

    k_event_clear(&data->events, wakeup);
    gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_LEVEL_ACTIVE);

    chip_reset(cfg);
    uint32_t events = k_event_wait_all(&data->events, wakeup, false, K_MSEC(100));
    int after = irq_pin(cfg);

    /* Back to off, so the next round's direct reads stay the only readers */
    gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_DISABLE);

    bool pass = events == wakeup && after == 0;

    printk("driver %s: events=0x%03x pin=%d\n", pass ? "PASS" : "FAIL", events, after);
    return pass;
}

int main(void)
{
    const struct rf215_config *cfg = radio->config;
    unsigned int round = 0, single_pass = 0, block_pass = 0, driver_pass = 0;

    if (!device_is_ready(radio)) {
        printk("AT86RF215 not ready, driver init failed\n");
        return 0;
    }

    /* Init has to leave nothing pending, or the driver's interrupt would be firing */
    printk("after init: pin=%d\n", irq_pin(cfg));

    /* The direct reads need the driver's IRQ work out of the way */
    gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_DISABLE);

    while (1) {
        round++;
        single_pass += test_single(cfg);
        block_pass += test_block(cfg);
        driver_pass += test_driver(cfg);
        printk("[%u] totals: single %u/%u, block %u/%u, driver %u/%u\n",
               round, single_pass, round, block_pass, round, driver_pass, round);
        k_msleep(2000);
    }

    return 0;
}
