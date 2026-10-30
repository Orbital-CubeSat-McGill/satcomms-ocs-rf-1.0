#ifndef ZEPHYR_DRIVERS_RF215_INTERNAL_H
#define ZEPHYR_DRIVERS_RF215_INTERNAL_H


#define RADIO_NODE DT_NODELABEL(at86rf215)


//Initialization of module

static void rf215_irq_isr(  const struct device *port,
                            struct gpio_callback *cb, 
                            uint32_t pins);
static int rf215_init(const struct device *dev);
static int rf215_reset(const struct device *dev);



//Abstraction layer over SPI driver 
static int rf215_blk_write(
	const struct device *dev,
	uint16_t address,
	const uint8_t *data,
	size_t length
);
static int rf215_blk_read(
	const struct device *dev,
	uint16_t address,
	const uint8_t *data,
	size_t length
);


//
#endif /*ZEPHYR_DRIVERS_RF215_INTERNAL_H
