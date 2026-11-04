#define DT_DRV_COMPACT atmel,at86rf215

#include <errno.h>
#include <zephyr/logging/log.h>
#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"


LOG_MODULE_REGISTER(rf215);


//------------------------------------------------------------------------------
//forward declarations of functions passed through to the api
//------------------------------------------------------------------------------
static int rf215_conn_check(const struct device *dev); //confirm at86rf215 is connected and alive
static int rf215_set_sleep(const struct device *dev, rf215_radio_t radio, uint8_t en); //set sleep state of transceiver or trxoff
static int rf215_select_preset(const struct device *dev, rf215_radio_t radio, uint8_t idx); /* validate, lock, apply */
static int rf215_set_channel(const struct device *dev, rf215_radio_t radio, uint16_t ch); /* checked against active preset */
static int rf215_set_tx_power(const struct device *dev, rf215_radio_t radio, int8_t dbm);
static int rf215_bb_enable(const struct device *dev, rf215_radio_t radio, uint8_t en);
static int rf215_get_value(const struct device *dev, rf215_radio_t radio, get_variable var, uint8_t *value) //get a variable from get_values enum to value
static int rf215_tx_buffer(const struct device *dev, rf215_radio_t radio, const uint8_t *psdu, size_t len); // write to tx buffer
static int rf215_tx(const struct device *dev, rf215_radio_t radio, const uint8_t *psdu, size_t len, size_t timeout_ms); // flush tx buffer and transmit data
static int rf215_rx(const struct device *dev, rf215_radio_t radio, size_t timeout_ms); // set transceiver to RX mode
static int rf215_rx_frame(const struct device *dev, rf215_radio_t radio, uint8_t *psdu, size_t len); // get data from RX buffer

//------------------------------------------------------------------------------
//System Functions definition, like INIT(), RESET() and IRQ handling
//------------------------------------------------------------------------------

/**
 * @brief GPIO interrupt callback for the radio IRQ line.
 *
 * Runs in ISR context. Disables the IRQ pin interrupt (the line is level
 * triggered and would otherwise retrigger immediately) and defers the SPI
 * work to the system work queue. The interrupt is re-armed by
 * rf215_irq_work().
 *
 * @param port GPIO port that triggered the callback (unused)
 * @param cb   registered callback, used to recover the driver data
 * @param pins bitmask of pins that triggered (unused)
 */
static void rf215_irq_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    struct rf215_data *data = CONTAINER_OF(cb, struct rf215_data, irq_cb);
    const struct rf215_config *cfg = data->dev->config;

    gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_DISABLE);
    k_work_submit(&data->irq_work);
}

/**
 * @brief Translate IRQ status bytes into driver events.
 *
 * Maps the RF09, RF24, BBC0 and BBC1 IRQ status bytes onto the rf215_evt
 * bits and posts them to data->events with k_event_post(). Posting only ORs
 * bits in, so this never clears events. Consumers must clear the bits they
 * care about before starting an operation.
 *
 * @param irqs array of 4 status bytes: [0] RF09, [1] RF24, [2] BBC0, [3] BBC1
 * @param data driver instance data holding the event object
 *
 * @note When unmasking a new interrupt in rf215_reset(), add its handling
 *       here and its bit to the rf215_evt enum.
 */
