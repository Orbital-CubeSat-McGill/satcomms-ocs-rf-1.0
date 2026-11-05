#define DT_DRV_COMPAT atmel_at86rf215

#include <errno.h>
#include <zephyr/logging/log.h>
#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"
#include "rf215_presets.h"

LOG_MODULE_REGISTER(rf215);


static int rf215_reset(const struct device *dev);

//------------------------------------------------------------------------------
//forward declarations of functions passed through to the api
//------------------------------------------------------------------------------
static int rf215_conn_check(const struct device *dev, uint8_t *part); //confirm at86rf215 is connected and alive, part gets the raw part number
static int rf215_set_sleep(const struct device *dev, rf215_radio_t radio, uint8_t en); //set sleep state of transceiver or trxoff
static int rf215_select_preset(const struct device *dev, rf215_radio_t radio, uint8_t idx); /* validate, lock, apply */
static int rf215_set_channel(const struct device *dev, rf215_radio_t radio, uint16_t ch); /* checked against active preset */
static int rf215_set_tx_power(const struct device *dev, rf215_radio_t radio, uint8_t txpwr); /* raw TXPWR register step, 0..31 */
static int rf215_bb_enable(const struct device *dev, rf215_radio_t radio, uint8_t en);
static int rf215_get_value(const struct device *dev, rf215_radio_t radio, get_variable var, uint8_t *value); //get a variable from get_variable enum to value
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
    data->num_presets = cfg->num_presets;
    data->preset = cfg->presets; 
    data->preset_idx = 0;   // default index [0]
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

    result = rf215_select_preset(dev, RF09, data->preset_idx);
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
  return RF215_OK;
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
 * @param dev  the device handle
 * @param part output: the raw part number. Also set when it is not recognized,
 *             so the caller can see what actually came back over the bus
 * @return RF215_OK if a supported chip answered, RF215_INVAL_PARAM if part is
 *         NULL, or a negative error code if the part number is not recognized
 *         or the SPI read fails
 */
static int rf215_conn_check(const struct device *dev, uint8_t *part)
{
    if (!part) {
        return RF215_INVAL_PARAM;
    }
    int ret = rf215_blk_read(dev, REG_RF_PN, part, 1);
    if (ret) {
        return ret;
    }
    switch (*part) {
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
		return RF215_INVAL_VAL;
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
		return RF215_INVAL_VAL;
	}
    }
    return rf215_blk_read(dev, reg, value, 1);
}

//------------------------------------------------------------------------------
// Start of definition of parametrization functions,
//------------------------------------------------------------------------------

/**
 * @brief Apply one MR-FSK baseband preset to a radio.
 *
 * Picks presets[idx], packs it into BBC/FSK registers for RF09 (BBC0) or
 * RF24 (BBC1), writes them over SPI, then remembers the active preset.
 * Does not enable the baseband (see rf215_bb_enable) and does not set
 * channel or TX power.
 */
