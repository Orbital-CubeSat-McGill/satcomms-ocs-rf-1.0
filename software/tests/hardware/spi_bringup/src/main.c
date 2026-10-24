#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ocs/at86rf215_zephyr.h>
#include <ocs/radio.h>

#include "at86rf215_regs.h"

static int check_write_readback(
    uint16_t address,
    const uint8_t *pattern,
    size_t length
)
{
    uint8_t saved[OCS_RADIO_MAX_TRANSFER];
    uint8_t actual[OCS_RADIO_MAX_TRANSFER];

    if (length == 0 || length > sizeof(saved)) {
        return -EINVAL;
    }

    int result = ocs_radio_read(
        address, saved, length
    );

    if (result != 0) {
        return result;
    }

    result = ocs_radio_write(
        address, pattern, length
    );

    if (result == 0) {
        result = ocs_radio_read(
            address, actual, length
        );

        if (result == 0 &&
            address == OCS_RADIO_TX_BUFFER) {
            printk("SPI received: ");

            for (size_t i = 0; i < length; i++) {
                printk("%c", (int)actual[i]);
            }

            printk("\n");
        }

        if (result == 0 &&
            memcmp(actual, pattern, length) != 0) {
            result = -EIO;
        }
    }

    /* Restore even if the test write or read failed. */
    int restore = ocs_radio_write(
        address, saved, length
    );

    if (restore != 0) {
        printk(
            "RESTORE FAILED at 0x%04x: %d; reset radio\n",
            (unsigned int)address,
            restore
        );
        return restore;
    }

    if (result != 0) {
        return result;
    }

    result = ocs_radio_read(
        address, actual, length
    );

    if (result != 0) {
        return result;
    }

    return memcmp(actual, saved, length) == 0 ? 0 : -EIO;
}

static int run_checks(void)
{
    int result = ocs_radio_init();

    if (result != 0) {
        printk("FAIL simple API init/presence check: %d\n", result);
        return result;
    }

    printk("PASS simple API init/presence check\n");

    /* Read the radio's read-only part-number register over real SPI. */
    uint8_t part_number;

    result = ocs_radio_read_reg(
        OCS_RADIO_REG_PN,
        &part_number
    );

    if (result != 0) {
        printk("FAIL RF_PN register read: %d\n", result);
        return result;
    }

    printk(
        "RF_PN register 0x%04x: read=0x%02x expected=0x34\n",
        (unsigned int)OCS_RADIO_REG_PN,
        (unsigned int)part_number
    );

    uint8_t id[2];

    result = ocs_radio_read(
        OCS_RADIO_REG_PN,
        id,
        sizeof(id)
    );

    if (result != 0) {
        printk("FAIL ID read: %d\n", result);
        return result;
    }

    printk(
        "PN=0x%02x VN=0x%02x\n",
        (unsigned int)id[0],
        (unsigned int)id[1]
    );

    if (part_number != 0x34 || id[0] != 0x34) {
        printk("FAIL unexpected part number\n");
        return -ENODEV;
    }

    printk("PASS part-number register read\n");
    printk("PASS identification and block read\n");

    uint8_t state;

    result = ocs_radio_read_reg(
        AT86RF215_REG_RF09_STATE,
        &state
    );

    if (result != 0) {
        return result;
    }

    if ((state & 0x07U) != AT86RF215_STATE_TRXOFF) {
        printk(
            "FAIL expected TRXOFF, got 0x%02x\n",
            (unsigned int)state
        );
        return -EBUSY;
    }

    uint8_t mask;

    result = ocs_radio_read_reg(
        AT86RF215_REG_RF09_IRQM,
        &mask
    );

    if (result != 0) {
        return result;
    }

    mask ^= AT86RF215_IRQ_TRXRDY;

    result = check_write_readback(
        AT86RF215_REG_RF09_IRQM,
        &mask,
        1
    );

    if (result != 0) {
        printk("FAIL register write/readback: %d\n", result);
        return result;
    }

    printk("PASS register write/readback/restore\n");

    const uint8_t pattern[] = "Hello world";

    printk("SPI sending: %s\n", (const char *)pattern);

    result = check_write_readback(
        OCS_RADIO_TX_BUFFER,
        pattern,
        sizeof(pattern) - 1
    );

    if (result != 0) {
        printk("FAIL Hello world test: %d\n", result);
        return result;
    }

    printk("PASS Hello world write/readback/restore\n");

    result = at86rf215_zephyr_irq_init();

    if (result != 0) {
        printk("FAIL IRQ setup: %d\n", result);
        return result;
    }

    uint8_t status[4];

    /* Clear existing RF09, RF24, BBC0 and BBC1 events. */
    result = ocs_radio_read(
        AT86RF215_REG_RF09_IRQS,
        status,
        sizeof(status)
    );

    if (result != 0) {
        return result;
    }

    k_msleep(1);

    int level = at86rf215_zephyr_irq_level();

    if (level != 0) {
        printk("FAIL IRQ did not clear: %d\n", level);
        return -EIO;
    }

    result = at86rf215_zephyr_reset();

    if (result != 0) {
        return result;
    }

    result = at86rf215_zephyr_wait_irq(100);
    level = at86rf215_zephyr_irq_level();

    printk("Reset IRQ wait=%d level=%d\n", result, level);

    if (result != 0) {
        return result;
    }

    if (level != 1) {
        return -EIO;
    }

    result = ocs_radio_read(
        AT86RF215_REG_RF09_IRQS,
        status,
        sizeof(status)
    );

    if (result != 0) {
        return result;
    }

    printk(
        "IRQ snapshot RF09=%02x RF24=%02x BBC0=%02x BBC1=%02x\n",
        (unsigned int)status[0],
        (unsigned int)status[1],
        (unsigned int)status[2],
        (unsigned int)status[3]
    );

    if ((status[0] & AT86RF215_IRQ_WAKEUP) == 0 ||
        (status[1] & AT86RF215_IRQ_WAKEUP) == 0) {
        return -EIO;
    }

    k_msleep(1);

    if (at86rf215_zephyr_irq_level() != 0) {
        return -EIO;
    }

    printk("PASS reset, GPIO interrupt and read-to-clear status\n");

    for (unsigned int i = 0; i < 100; i++) {
        uint8_t pn;

        result = ocs_radio_read_reg(
            OCS_RADIO_REG_PN,
            &pn
        );

        if (result != 0 || pn != 0x34) {
            printk("FAIL repeat %u, error=%d\n", i, result);
            return result != 0 ? result : -EIO;
        }

        k_msleep(1);
    }

    printk("PASS 100 repeated ID reads\n");

    return 0;
}

int main(void)
{
    k_msleep(1500);

    printk("OCS RF bring-up: SAMD21 + AT86RF215\n");

    int result = run_checks();

    printk(
        "BRING-UP %s (%d)\n",
        result == 0 ? "PASS" : "FAIL",
        result
    );

    for (;;) {
        printk(
            "Alive: bring-up %s; reset to run again\n",
            result == 0 ? "PASS" : "FAIL"
        );

        k_msleep(5000);
    }

    return 0;
}
