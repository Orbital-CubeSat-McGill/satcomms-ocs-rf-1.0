#include <errno.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <ocs/radio.h>

enum { TX_PACKET_COUNT = 10 };
static const char tx_pattern[] = "Hello world";
static uint8_t tx_message[OCS_RADIO_MAX_PAYLOAD];
static uint8_t received[OCS_RADIO_MAX_PAYLOAD];

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct ocs_radio_config config = {
    .frequency_hz = 915000000U,
    .tx_power_level = 0 /* Minimum chip setting, not a calibrated dBm value. */
};

static int configure_radio(void)
{
    int result = ocs_radio_configure(&config);
    if (result != 0) {
        printk("FAIL RF configure/PLL check: %d\n", result);
        return result;
    }
    printk("PASS RF configuration: 915 MHz, 2-FSK 50 kb/s, CRC32\n");
    printk("PLL readiness checked; radio returns to TRXOFF between operations\n");
    return 0;
}

static void print_packet(const uint8_t *data, size_t length)
{
    size_t preview = MIN(length, 64U);
    printk("RX CRC OK (%u bytes; showing first %u): ",
           (unsigned int)length, (unsigned int)preview);
    for (size_t i = 0; i < preview; i++) {
        printk("%c", data[i] >= 32 && data[i] <= 126 ? (int)data[i] : '.');
    }
    printk("\nRX hex:");
    for (size_t i = 0; i < preview; i++) {
        printk(" %02x", (unsigned int)data[i]);
    }
    printk("\n");
    if (length == 11 && memcmp(data, "Hello world", 11) == 0) {
        printk("PASS RF receive: expected Hello world\n");
    } else if (length == sizeof(tx_message) &&
               memcmp(data, tx_message, length) == 0) {
        printk("PASS RF receive: expected SDR test message\n");
    }
}

int main(void)
{
    /* Fill one maximum-size payload with repeated readable text, no NUL. */
    for (size_t i = 0; i < sizeof(tx_message); i++) {
        tx_message[i] = (uint8_t)tx_pattern[i % (sizeof(tx_pattern) - 1)];
    }
    k_msleep(1500);
    printk("OCS RF packet bench: real RF operations, button-triggered TX\n");
    if (!gpio_is_ready_dt(&button)) {
        printk("FAIL button GPIO unavailable\n");
        return -ENODEV;
    }
    int result = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (result != 0) { return result; }
    result = ocs_radio_init();
    if (result != 0) {
        printk("FAIL radio init/presence: %d\n", result);
        return result;
    }
    uint8_t identity[2];
    result = ocs_radio_read(OCS_RADIO_REG_PN, identity, sizeof(identity));
    if (result != 0) { return result; }
    printk("PN=0x%02x VN=0x%02x\n",
           (unsigned int)identity[0], (unsigned int)identity[1]);
    result = configure_radio();
    if (result != 0) { return result; }

    /* A button held during boot must be released before it can trigger TX. */
    int previous = gpio_pin_get_dt(&button);
    if (previous < 0) { return previous; }
    int64_t next_status = k_uptime_get() + 5000;
    printk("Ready: receiving. Hold SW0 for about two seconds to send %u packets,\n",
           (unsigned int)TX_PACKET_COUNT);
    printk("with a one-second delay between packets (%u payload bytes each).\n",
           (unsigned int)sizeof(tx_message));

    for (;;) {
        int pressed = gpio_pin_get_dt(&button);
        if (pressed < 0) { return pressed; }
        if (pressed != 0 && previous == 0) {
            for (unsigned int i = 0; i < TX_PACKET_COUNT; i++) {
                result = ocs_radio_send(tx_message, sizeof(tx_message), 1000);
                if (result != 0) {
                    printk("FAIL TX %u/%u: %d; stopping batch and reconfiguring\n",
                           i + 1, (unsigned int)TX_PACKET_COUNT, result);
                    if (configure_radio() != 0) { return result; }
                    break;
                }
                printk("TX complete %u/%u: %u payload bytes, CRC32 appended\n",
                       i + 1, (unsigned int)TX_PACKET_COUNT,
                       (unsigned int)sizeof(tx_message));
                if (i + 1 < TX_PACKET_COUNT) {
                    k_msleep(1000);
                }
            }
            printk("TX batch ended; receiving again. Check the external capture.\n");
        }
        previous = pressed;

        size_t length;
        result = ocs_radio_receive(received, sizeof(received), &length, 1000);
        if (result == 0) {
            print_packet(received, length);
        } else if (result != -ETIMEDOUT) {
            printk("RX error: %d; reconfiguring\n", result);
            if (configure_radio() != 0) { return result; }
        }
        if (k_uptime_get() >= next_status) {
            printk("RX waiting; SW0 sends %u packets with one-second delays\n",
                   (unsigned int)TX_PACKET_COUNT);
            next_status = k_uptime_get() + 5000;
        }
    }
    return 0;
}