static int rf215_select_preset(const struct device *dev, rf215_radio_t radio,
			       uint8_t idx)
{
	if (!dev || (radio != RF09 && radio != RF24)) {
		return RF215_INVAL_PARAM;
	}

	const struct rf215_config *cfg = dev->config;
	struct rf215_data *data = dev->data;

	if (!cfg->presets || idx >= data->num_presets) {
		return RF215_INVAL_PARAM;
	}

	const struct rf215_bb_conf *bb = &cfg->presets[idx];
	const struct rf215_mrfsk_conf *fsk = &bb->fsk;

	const uint16_t reg_pc = (radio == RF09) ? REG_BBC0_PC : REG_BBC1_PC;
	const uint16_t reg_fskc0 =
		(radio == RF09) ? REG_BBC0_FSKC0 : REG_BBC1_FSKC0;
	const uint16_t reg_rrxfl =
		(radio == RF09) ? REG_BBC0_FSKRRXFLL : REG_BBC1_FSKRRXFLL;
	const uint16_t reg_fskdm =
		(radio == RF09) ? REG_BBC0_FSKDM : REG_BBC1_FSKDM;

    // byte packing the parameters into the registers

	/* BBCn_PC: CTX FCSFE TXAFCS FCST PT (BBEN left 0) */
	uint8_t pc = (bb->ctx << 7) | (bb->fcsfe << 6) | (bb->txafcs << 4) |
		     ((bb->fcst & 1) << 3) | (bb->pt & 0x3);

	/* FSKC0..FSKPHRTX are consecutive */
	uint8_t fsk_regs[11];

	fsk_regs[0] = ((fsk->bt & 0x3) << 6) | ((fsk->midxs & 0x3) << 4) |
		      ((fsk->midx & 0x7) << 1) | (fsk->mord & 0x1);
	fsk_regs[1] = (((fsk->preamble_length >> 8) & 0x3) << 6) |
		      ((fsk->fi & 0x1) << 5) | (fsk->srate & 0xF);
	fsk_regs[2] = ((fsk->pdtm & 0x1) << 7) | ((fsk->rxo & 0x3) << 5) |
		      ((fsk->rxpto & 0x1) << 4) | ((fsk->mse & 0x1) << 3) |
		      ((fsk->pri & 0x1) << 2) | ((fsk->fecs & 0x1) << 1) |
		      (fsk->fecie & 0x1);
	fsk_regs[3] = ((fsk->sfd_threshold & 0xF) << 4) |
		      (fsk->preamble_threshold & 0xF);
	fsk_regs[4] = ((fsk->sfdq & 0x1) << 6) | ((fsk->sfd32 & 0x1) << 5) |
		      ((fsk->rawrbit & 0x1) << 4) | ((fsk->csfd1 & 0x3) << 2) |
		      (fsk->csfd0 & 0x3);
	fsk_regs[5] = fsk->preamble_length & 0xFF; /* FSKPLL */
	fsk_regs[6] = fsk->sfd0 & 0xFF;
	fsk_regs[7] = (fsk->sfd0 >> 8) & 0xFF;
	fsk_regs[8] = fsk->sfd1 & 0xFF;
	fsk_regs[9] = (fsk->sfd1 >> 8) & 0xFF;
	fsk_regs[10] = ((fsk->sfd & 0x1) << 3) | ((fsk->dw & 0x1) << 2) |
		       ((fsk->rb2 & 0x1) << 1) | (fsk->rb1 & 0x1);

	uint8_t rrxfl[2] = {
		fsk->fskrrxf & 0xFF,
		(fsk->fskrrxf >> 8) & 0x07,
	};

	/* FSKDM + FSKPE0/1/2 */
	uint8_t dm_pe[4] = {
		(fsk->dm & 0x1) | ((fsk->preemphasis & 0x1) << 1),
		fsk->preemphasis_taps & 0xFF,
		(fsk->preemphasis_taps >> 8) & 0xFF,
		(fsk->preemphasis_taps >> 16) & 0xFF,
	};

	int ret = rf215_blk_write(dev, reg_pc, &pc, 1);
	if (ret < 0) {
		return ret;
	}

	ret = rf215_blk_write(dev, reg_fskc0, fsk_regs, sizeof(fsk_regs));
	if (ret < 0) {
		return ret;
	}

	ret = rf215_blk_write(dev, reg_rrxfl, rrxfl, sizeof(rrxfl));
	if (ret < 0) {
		return ret;
	}

	ret = rf215_blk_write(dev, reg_fskdm, dm_pe, sizeof(dm_pe));
	if (ret < 0) {
		return ret;
	}

	data->preset = bb;
	data->preset_idx = idx;
	return RF215_OK;
}

