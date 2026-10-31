#define DT_DRV_COMPACT atmel,at86rf215

#include <errno.h>
#include <zephyr/logging/log.h>
#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"


LOG_MODULE_REGISTER(rf215);


//forward declarations of functions passed through to the api
//switch uint8_t to it's respective enum if applicable
static int rf215_confirm_conn(const struct device *dev); //confirm at86rf215 is connected and alive
static int rf215_set_mode(const struct device *dev, uint8_t mode); //set Mode of transceiver (ex:sleep)
static int rf215_bb_conf(const struct device *dev, struct rf215_bbX_config); //set radio configuration from struct
static int rf215_set_value(const struct device *dev, struct radio, uint8_t set_variable); //set a varible from set_values enum to value
static int rf215_get_value(const struct device *dev, uint8_t *value, uint8_t get_variable);//get a varible from get_values enum to value
static int rf215_bb_enable(const struct device *dev, struct radio, uint8_t en);
static int rf215_tx_frame(const struct device *dev, struct radio, size_t timeout_ms);
static int rf215_rx_frame(const struct device *dev, struct radio, size_t timeout_ms);
static int rf215_rx(const struct device *dev, struct radio, size_t timeout_ms);

//--------------------------------------------------------------
//Private Functions definition
/* ISR context: no SPI, no blocking */
static void rf215_irq_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    struct rf215_data *data = CONTAINER_OF(cb, struct rf215_data, irq_cb);
    const struct rf215_config *cfg = data->dev->config;

    gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_DISABLE);
    k_work_submit(&data->irq_work);
}

static void rf215_post_events(const uint8_t* irqs, struct rf215_data* data) {
    uint32_t events = 0;

    if (irqs[0] & RF_TRXRDY) {
        events |= RF215_EVT_TRXRDY;
    }
    if (irqs[0] & RF_TRXERR) {
        events |= RF215_EVT_ERROR;
    }

    // TODO implement the rest of the interrupt signals 

    if (events != 0) {
        k_event_post(&data->events, events);
    }
}

/* Thread context: SPI is fine here */
static void rf215_irq_work(struct k_work *work)
{
    struct rf215_data *data = CONTAINER_OF(work, struct rf215_data, irq_work);
    const struct device *dev = data->dev;
    const struct rf215_config *cfg = dev->config;
    uint8_t irqs[4];  /* RF09, RF24, BBC0, BBC1 */

    int read_return = rf215_blk_read(dev, REG_RF09_IRQS, irqs, sizeof(irqs));
    if (read_return == 0) {
        rf215_post_events(irqs, data);
    }
    else {
        LOG_ERR("Could not read from IRQ registers, Error Code: %d\n", read_return);
    }

    gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_LEVEL_ACTIVE);
}

static int rf215_init(const struct device *dev) {
    int result;

    const struct rf215_config *cfg = (const struct rf215_config *)dev->config;
    struct rf215_data *data = dev->data;

    const struct spi_dt_spec *rf215_spi = &cfg->rf215_spi;
    const struct gpio_dt_spec *rf215_rst = &cfg->rf215_rst;
    const struct gpio_dt_spec *rf215_irq = &cfg->rf215_irq;

    data->dev = dev;
    k_mutex_init(&data->bus_lock);
    k_work_init(&data->irq_work, rf215_irq_work);
    k_event_init(&data->events);

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

    // Call reset here to set Register configurations in init
    rf215_reset(dev);

    gpio_init_callback(&data->irq_cb, rf215_irq_isr, BIT(cfg->rf215_irq.pin));
    result = gpio_add_callback(cfg->rf215_irq.port, &data->irq_cb);
    if (result) {
        return result;
    }

    //checks and discards stale IRQ state
    uint8_t stale_irqs[4];
    int irq_result = rf215_blk_read(dev, REG_RF09_IRQS, stale_irqs, sizeof(stale_irqs)) ;
    if (irq_result != 0) {
        LOG_ERR("Could not read from IRQ registers. Error Code: %d\n", irq_result);
        return irq_result;
    }

    return gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_LEVEL_ACTIVE);
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

    // set all register configurations here
    // set operating mode to baseband for both transceivers
    rf215_blk_write(dev, REG_RF_IQIFC1, RF215_MODE_BBRF, 1);
    return 0;
}


static int rf215_state_get(const struct device *dev, rf215_state_t *state, rf215_radio_t radio)
{
    const struct rf215_config *cfg = (const struct rf215_config *)dev->config;

    if (!state) {
        return -AT86RF215_INVAL_PARAM;
    }
    uint8_t  val = 0;
    uint16_t reg = 0;
    switch (radio) {
        case RF24:
            reg = REG_RF24_STATE;
            break;
        case RF09:
            reg = REG_RF09_STATE;
            break;
        default:
            return -EINVAL;
    }
    ret = rf215_blk_read(dev, reg, &val, 1);
    if (ret) {
        return ret;
    }
    /* Sanity check on the returned value */
    val &= 0x7;
    switch (val) {
        case STATE_RF_TRXOFF:
        case STATE_RF_TXPREP:
        case STATE_RF_TX:
        case STATE_RF_RX:
        case STATE_RF_TRANSITION:
        case STATE_RF_RESET:
            *state = (rf215_state_t)val;
            break;
        default:
            return -EINVAL;
    }
    return AT86RF215_OK;
}


//Public funcitions

//-------------------------------------------------------------
//devicetree handling: MacroBatics


//define rf215 api
static const struct rf215_api rf215_api_funcs = {
    .get = rf215_state_get,
    //define rest here
};