static void rf215_post_events(const uint8_t *irqs, struct rf215_data *data)
{
    uint32_t events = 0;
    // Check for interrupts set in the IRQ bit mask
    // if you add more masks make sure to implement them here
    // Also make sure that the new event you implement is added to the event enum
    if (irqs[0] & RF_TRXRDY) {
        events |= RF09_EVT_TRXRDY;
    }
    if (irqs[0] & RF_TRXERR) {
        events |= RF09_EVT_ERROR;
    }

    if (irqs[1] & RF_TRXRDY) {
        events |= RF24_EVT_TRXRDY;
    }
    if (irqs[1] & RF_TRXERR) {
        events |= RF24_EVT_ERROR;
    }

    if (irqs[2] & BB_RXFS) {
        events |= BBC0_EVT_RXFS;
    }
    if (irqs[2] & BB_RXFE) {
        events |= BBC0_EVT_RXFE;
    }
    if (irqs[2] & BB_TXFE) {
        events |= BBC0_EVT_TXFE;
    }

    if (irqs[3] & BB_RXFS) {
        events |= BBC1_EVT_RXFS;
    }
    if (irqs[3] & BB_RXFE) {
        events |= BBC1_EVT_RXFE;
    }
    if (irqs[3] & BB_TXFE) {
        events |= BBC1_EVT_TXFE;
    }

    if (events != 0) {
        k_event_post(&data->events, events);
    }
}

/**
 * @brief Work handler that reads and dispatches pending radio interrupts.
 *
 * Reads the four IRQ status registers in one SPI transaction, posts the
 * resulting events, and re-arms the IRQ pin interrupt. The pin is re-armed
 * even if the SPI read fails, so a bus error is logged but does not leave
 * the interrupt disabled.
 *
 * @param work work item embedded in struct rf215_data
 */
static void rf215_irq_work(struct k_work *work)
{
    struct rf215_data *data = CONTAINER_OF(work, struct rf215_data, irq_work);
    const struct device *dev = data->dev;
    const struct rf215_config *cfg = dev->config;
    uint8_t irqs[4]; /* RF09, RF24, BBC0, BBC1 */

    int read_return = rf215_blk_read(dev, REG_RF09_IRQS, irqs, sizeof(irqs));
    if (read_return == 0) {
        rf215_post_events(irqs, data);
    } else {
        LOG_ERR("Could not read from IRQ registers, Error Code: %d\n", read_return);
    }

    gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_LEVEL_ACTIVE);
}

/**
 * @brief Initialize the driver instance.
 *
 * Sets up the driver data (mutex, work item, event object, default chip
 * mode), checks that the SPI bus and GPIOs are ready, configures the reset
 * and IRQ pins, resets the chip, registers the IRQ callback, discards any
 * stale IRQ state and finally enables the IRQ pin interrupt.
 *
 * @param dev the device handle
 * @return 0 on success, -ENODEV if a bus or GPIO is not ready or cannot be
 *         configured, or a negative error code from the GPIO or SPI layer
 *
 * @note The return value of rf215_reset() is currently ignored.
 */