static int rf215_set_channel(const struct device *dev, rf215_radio_t radio, uint16_t ch)
{
  if (radio != RF09 && radio != RF24) {
    return RF215_INVAL_PARAM;
  }
  if (ch > 0x1F) {
    return RF215_INVAL_PARAM;
  }
  /* The radio should be in IEEE channel mode to set a channel
   * since we dont have preset that are not in IEEE mode we can skip this?
  if (h->priv.radios[radio].cm != AT86RF215_CM_IEEE) {
    return -RF215_INVAL_CONF;
  }*/
  const uint16_t reg_cnl = (radio == RF09) ? REG_RF09_CNL : REG_RF24_CNL;
  const uint16_t reg_cnm = (radio == RF09) ? REG_RF09_CNM : REG_RF24_CNM;
  uint8_t cnl = ch & 0xFF;
  uint8_t cnm = (RF215_CM_IEEE << 6) | ((ch >> 8) & 0x1);

  int ret = rf215_blk_write(dev, reg_cnl, &cnl, 1);
  if (ret) {
    return ret;
  }
  return rf215_blk_write(dev, reg_cnm, &cnm, 1);
}

static int rf215_set_tx_power(const struct device *dev, rf215_radio_t radio, uint8_t txpwr)
{
  if (radio != RF09 && radio != RF24) {
    return RF215_INVAL_PARAM;
  }
  if (txpwr > 31) {
    txpwr = 31;
  }

  const uint16_t reg = (radio == RF09) ? REG_RF09_PAC : REG_RF24_PAC;
  uint8_t val = (RF215_PACUR_NO_RED << 5) | txpwr;

  return rf215_blk_write(dev, reg, &val, 1);
}

static int rf215_bb_enable(const struct device *dev, rf215_radio_t radio, uint8_t en)
{
  if (radio != RF09 && radio != RF24) {
    return RF215_INVAL_PARAM;
  }

  const uint16_t reg = (radio == RF09) ? REG_BBC0_PC : REG_BBC1_PC;
  uint8_t val = 0;

  /* BBEN is bit 2 of BBCn_PC, keep the rest of the register as the preset set it */
  int ret = rf215_blk_read(dev, reg, &val, 1);
  if (ret) {
    return ret;
  }
  if (en) {
    val |= BIT(2);
  } else {
    val &= ~BIT(2);
  }
  return rf215_blk_write(dev, reg, &val, 1);
}

//------------------------------------------------------------------------------
// End of definition of parametrization functions
//------------------------------------------------------------------------------

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
    .conn_check = rf215_conn_check,
    .set_sleep = rf215_set_sleep,
    .select_preset = rf215_select_preset,
    .set_channel = rf215_set_channel,
    .set_tx_power = rf215_set_tx_power,
    .bb_enable = rf215_bb_enable,
    .get_value = rf215_get_value,
    .tx_buffer = rf215_tx_buffer,
    .radio_tx = rf215_tx,
    .radio_rx = rf215_rx,
    .rx_frame = rf215_rx_frame,
};



//Expansion macro to define driver instance
#define RF215_DEFINE(inst)							\
    static struct rf215_data rf215_data_##inst;					\
    /*Create an instance of the config struct, populate with DT values */	\
    static const struct rf215_config rf215_config_##inst = {			\
	.rf215_spi = SPI_DT_SPEC_INST_GET(inst,					\
			SPI_WORD_SET(8) | SPI_TRANSFER_MSB),			\
	.rf215_rst = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),			\
	.rf215_irq = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),			\
	.presets = rf215_fsk_presets,						\
	.num_presets = RF215_NUM_PRESETS,					\
	.id = inst								\
    };										\
    /* Create a "device" instance from a Devicetree node identifier and */ 	\
    /* registers the init function to run during boot. */ 			\
    DEVICE_DT_INST_DEFINE(inst,							\
		     rf215_init,						\
		     NULL,							\
		     &rf215_data_##inst,					\
		     &rf215_config_##inst,					\
		     POST_KERNEL,						\
		     CONFIG_IEEE802154_AT86RF215_INIT_PRIORITY,			\
		     &rf215_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(RF215_DEFINE)
