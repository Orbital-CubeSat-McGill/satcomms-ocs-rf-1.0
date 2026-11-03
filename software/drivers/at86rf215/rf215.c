#define DT_DRV_COMPACT atmel,at86rf215

#include <errno.h>
#include <zephyr/logging/log.h>
#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"


LOG_MODULE_REGISTER(rf215);


//forward declarations of functions passed through to the api
//switch uint8_t to it's respective enum if applicable
static int rf215_conn_check(const struct device *dev); //confirm at86rf215 is connected and alive
static int rf215_set_sleep(const struct device *dev, rf215_radio_t radio, uint8_t en); //set sleep state of transceiver or trxoff 
static int rf215_select_preset(const struct device *dev, rf215_radio_t radio, uint8_t idx);  /* validate, lock, apply */
static int rf215_set_channel(const struct device *dev, rf215_radio_t radio, uint16_t ch);    /* checked against active preset */
static int rf215_set_tx_power(const struct device *dev, rf215_radio_t radio, int8_t dbm);
static int rf215_bb_enable(const struct device *dev, rf215_radio_t radio, uint8_t en);
static int rf215_get_value(const struct device *dev, uint8_t *value, rf215_get get_variable);//get a varible from get_values enum to value
static int rf215_tx_buffer(const struct device *dev, rf215_radio_t radio, size_t timeout_ms); // write to tx buffer
static int rf215_tx(const struct device *dev, rf215_radio_t radio, size_t timeout_ms); //Flush tx buffer and transmit data
static int rf215_rx(const struct device *dev, rf215_radio_t radio, size_t timeout_ms); // set transceiver to RX mode
static int rf215_rx_frame(const struct device *dev, rf215_radio_t radio, size_t timeout_ms); // Get data from RX buffer

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
    // IRQ mask setup (when writint to this register you enable these types of interrupts)
    // section 5.3.2.1
    //
    //
    // Set 2.4Ghz transciever to sleep as it not used this flight
    return 0;
}

//------------------------------------------------------------------------------
// General purpose method
//------------------------------------------------------------------------------

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


//------------------------------------------------------------------------------
//Public funcitions
//------------------------------------------------------------------------------

//returns part number
static int rf215_conn_check(const struct device *dev)
{
    uint8_t part = 0;
    int ret = rf215_blk_read(dev, REG_RF_PN, &part, 1);
    if (ret) {
        return ret;
    }
  switch (part) {
  case RF215:
  case RF215IQ:
  case RF215M:
    return RF215_OK;
  default:
    return -RF215_UNKNOWN_IC;
  }
}

