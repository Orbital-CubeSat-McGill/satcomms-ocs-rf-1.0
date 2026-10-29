#define DT_DRV_COMPACT atmel,at86rf215

#include <errno.h>
#include <zephyr/logging/log.h>

#include "at86rf215.h"

#define RADIO_NODE DT_NODELABEL(at86rf215)

LOG_MODULE_REGISTER(rf215);

//hardware configuration to call zephyr apis??
struct radio_hardware {
    struct spi_dt_spec spi;
    struct gpio_dt_spec reset;
    struct gpio_dt_spec irq;
    struct gpio_callback callback;
};

static struct radio_hardware hardware = {
    .spi = SPI_DT_SPEC_GET(
        RADIO_NODE,
        SPI_WORD_SET(8) | SPI_TRANSFER_MSB
    ),
    .reset = GPIO_DT_SPEC_GET(RADIO_NODE, reset_gpios),
    .irq = GPIO_DT_SPEC_GET(RADIO_NODE, irq_gpios)
};

//Forward declarations
//private
static int rf215_init(const struct device *dev);
static int rf215_reset(const struct device *dev);
static int rf215_irq_init(const struct device *dev);
static int rf215_spi_validate(
	const struct device *dev,
	struct at86rf215 *radio,
	uint16_t address,
	const uint8_t *data,
	size_t length
);
static int rf215_blk_write(
	const struct device *dev,
	struct at86rf215 *radio,
	uint16_t address,
	const uint8_t *data,
	size_t length
);
static int rf215_blk_read(
	const struct device *dev,
	struct at86rf215 *radio,
	uint16_t address,
	const uint8_t *data,
	size_t length
);

//public
static int rf215_state_get(const struct device *dev, uint8_t *state);



//--------------------------------------------------------------
//Private Functions definition


static int rf215_init(const struct device *dev) {
    int result;

    const struct rf215_config *cfg = (const struct rf215_config *)dev->config;

    const struct gpio_dt_spec *rf215 = &cfg->rf215;

    LOG_DBG("Initializing Radio module (instance: %u)\r\n", cfg->id);

    if (!spi_is_ready_dt(&hardware.spi) ||
        !gpio_is_ready_dt(&hardware.reset)) {
        return -ENODEV;
    }
    result = gpio_pin_configure_dt(
        &hardware.reset,
        GPIO_OUTPUT_ACTIVE
    );

    if (result < 0) {
	LOG_ERR("Could not configure GPIO as RST\r\n");
	return -ENODEV;
    }

    //pass context to other function through struct
    //like spi, gpio and irq pins
    radio->bus.ctx = &hardware;
    radio->bus.transfer = zephyr_transfer;

    //return at86rf215_zephyr_reset();
    return 0;
}



//Public funcitions
static int rf215_state_get(const struct device *dev, uint8_t *state) 
{
    int result;
    const struct rf215_config *cfg = (const struct rf215_config *)dev->config;
    // implementation with spi later
}



//-------------------------------------------------------------
//devicetree handling: MacroBatics


//define rf215 api
static const struct rf215_api rf215_api_funcs = {
    .get = rf215_state_get,
    //define rest here
};
