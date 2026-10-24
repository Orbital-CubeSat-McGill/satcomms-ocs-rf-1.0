#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ocs/radio.h>

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

    return 0;
}

int main(void)
{
    k_msleep(1500);

    printk("OCS RF SPI identification: SAMD21 + AT86RF215\n");

    int result = run_checks();

    printk(
        "BRING-UP %s (%d)\n",
        result == 0 ? "PASS" : "FAIL",
        result
    );

    for (;;) {
        printk(
            "Alive: PN/VN check %s; reset to run again\n",
            result == 0 ? "PASS" : "FAIL"
        );

        k_msleep(5000);
    }

    return 0;
}
