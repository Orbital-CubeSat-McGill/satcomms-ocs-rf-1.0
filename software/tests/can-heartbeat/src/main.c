#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

static const struct gpio_dt_spec can_stby =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), can_stby_gpios);

int main(void)
{
	const struct device *can_dev = DEVICE_DT_GET(DT_NODELABEL(can0));

	if (!gpio_is_ready_dt(&can_stby)) {
		printk("CAN standby GPIO not ready\n");
		return -1;
	}

	int ret = gpio_pin_configure_dt(&can_stby, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		printk("Failed to enable CAN transceiver: %d\n", ret);
		return ret;
	}

	if (!device_is_ready(can_dev)) {
		printk("CAN controller (mcp251xfd) not ready\n");
		return -1;
	}

	printk("CAN controller (mcp251xfd) is ready!\n");

	struct can_frame frame = {
		.id = 0x123,
		.dlc = 8,
		.flags = 0,
		.data = {0}
	};

	ret = can_start(can_dev);
	if (ret != 0) {
		printk("Failed to start CAN controller: %d\n", ret);
		return ret;
	}

	uint8_t counter = 0;
	while (1) {
		frame.data[7] = counter++;
		ret = can_send(can_dev, &frame, K_FOREVER, NULL, NULL);
		if (ret != 0) {
			printk("can_send failed: %d\n", ret);
		} else {
			printk("Sending frame: counter=%u\n", frame.data[7]);
		}
		k_msleep(1000);
	}

	return 0;
}