static int rf215_init(const struct device *dev)
{
    const struct rf215_config *cfg = (const struct rf215_config *) dev->config;
    struct rf215_data *data = dev->data;

    const struct spi_dt_spec *rf215_spi = &cfg->rf215_spi;
    const struct gpio_dt_spec *rf215_rst = &cfg->rf215_rst;
    const struct gpio_dt_spec *rf215_irq = &cfg->rf215_irq;

    data->dev = dev;
    //define op_mode here
    data->op_mode = RF215_MODE_BBRF;
    k_mutex_init(&data->bus_lock);
    k_work_init(&data->irq_work, rf215_irq_work);
    k_event_init(&data->events);

    LOG_DBG("Initializing Radio module (instance: %u)\r\n", cfg->id);

    if (!spi_is_ready_dt(rf215_spi) ||
        !gpio_is_ready_dt(rf215_rst) ||
        !gpio_is_ready_dt(rf215_irq)) {
        return -ENODEV;
    }

    int result = gpio_pin_configure_dt(
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
    result = rf215_reset(dev);
    if (result < 0) {
        return result;
    }

    gpio_init_callback(&data->irq_cb, rf215_irq_isr, BIT(cfg->rf215_irq.pin));
    result = gpio_add_callback(cfg->rf215_irq.port, &data->irq_cb);
    if (result) {
        return result;
    }

    //checks and discards stale IRQ state
    uint8_t stale_irqs[4];
    int irq_result = rf215_blk_read(dev, REG_RF09_IRQS, stale_irqs, sizeof(stale_irqs));
    if (irq_result != 0) {
        LOG_ERR("Could not read from IRQ registers. Error Code: %d\n", irq_result);
        return irq_result;
    }

    return gpio_pin_interrupt_configure_dt(&cfg->rf215_irq, GPIO_INT_LEVEL_ACTIVE);
}

/**
 * @brief Hardware-reset the chip and apply the baseline configuration.
 *
 * Pulses the reset pin (assert, 10 ms, release, 10 ms), then writes the chip
 * mode (data->op_mode) to IQIFC1 and unmasks the interrupts the driver
 * handles (RXFS, RXFE and TXFE on both baseband cores, TRXRDY and TRXERR on
 * both radios). All previous register state is lost.
 *
 * @param dev the device handle
 * @return 0 on success or a negative error code from the GPIO or SPI layer
 *
 * @note The reset pin polarity depends on the devicetree flags: the
 *       assert/release sequence assumes the pin is marked active-low.
 */
static int rf215_reset(const struct device *dev)
{
    const struct rf215_config *cfg = (const struct rf215_config *) dev->config;
    struct rf215_data *data = dev->data;
    const struct gpio_dt_spec *rf215_gpio = &cfg->rf215_rst;

    int result = gpio_pin_set_dt(rf215_gpio, 1);
    if (result != 0) {
        return result;
    }

    k_msleep(10);

    result = gpio_pin_set_dt(rf215_gpio, 0);
    if (result != 0) {
        return result;
    }

    k_msleep(10);

    // set all register configurations here
    // set operating mode to baseband for both transceivers
    // if you add more masks make sure to implement them in post_event function
    uint8_t mode = data->op_mode;
    result = rf215_blk_write(dev, REG_RF_IQIFC1, &mode, 1);
    if (result != 0) {
        return result;
    }

    // IRQ mask setup (when writing to this register you enable these types of interrupts)
    // section 5.3.2.1
    // IRQ masks for sub 1GHz BaseBand core
    uint8_t val = BIT(0) | BIT(1) | BIT(4);
    result = rf215_blk_write(dev, REG_BBC0_IRQM, &val, 1);
    if (result != 0) {
        return result;
    }
    // IRQ masks for 2.4GHz BaseBand core
    result = rf215_blk_write(dev, REG_BBC1_IRQM, &val, 1);
    if (result != 0) {
        return result;
    }
    // IRQ masks for sub 1GHz Transceiver
    result = rf215_blk_write(dev, REG_RF09_IRQM, &val, 1);
    if (result != 0) {
        return result;
    }
    // IRQ masks for 2.4GHz Transceiver
    result = rf215_blk_write(dev, REG_RF24_IRQM, &val, 1);
    if (result != 0) {
        return result;
    }

    return 0;
}

//------------------------------------------------------------------------------
// General purpose method
//------------------------------------------------------------------------------

/**
 * @brief Read the current state of a transceiver.
 *
 * Reads the radio's STATE register and validates that the value is a known
 * state.
 *
 * @param dev   the device handle
 * @param state output: the current state
 * @param radio the RF frontend (RF09 or RF24)
 * @return RF215_OK on success, a negative error code if an argument is
 *         invalid, the SPI read fails or the register holds an unknown value
 */
static int rf215_state_get(const struct device *dev, rf215_state_t *state, rf215_radio_t radio)
{
    if (!state) {
        return RF215_INVAL_PARAM;
    }
    uint8_t val = 0;
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
    int ret = rf215_blk_read(dev, reg, &val, 1);
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
            *state = (rf215_state_t) val;
            break;
        default:
            return -EINVAL;
    }
    return RF215_OK;
}

/**
 * Get the PLL lock status for the corresponding RF frontend
 * @param h the device handle
 * @param status pointer to store the lock status result
 * @param radio the RF frontend
 * @return 0 on success or negative error code
 */