// Set Transceiver to sleep
// wrapper to call private function
static int rf215_set_sleep(const struct device *dev, rf215_radio_t radio, uint8_t en)
{
    if (en)
    {
        return rf215_cmd(dev, CMD_SLEEP, radio);
    }else{
        return rf215_cmd(dev, CMD_TRXOFF, radio);
}

//------------------------------------------------------------------------------
// Start of definition of radio function for transmission and receiving
//------------------------------------------------------------------------------

static int
write_tx_buffer(struct at86rf215 *h, at86rf215_radio_t radio, const uint8_t *b,
                size_t len)
{
  int ret = 0;
  if (radio == AT86RF215_RF09) {
    /* Declare the size of the PSDU */
    ret = at86rf215_reg_write_8(h, len & 0xFF, REG_BBC0_TXFLL);
    if (ret) {
      return ret;
    }
    ret = at86rf215_reg_write_8(h, (len >> 8) & 0xFF, REG_BBC0_TXFLH);
    if (ret) {
      return ret;
    }

    /* Fill the buffer */
    ret = at86rf215_set_seln(h, 0);
    if (ret) {
      return ret;
    }
    at86rf215_irq_enable(h, 0);
    uint8_t mosi[2] = {(REG_BBC0_FBTXS >> 8) | 0x80, REG_BBC0_FBTXS & 0xFF};
    ret             = at86rf215_spi_write(h, mosi, 2);
    if (ret) {
      at86rf215_set_seln(h, 1);
      at86rf215_irq_enable(h, 1);
      return ret;
    }
    ret = at86rf215_spi_write(h, b, len);
    if (ret) {
      at86rf215_set_seln(h, 1);
      at86rf215_irq_enable(h, 1);
      return ret;
    }
  } else {
    /* Declare the size of the PSDU */
    ret = at86rf215_reg_write_8(h, len & 0xFF, REG_BBC1_TXFLL);
    if (ret) {
      return ret;
    }
    ret = at86rf215_reg_write_8(h, (len >> 8) & 0xFF, REG_BBC1_TXFLH);
    if (ret) {
      return ret;
    }

    /* Fill the buffer */
    ret = at86rf215_set_seln(h, 0);
    if (ret) {
      return ret;
    }
    at86rf215_irq_enable(h, 0);
    uint8_t mosi[2] = {(REG_BBC1_FBTXS >> 8) | 0x80, REG_BBC1_FBTXS & 0xFF};
    ret             = at86rf215_spi_write(h, mosi, 2);
    if (ret) {
      at86rf215_set_seln(h, 1);
      at86rf215_irq_enable(h, 1);
      return ret;
    }
    ret = at86rf215_spi_write(h, b, len);
    if (ret) {
      at86rf215_set_seln(h, 1);
      at86rf215_irq_enable(h, 1);
      return ret;
    }
  }
  at86rf215_irq_enable(h, 1);
  return at86rf215_set_seln(h, 1);
}

/**
 * @brief Receives a (possibly) received frame from the frame buffer
 *
 * @note This function transfers only the contents of the frame buffer. It does
 * not check for their validity.
 *
 * @param h the device handle
 * @param radio the RF frontend
 * @param psdu buffer to store the received data
 * @param len the number of bytes to read from the frame buffer
 * @return 0 on success or negative error code
 */
int
at86rf215_rx_frame(struct at86rf215 *h, at86rf215_radio_t radio, uint8_t *psdu,
                   size_t len)
{
  int ret = supports_rf(h, radio);
  if (ret) {
    return ret;
  }
  if (!psdu) {
    return -AT86RF215_INVAL_PARAM;
  }

  ret = at86rf215_set_seln(h, 0);
  if (ret) {
    return ret;
  }
  at86rf215_irq_enable(h, 0);
  /* Construct properly the MOSI buffer */
  const uint16_t reg =
      radio == AT86RF215_RF09 ? REG_BBC0_FBRXS : REG_BBC1_FBRXS;
  uint8_t mosi[2] = {(reg >> 8) & 0x3F, reg & 0xFF};
  ret             = at86rf215_spi_read(h, spi_buffer, mosi, 2, len + 2);
  at86rf215_irq_enable(h, 1);
  at86rf215_set_seln(h, 1);
  memcpy(psdu, spi_buffer + 2, len);
  return ret;
}

/**
 * Transmits a frame using the configured baseband core mode
 * @note the chip mode should ensure that the corresponding baseband core
 * is enabled
 * @param h the device handle
 * @param radio the RF fronted
 * @param psdu the data to send
 * @param len the number of bytes to send
 * @return 0 on success or negative error code
 */
int
at86rf215_tx_frame(struct at86rf215 *h, at86rf215_radio_t radio,
                   const uint8_t *psdu, size_t len, size_t timeout_ms)
{
  size_t deadline = at86rf215_get_time_ms(h) + timeout_ms;
  int    ret      = supports_rf(h, radio);
  if (ret) {
    return ret;
  }
  if (!psdu) {
    return -AT86RF215_INVAL_PARAM;
  }

  /*
   * In order to transmit the frame, the corresponding baseband core should
   * be enabled
   */
  if (h->priv.chpm == AT86RF215_RF_MODE_RF) {
    return -AT86RF215_INVAL_CHPM;
  }
  if (radio == AT86RF215_RF09) {
    if (h->priv.chpm == AT86RF215_RF_MODE_BBRF09) {
      return -AT86RF215_INVAL_CHPM;
    }
  } else {
    if (h->priv.chpm == AT86RF215_RF_MODE_BBRF24) {
      return -AT86RF215_INVAL_CHPM;
    }
  }

  /*
   * Write at least once the TXPREP so we are sure that we can start the
   * transmission
   */
  at86rf215_rf_state_t state = AT86RF215_STATE_RF_TRANSITION;
  while (state != AT86RF215_STATE_RF_TXPREP) {
    if (at86rf215_get_time_ms(h) > deadline) {
      return -AT86RF215_TIMEOUT;
    }
    at86rf215_set_cmd(h, AT86RF215_CMD_RF_TXPREP, radio);
    at86rf215_delay_us(h, 100);
    at86rf215_get_state(h, &state, radio);
  }

  ret = write_tx_buffer(h, radio, psdu, len);
  if (ret) {
    return ret;
  }

  struct at86rf215_radio *r = &h->priv.radios[radio];
  r->tx_complete            = 0;

  /* Data are on the buffer. Issue the TX cmd to send them */
  ret = at86rf215_set_cmd(h, AT86RF215_CMD_RF_TX, radio);
  if (ret) {
    return ret;
  }
  /* Wait for the transfer to complete */
  while (r->tx_complete == 0) {
    if (at86rf215_get_time_ms(h) > deadline) {
      return -AT86RF215_TIMEOUT;
    }
    at86rf215_delay_us(h, 100);
  }
  return AT86RF215_OK;
}

/**
 * @brief Sets the transceiver in RX mode
 *
 * @param h the device handle
 * @param radio the RF fronted
 * @param timeout_ms timeout in milisceconds
 * @return 0 on success or negative error code
 */
int
at86rf215_rx(struct at86rf215 *h, at86rf215_radio_t radio, size_t timeout_ms)
{
  size_t deadline = at86rf215_get_time_ms(h) + timeout_ms;
  int    ret      = supports_rf(h, radio);
  if (ret) {
    return ret;
  }

  /*
   * In order to transmit the frame, the corresponding baseband core should
   * be enabled
   */
  if (h->priv.chpm == AT86RF215_RF_MODE_RF) {
    return -AT86RF215_INVAL_CHPM;
  }
  if (radio == AT86RF215_RF09) {
    if (h->priv.chpm == AT86RF215_RF_MODE_BBRF09) {
      return -AT86RF215_INVAL_CHPM;
    }
  } else {
    if (h->priv.chpm == AT86RF215_RF_MODE_BBRF24) {
      return -AT86RF215_INVAL_CHPM;
    }
  }

  at86rf215_rf_state_t state;
  ret = at86rf215_get_state(h, &state, radio);
  if (ret) {
    return ret;
  }

  while (state != AT86RF215_STATE_RF_TRXOFF &&
         state != AT86RF215_STATE_RF_TXPREP) {
    at86rf215_delay_us(h, 100);
    if (at86rf215_get_time_ms(h) > deadline) {
      return -AT86RF215_TIMEOUT;
    }

    /*
     * If the radio was in the TRXOFF state request again the TXPREP state
     */
    if (state == AT86RF215_STATE_RF_TRXOFF) {
      at86rf215_set_cmd(h, AT86RF215_CMD_RF_TXPREP, radio);
      at86rf215_delay_us(h, 100);
    }
    at86rf215_get_state(h, &state, radio);
  }

  at86rf215_delay_us(h, 100);

  /* Go to RX state*/
  while (state != AT86RF215_STATE_RF_RX) {
    if (at86rf215_get_time_ms(h) > deadline) {
      return -AT86RF215_TIMEOUT;
    }
    at86rf215_set_cmd(h, AT86RF215_CMD_RF_RX, radio);
    at86rf215_delay_us(h, 100);
    at86rf215_get_state(h, &state, radio);
  }
  return AT86RF215_OK;
}

//------------------------------------------------------------------------------
// End of definition of radio function for transmission and receiving
//------------------------------------------------------------------------------


//-------------------------------------------------------------
//devicetree handling: MacroBatics


//define rf215 api
static const struct rf215_api rf215_api_funcs = {
    .get = rf215_state_get,
    //define rest here
};
