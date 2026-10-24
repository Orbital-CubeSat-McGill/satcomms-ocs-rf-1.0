#include <errno.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <ocs/radio.h>

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
    printk("RX CRC OK (%u bytes): ", (unsigned int)length);
    for (size_t i = 0; i < length; i++) {
        printk("%c", data[i] >= 32 && data[i] <= 126 ? (int)data[i] : '.');
    }
    printk("\nRX hex:");
    for (size_t i = 0; i < length; i++) {
        printk(" %02x", (unsigned int)data[i]);
    }
    printk("\n");
    if (length == 11 && memcmp(data, "Hello world", 11) == 0) {
        printk("PASS RF receive: expected Hello world\n");
    }
}

int main(void)
{
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
    printk("Ready: receiving. Press SW0 for about half a second to send once.\n");

    for (;;) {
        int pressed = gpio_pin_get_dt(&button);
        if (pressed < 0) { return pressed; }
        if (pressed != 0 && previous == 0) {
            const char message[] = "Hello world";
            result = ocs_radio_send(message, sizeof(message) - 1, 100);
            if (result == 0) {
                printk("TX complete: Hello world (11 payload bytes, CRC32 appended)\n");
                printk("Check the external capture; TX complete is a local result.\n");
            } else {
                printk("FAIL TX: %d; reconfiguring\n", result);
                if (configure_radio() != 0) { return result; }
            }
        }
        previous = pressed;

        uint8_t received[OCS_RADIO_MAX_PAYLOAD];
        size_t length;
        result = ocs_radio_receive(received, sizeof(received), &length, 100);
        if (result == 0) {
            print_packet(received, length);
        } else if (result != -ETIMEDOUT) {
            printk("RX error: %d; reconfiguring\n", result);
            if (configure_radio() != 0) { return result; }
        }
        if (k_uptime_get() >= next_status) {
            printk("RX waiting; SW0 sends one Hello world packet\n");
            next_status = k_uptime_get() + 5000;
        }
    }
    return 0;
}