static int rf215_get_pll_ls(const struct device *dev, uint8_t *state, rf215_radio_t radio)
{
  uint16_t reg = 0;
  if (radio == RF09) {
    reg = REG_RF09_PLL;
  } else {
    reg = REG_RF24_PLL;
  }
  uint8_t val = 0;
  int ret = rf215_blk_read(dev, reg, &val, 1);
  if (ret) {
    return ret;
  }
  *state = (rf215_pll_ls_t)((val >> 1) & 0x1);
  return AT86RF215_OK;
}


//------------------------------------------------------------------------------
//Public functions
//------------------------------------------------------------------------------

/**
 * @brief Check that a supported AT86RF215 is connected.
 *
 * Reads the part number register and compares it against the known device
 * family values (RF215, RF215IQ, RF215M).
 *
 * @param dev the device handle
 * @return RF215_OK if a supported chip answered, a negative error code if
 *         the part number is not recognized or the SPI read fails
 */
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
            return RF215_UNKNOWN_IC;
    }
}

/**
 * @brief Put a transceiver to sleep, or wake it to TRXOFF.
 *
 * @param dev   the device handle
 * @param radio the RF frontend
 * @param en    nonzero issues the SLEEP command, zero issues TRXOFF
 * @return 0 on success or a negative error code from rf215_cmd()
 *
 * @note This only sends the command. It does not wait for the state change.
 */
static int rf215_set_sleep(const struct device *dev, rf215_radio_t radio, uint8_t en)
// Set Transceiver to sleep
// wrapper to call privbate function
{
    if (en) {
        return rf215_cmd(dev, CMD_SLEEP, radio);
    } else {
        return rf215_cmd(dev, CMD_TRXOFF, radio);
    }
}

static int rf215_get_value(const struct device *dev, rf215_radio_t radio, get_variable var, uint8_t *value)
{
    int reg;
    if (radio == RF09){
	switch (var){
	case RSSI:
	    reg = REG_RF09_RSSI;
		break;
	case EDV:
		reg = REG_RF09_EDV;
		break;
	case AGC:
		reg = REG_RF09_AGCS;
		break;
	default:
		return -RF215_INVAL_VAL;
	}
    }else{
	switch (var){
	case RSSI:
	    reg = REG_RF24_RSSI;
		break;
	case EDV:
		reg = REG_RF24_EDV;
		break;
	case AGC:
		reg = REG_RF24_AGCS;
		break;
	default:
		return -RF215_INVAL_VAL;
	}
    }
    return rf215_blk_write(dev, reg, &value, 1);
}


//------------------------------------------------------------------------------
// Start of definition of radio function for transmission and receiving
//------------------------------------------------------------------------------

/**
 * @brief Load a frame into the TX frame buffer.
 *
 * Writes the frame length (11 bits, little endian) to TXFLL/TXFLH, then the
 * payload to the frame buffer of the baseband core that serves the radio
 * (BBC0 for RF09, BBC1 for RF24). This does not transmit; rf215_tx() issues
 * the TX command.
 *
 * @param dev   the device handle
 * @param radio the RF frontend (RF09 or RF24)
 * @param psdu  frame bytes to load
 * @param len   number of bytes, 1 to AT86RF215_MAX_PDU
 * @return 0 on success, RF215_INVAL_PARAM for a bad argument, or a negative
 *         error code from the SPI layer
 *
 * @note Check whether len must include the FCS bytes for the configured PHY.
 */
static int rf215_tx_buffer(const struct device *dev, rf215_radio_t radio,
                           const uint8_t *psdu, size_t len)
{
    if (!dev || !psdu || len == 0 || len > AT86RF215_MAX_PDU ||
        (radio != RF09 && radio != RF24)) {
        return RF215_INVAL_PARAM;
    }

    const uint16_t txfl = (radio == RF09) ? REG_BBC0_TXFLL : REG_BBC1_TXFLL;
    const uint16_t fbtx = (radio == RF09) ? REG_BBC0_FBTXS : REG_BBC1_FBTXS;

    uint8_t lenbuf[2] = { len & 0xFF, (len >> 8) & 0x07 };

    int ret = rf215_blk_write(dev, txfl, lenbuf, sizeof(lenbuf));
    if (ret < 0) {
        return ret;
    }

    return rf215_blk_write(dev, fbtx, psdu, len);
}

