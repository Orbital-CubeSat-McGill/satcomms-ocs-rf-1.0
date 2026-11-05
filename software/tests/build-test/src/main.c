/*
 * Build test for the at86rf215 module.
 *
 * Referencing the device and its API means this only links if the module was
 * imported and the driver instance was actually compiled in.
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <errno.h>

#include "rf215.h"

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(at86rf215));

int main(void)
{
    if (!device_is_ready(radio)) {
        printk("AT86RF215 not ready\n");
        return -ENODEV;
    }

    const struct rf215_api *api = radio->api;
    uint8_t part = 0;
    int ret = api->conn_check(radio, &part);

    printk("AT86RF215 conn_check: %d, part: 0x%02x\n", ret, part);

    return ret;
}
