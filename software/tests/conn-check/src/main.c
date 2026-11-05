/*
 * Hardware test for the at86rf215 driver's SPI path.
 *
 * Reads the part number register through conn_check once a second and prints
 * the result. Console is 115200 8N1. The radio is never told to transmit.
 *
 * ret=0 part=0x34       AT86RF215 answered (0x35 is the IQ, 0x36 the M variant)
 * ret=-6 part=0x00/0xff nothing is answering (SPI has no ack): check power,
 *                       reset, CS, SCK, MOSI and MISO
 * ret=-6 part=other     something answered but the bits are wrong: check for
 *                       clock mode or signal integrity problems
 * "not ready"           the driver's init failed, see the log line above it
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rf215.h"

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(at86rf215));

int main(void)
{
    const struct rf215_api *api = radio->api;
    unsigned int count = 0;

    while (1) {
        if (device_is_ready(radio)) {
            uint8_t part = 0;
            int ret = api->conn_check(radio, &part);

            printk("[%u] conn_check ret=%d part=0x%02x\n", count, ret, part);
        } else {
            printk("[%u] AT86RF215 not ready, driver init failed\n", count);
        }

        count++;
        k_msleep(1000);
    }

    return 0;
}