/**
 * @brief Transmit a frame and wait for it to finish.
 *
 * Moves the radio to TXPREP if needed (waiting for TRXRDY), loads the frame
 * with rf215_tx_buffer(), issues the TX command and waits for the baseband
 * TXFE event. Pending events for this radio are cleared before each command
 * so stale events cannot satisfy a wait. The timeout covers the whole call.
 *
 * @note The baseband core serving the radio must be enabled in the chip mode.
 * @note After a successful transmission the radio is left in TXPREP. Call
 *       rf215_rx() to return to receiving.
 *
 * @param dev        the device handle
 * @param radio      the RF frontend (RF09 or RF24)
 * @param psdu       the data to send
 * @param len        the number of bytes to send, 1 to AT86RF215_MAX_PDU
 * @param timeout_ms time that can pass before timeout
 * @return RF215_OK on success, RF215_INVAL_PARAM for a bad argument,
 *         -EBUSY if a transmission is already running, RF215_TIMEOUT if the
 *         radio does not become ready or the frame does not finish in time,
 *         RF215_INVAL_CHPM if the chip reports a transceiver error, or a
 *         negative error code from the SPI layer
 */
static int rf215_tx(const struct device *dev, rf215_radio_t radio,
                    const uint8_t *psdu, size_t len, size_t timeout_ms)
{
    if (!dev || !psdu || len == 0 || len > AT86RF215_MAX_PDU ||
        (radio != RF09 && radio != RF24)) {
        return RF215_INVAL_PARAM;
    }

    struct rf215_data *data = dev->data;
    k_timepoint_t end = sys_timepoint_calc(K_MSEC(timeout_ms));
    rf215_state_t state;

    const uint32_t rdy  = (radio == RF09) ? RF09_EVT_TRXRDY : RF24_EVT_TRXRDY;
    const uint32_t err  = (radio == RF09) ? RF09_EVT_ERROR  : RF24_EVT_ERROR;
    const uint32_t txfe = (radio == RF09) ? BBC0_EVT_TXFE   : BBC1_EVT_TXFE;

    int ret = rf215_state_get(dev, &state, radio);
    if (ret < 0) {
        return ret;
    }
    if (state == STATE_RF_TX) {
        return -EBUSY;              /* a transmission is already running */
    }

    /* TX can only be started from TXPREP */
    if (state != STATE_RF_TXPREP) {
        k_event_clear(&data->events, rdy | err);

        ret = rf215_cmd(dev, CMD_TXPREP, radio);
        if (ret < 0) {
            return ret;
        }

        uint32_t received = k_event_wait(&data->events, rdy | err, false,
                                         sys_timepoint_timeout(end));
        if (received & err) {
            return RF215_INVAL_CHPM;
        }
        if (!(received & rdy)) {
            return RF215_TIMEOUT;
        }
    }

    ret = rf215_tx_buffer(dev, radio, psdu, len);
    if (ret < 0) {
        return ret;
    }

    /* Clear right before the command so an old TXFE can't satisfy the wait */
    k_event_clear(&data->events, txfe | err);

    ret = rf215_cmd(dev, CMD_TX, radio);
    if (ret < 0) {
        return ret;
    }

    uint32_t received = k_event_wait(&data->events, txfe | err, false,
                                     sys_timepoint_timeout(end));
    if (received & err) {
        return RF215_INVAL_CHPM;
    }
    if (!(received & txfe)) {
        return RF215_TIMEOUT;
    }

    return RF215_OK;
}

