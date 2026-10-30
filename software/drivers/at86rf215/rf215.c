#define DT_DRV_COMPACT atmel,at86rf215

#include <errno.h>
#include <zephyr/logging/log.h>
#include "rf215.h"
#include "rf215_internal.h"


LOG_MODULE_REGISTER(rf215);


//forward declarations of functions passed through to the api
static int rf215_state_get(const struct device *dev, uint8_t *state);



//--------------------------------------------------------------
//Private Functions definition
/* ISR context: no SPI, no blocking */
static void rf215_irq_isr(const struct device *port,
                              struct gpio_callback *cb, uint32_t pins)
{
    struct rf215_trigs *trigs = CONTAINER_OF(cb, struct rf215_trigs, irq_cb);
    const struct rf215_config *cfg = trigs->dev->config;

    gpio_pin_interrupt_configure_dt(&cfg->irq_gpio, GPIO_INT_DISABLE);
    k_work_submit(&trigs->irq_work);
}

/* Thread context: SPI is fine here */
static void rf215_irq_work(struct k_work *work)
{
    struct rf215_trigs *trigs = CONTAINER_OF(work, struct rf215_trigs, irq_work);
    const struct device *dev = trigs->dev;
    const struct rf215_config *cfg = dev->config;
    uint8_t irqs[4];  /* RF09, RF24, BBC0, BBC1 */

    if (rf215_read(dev, RF09_IRQS, irqs, sizeof(irqs)) == 0) {
        /* reading clears the flags; dispatch on irqs[0..3] here */
        /* call internal function to handle interrupt */
    }

    gpio_pin_interrupt_configure_dt(&cfg->irq_gpio, GPIO_INT_LEVEL_ACTIVE);
}

static int rf215_init(const struct device *dev) {
    int result;

    const struct rf215_config *cfg = (const struct rf215_config *)dev->config;
    struct rf215_trigs *trigs = dev->trigs;

    const struct spi_dt_spec *rf215_spi = &cfg->rf215_spi;
    const struct gpio_dt_spec *rf215_rst = &cfg->rf215_rst;
    const struct gpio_dt_spec *rf215_irq = &cfg->rf215_irq;

    trigs->dev = dev;
    k_mutex_init(&trigs->bus_lock);
    k_work_init(&trigs->irq_work, rf215_irq_work);

    LOG_DBG("Initializing Radio module (instance: %u)\r\n", cfg->id);

    if (!spi_is_ready_dt(rf215_spi) ||
        !gpio_is_ready_dt(rf215_rst) ||
        !gpio_is_ready_dt(rf215_irq)) {
        return -ENODEV;
    }

    result = gpio_pin_configure_dt(
        rf215_rst,
        GPIO_OUTPUT_ACTIVE
    );

    if (result < 0) {
        LOG_ERR("Could not configure GPIO as RST\r\n");
	return -ENODEV;
    }

    result = gpio_pin_configure_dt(
        rf215_irq,
        GPIO_INPUT
    );

    if (result < 0) {
        LOG_ERR("Could not configure GPIO as IRQ\r\n");
	return -ENODEV;
    }

    LOG_DBG("Initializing Radio module interrupts (instance: %u)\r\n", cfg->id);


    gpio_init_callback(&trigs->irq_cb, rf215_irq_isr, BIT(cfg->irq_gpio.pin));
    result = gpio_add_callback(cfg->irq_gpio.port, &trigs->irq_cb);
    if (result) {
        return result;
    }

    /* ... reset chip, configure IRQCFG / IRQ masks over SPI first ... */

    return gpio_pin_interrupt_configure_dt(&cfg->irq_gpio, GPIO_INT_LEVEL_ACTIVE);
}

static int rf215_reset(const struct device *dev) 
{
    int result;

    const struct rf215_config *cfg = (const struct rf215_config *)dev->config;

    const struct gpio_dt_spec *rf215_gpio = &cfg->rf215;

    result = gpio_pin_set_dt(rf215_gpio, 1);

    if (result != 0) {
        return result;
    }

    k_msleep(1);

    result = gpio_pin_set_dt(rf215_gpio, 0);

    if (result != 0) {
        return result;
    }

    k_msleep(10);

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