/**
 * @brief Sets the transceiver in RX mode
 *
 * Reads the current state and returns immediately if the radio is already
 * in RX. Otherwise moves it to TXPREP if needed (waiting for TRXRDY), since
 * RX can only be entered from TXPREP, then issues the RX command and
 * confirms with a single state read.
 *
 * @param dev the device handle
 * @param radio the RF frontend
 * @param timeout_ms timeout in milliseconds
 * @return RF215_OK on success, RF215_INVAL_PARAM for a bad argument,
 *         RF215_TIMEOUT if the radio does not reach TXPREP in time,
 *         RF215_INVAL_CHPM if the chip reports a transceiver error,
 *         RF215_INVAL_CONF if the radio is not in RX after the command, or
 *         a negative error code from the SPI layer
 */
static int rf215_rx(const struct device *dev, rf215_radio_t radio, size_t timeout_ms)
{
    if ((radio != RF09 && radio != RF24) || !dev) {
        return RF215_INVAL_PARAM;
    }

    k_timepoint_t end = sys_timepoint_calc(K_MSEC(timeout_ms));
    rf215_state_t state;

    const uint32_t rdy = (radio == RF09) ? RF09_EVT_TRXRDY : RF24_EVT_TRXRDY;
    const uint32_t err = (radio == RF09) ? RF09_EVT_ERROR  : RF24_EVT_ERROR;
    struct rf215_data *data = dev->data;

    int ret = rf215_state_get(dev, &state, radio);
    if (ret < 0) {
        return ret;
    }
    if (state == STATE_RF_RX) {
        return RF215_OK;
    }

    // RX can only be reached if RF215 is in the TXPREP state
    if (state != STATE_RF_TXPREP) {
        k_event_clear(&data->events, rdy | err);

        ret = rf215_cmd(dev, CMD_TXPREP, radio);
        if (ret < 0) {
            return ret;
        }

        uint32_t received = k_event_wait(&data->events, rdy | err, false,
                                         sys_timepoint_timeout(end));
        if (received & err) {
            return RF215_INVAL_CHPM;
        }
        if (!(received & rdy)) {
            return RF215_TIMEOUT;
        }
    }

    ret = rf215_cmd(dev, CMD_RX, radio);
    if (ret < 0) {
        return ret;
    }

    ret = rf215_state_get(dev, &state, radio);
    if (ret < 0) {
        return ret;
    }

    return (state == STATE_RF_RX) ? RF215_OK : RF215_INVAL_CONF;
}

/**
 * @brief Receives a (possibly) received frame from the frame buffer
 *
 * @note This function transfers only the contents of the frame buffer. It does
 * not check for their validity, wait for a frame, or determine its length.
 * The caller must know how many bytes to read (see RXFLL/RXFLH).
 *
 * @param dev the device handle
 * @param radio the RF frontend
 * @param psdu buffer to store the received data, at least len bytes
 * @param len the number of bytes to read from the frame buffer
 * @return 0 on success, RF215_INVAL_PARAM for a bad argument, or a negative
 *         error code from the SPI layer
 */
static int rf215_rx_frame(const struct device *dev, rf215_radio_t radio,
                          uint8_t *psdu, size_t len)
{
    if (!dev || !psdu || (radio != RF09 && radio != RF24)) {
        return RF215_INVAL_PARAM;
    }

    //Frame buffer
    const uint16_t fbrx = (radio == RF09) ? REG_BBC0_FBRXS : REG_BBC1_FBRXS;

    return rf215_blk_read(dev, fbrx, psdu, len);
}

//------------------------------------------------------------------------------
// End of definition of radio function for transmission and receiving
//------------------------------------------------------------------------------


//-------------------------------------------------------------
//devicetree handling: MacroBatics


/**
 * @brief Driver API table exposed through the device.
 *
 * Currently only .get is populated (rf215_state_get). The remaining
 * functions still need to be added.
 */
static const struct rf215_api rf215_api_funcs = {
    .get = rf215_state_get,
    //define rest here
};
