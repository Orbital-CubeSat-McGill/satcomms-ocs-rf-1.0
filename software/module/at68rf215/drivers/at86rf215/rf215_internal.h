#ifndef ZEPHYR_DRIVERS_RF215_INTERNAL_H
#define ZEPHYR_DRIVERS_RF215_INTERNAL_H

#include <errno.h>

#include "rf215.h"


#define RADIO_NODE DT_NODELABEL(at86rf215)

//structs definitions
typedef enum {
    RF09_EVT_WAKEUP  = BIT(0),
    RF09_EVT_TRXRDY = BIT(1),
    RF09_EVT_ERROR   = BIT(2),
    RF24_EVT_WAKEUP  = BIT(3),
    RF24_EVT_TRXRDY = BIT(4),
    RF24_EVT_ERROR   = BIT(5),
    BBC0_EVT_RXFS    = BIT(6),
    BBC0_EVT_RXFE    = BIT(7),
    BBC0_EVT_TXFE    = BIT(8),
    BBC1_EVT_RXFS    = BIT(9),
    BBC1_EVT_RXFE    = BIT(10),
    BBC1_EVT_TXFE    = BIT(11),
}rf215_evt;

// RF09, RF24, BBC0 and BBC1 status byte bits
#define RF_WAKEUP BIT(0)
#define RF_TRXRDY BIT(1)
#define RF_TRXERR BIT(4)

#define BB_RXFS BIT(0)
#define BB_RXFE BIT(1)
#define BB_TXFE BIT(4)


// TODO BBC0 and BBC1 status byte bits


//Enums from libre space project
//Comment out all enums until they are used
/**
 * Maximum PDU size that the frame buffer can hold
 */
#define AT86RF215_MAX_PDU (2047)

/**
 * AT86RF215 driver error codes
 */
typedef enum
{
  RF215_OK = 0,        //!< No error
  RF215_INVAL_PARAM   = -EINVAL,    //!< Invalid parameter
  RF215_NO_INIT       = -ENODEV,    //!< The IC has not been initialized
  RF215_NOT_IMPL      = -ENOSYS,    //!< The requested configuration is not implemented
  RF215_INVAL_VAL     = -ERANGE,    //!< Invalid value
  RF215_UNKNOWN_IC    = -ENXIO,     //!< The IC is not valid
  RF215_NOT_SUPPORTED = -ENOTSUP,   //!< The requested configuration/mode is not supported
  RF215_INVAL_CONF    = -EINVAL,    //!< The requested configuration is invalid
  RF215_INVAL_CHPM    = -EPERM,     //!< Invalid chip mode for the requested operation
  RF215_TIMEOUT       = -ETIMEDOUT, //!< A timeout event occurred
  RF215_PLL_UNLOCK    = -EIO        //!< The PLL lock error occurred
} rf215_error_t;

/**
 * Device Family
*/
typedef enum
{
  RF215   = 0x34, //!< Dual band transceiver and I/Q radio
  RF215IQ = 0x35, //!< I/Q radio
  RF215M  = 0x36  //!< Sub-1GHz Transceiver and I/Q radio
} at86rf215_family_t;

//Mostly configuration enums here

/**
 * CLKO pad driver strength
 */
typedef enum
{
  RF215_RF_DRVCLKO2 = 0, //!< 2 mA
  RF215_RF_DRVCLKO4,     //!< 4 mA
  RF215_RF_DRVCLKO6,     //!< 6 mA
  RF215_RF_DRVCLKO8      //!< 8 mA
} rf215_rf_clko_drv_t;

/**
 * CLKO output selection
 */
typedef enum
{
  RF215_RF_CLKO_OFF = 0,
  RF215_RF_CLKO_26_MHZ,
  RF215_RF_CLKO_32_MHZ,
  RF215_RF_CLKO_16_MHZ,
  RF215_RF_CLKO_8_MHZ,
  RF215_RF_CLKO_4_MHZ,
  RF215_RF_CLKO_2_MHZ,
  RF215_RF_CLKO_1_MHZ,
} at86rf215_rf_clko_os_t;

/**
 * External Frontend Control
 */
typedef enum
{
  RF215_RF_FEMODE0 = 0,
  RF215_RF_FEMODE1,
  RF215_RF_FEMODE2,
  RF215_RF_FEMODE3,
} at86rf215_external_rffe_control_t;

typedef enum
{
  STATE_RF_TRXOFF     = 0x2,
  STATE_RF_TXPREP     = 0x3,
  STATE_RF_TX         = 0x4,
  STATE_RF_RX         = 0x5,
  STATE_RF_TRANSITION = 0x6,
  STATE_RF_RESET      = 0x7
} rf215_state_t;

/**
 * Radio command affecting the FSM
 */
typedef enum
{
  CMD_NOP    = 0x0, //!< AT86RF215_CMD_RF_NOP
  CMD_SLEEP  = 0x1, //!< AT86RF215_CMD_RF_SLEEP
  CMD_TRXOFF = 0x2, //!< AT86RF215_CMD_RF_TRXOFF
  CMD_TXPREP = 0x3, //!< AT86RF215_CMD_RF_TXPREP
  CMD_TX     = 0x4, //!< AT86RF215_CMD_RF_TX
  CMD_RX     = 0x5, //!< AT86RF215_CMD_RF_RX
  CMD_RESET  = 0x7  //!< AT86RF215_CMD_RF_RESET
} rf_cmd_t;

/**
 * Chip mode
 */
typedef enum
{
  RF215_MODE_BBRF =
      0x0, //!< RF enabled, baseband (BBC0, BBC1) enabled, I/Q IF disabled
  RF215_MODE_RF =
      0x1, //!< RF enabled, baseband (BBC0, BBC1) disabled, I/Q IF enabled
  RF215_MODE_BBRF09 =
      0x4, //!< RF enabled, baseband (BBC0) disabled and (BBC1) enabled, I/Q IF
           //!< for sub-GHz Transceiver enabled
  RF215_MODE_BBRF24 =
      0x5 //!< RF enabled, baseband (BBC1) disabled and (BBC0) enabled, I/Q IF
          //!< for 2.4GHz Transceiver enabled
} rf215_chpm_t;

/**
 * Ramp up/down time of the power amplifier
 */
typedef enum
{
  RF215_RF_PARAMP4U  = 0x0, //!< 4 us
  RF215_RF_PARAMP8U  = 0x1, //!< 8 us
  RF215_RF_PARAMP16U = 0x2, //!< 16 us
  RF215_RF_PARAMP32U = 0x3, //!< 32 us
} rf215_paramp_t;

/**
 * Low pass filter configuration
 */
typedef enum
{
  RF215_RF_FLC80KHZ   = 0x0,
  RF215_RF_FLC100KHZ  = 0x1,
  RF215_RF_FLC125KHZ  = 0x2,
  RF215_RF_FLC160KHZ  = 0x3,
  RF215_RF_FLC200KHZ  = 0x4,
  RF215_RF_FLC250KHZ  = 0x5,
  RF215_RF_FLC315KHZ  = 0x6,
  RF215_RF_FLC400KHZ  = 0x7,
  RF215_RF_FLC500KHZ  = 0x8,
  RF215_RF_FLC624KHZ  = 0x9,
  RF215_RF_FLC800KHZ  = 0xA,
  RF215_RF_FLC1000KHZ = 0xB
} rf215_lpfcut_t;

typedef enum
{
  RF215_RF_BW160KHZ_IF250KHZ   = 0,
  RF215_RF_BW200KHZ_IF250KHZ   = 0x1,
  RF215_RF_BW250KHZ_IF250KHZ   = 0x2,
  RF215_RF_BW320KHZ_IF500KHZ   = 0x3,
  RF215_RF_BW400KHZ_IF500KHZ   = 0x4,
  RF215_RF_BW500KHZ_IF500KHZ   = 0x5,
  RF215_RF_BW630KHZ_IF1000KHZ  = 0x6,
  RF215_RF_BW800KHZ_IF1000KHZ  = 0x7,
  RF215_RF_BW1000KHZ_IF1000KHZ = 0x8,
  RF215_RF_BW1250KHZ_IF2000KHZ = 0x9,
  RF215_RF_BW1600KHZ_IF2000KHZ = 0xA,
  RF215_RF_BW2000KHZ_IF2000KHZ = 0xB
} rf215_rx_bw_t;

/**
 * Sampling rate configuration
 */
typedef enum
{
  RF215_SR_4000KHZ = 0x1,
  RF215_SR_2000KHZ = 0x2,
  RF215_SR_1333KHZ = 0x3,
  RF215_SR_1000KHZ = 0x4,
  RF215_SR_800KHZ  = 0x5,
  RF215_SR_666KHZ  = 0x6,
  RF215_SR_500KHZ  = 0x8,
  RF215_SR_400KHZ  = 0xA,
} rf215_sr_t;

/**
 * @brief Relative cut-off frequency, where 1.0 refers to half the sample
 * frequency Fs.
 *
 */
typedef enum
{
  RF215_RCUT_25FS2  = 0x0, //!< 0.25 *fs/2
  RF215_RCUT_37FS2  = 0x1, //!< 0.375 *fs/2
  RF215_RCUT_50FS2  = 0x2, //!< 0.50 *fs/2
  RF215_RCUT_75FS2  = 0x3, //!< 0.75 *fs/2
  RF215_RCUT_100FS2 = 0x4  //!< 1.00 *fs/2
} rf215_rcut_t;

/**
 * Power amplifier current settings
 */
typedef enum
{
  RF215_PACUR_22mA_RED = 0x0, //!< current reduction by about 22mA (3dB
                              //!< reduction of max. small signal gain)
  RF215_PACUR_18mA_RED = 0x1, //!< current reduction by about 18mA (2dB
                              //!< reduction of max. small signal gain)
  RF215_PACUR_11mA_RED = 0x2, //!< current reduction by about 11mA (1dB
                              //!< reduction of max. small signal gain)
  RF215_PACUR_NO_RED = 0x3, //!< No power amplifier current reduction (max.
                            //!< transmit small signal gain)
} rf215_pacur_t;

/**
 * PLL lock status
 */
typedef enum
{
  RF215_PLL_UNLOCKED = 0, //!< PLL is not locked
  RF215_PLL_LOCKED   = 1  //!< PLL is locked
} rf215_pll_ls_t;

/**
 * Output pads (IRQ, MISO,  FEA09, FEB09, FEA24, FEB24)) driving current
 * strength
 */
typedef enum
{
  RF215_RF_DRV2 = 0, //!< 2 mA
  RF215_RF_DRV4 = 1, //!< 4 mA
  RF215_RF_DRV6 = 2, //!< 6 mA
  RF215_RF_DRV8 = 3  //!< 8 mA
} rf215_drv_t;

/**
 * LVDS driving current strength
 */
typedef enum
{
  RF215_LVDS_DRV1 = 0, //!< 1 mA
  RF215_LVDS_DRV2 = 1, //!< 2 mA
  RF215_LVDS_DRV3 = 2, //!< 3 mA
  RF215_LVDS_DRV4 = 3, //!< 4 mA
} rf215_lvds_drv_t;

/**
 * LVDS common mode voltage
 */
typedef enum
{
  RF215_LVDS_CMV150 = 0, //!< 150 mV
  RF215_LVDS_CMV200 = 1, //!< 200 mV
  RF215_LVDS_CMV250 = 2, //!< 250 mV
  RF215_LVDS_CMV300 = 3, //!< 300 mV
} rf215_lvds_cmv_t;

/**
 * The AGC target level relative to ADC full scale
 */
typedef enum
{
  RF215_TGT_M21 = 0, //!< Target=-21dB
  RF215_TGT_M24 = 1, //!< Target=-24dB
  RF215_TGT_M27 = 2, //!< Target=-27dB
  RF215_TGT_M30 = 3, //!< Target=-30dB
  RF215_TGT_M33 = 4, //!< Target=-33dB
  RF215_TGT_M36 = 5, //!< Target=-36dB
  RF215_TGT_M39 = 6, //!< Target=-39dB
  RF215_TGT_M42 = 7, //!< Target=-42dB
} rf215_agc_tgt_t;

/**
 * AGC Average Time in Number of Samples
 */
typedef enum
{
  RF215_AVGS_8  = 0, //!< 8 samples
  RF215_AVGS_16 = 1, //!< 16 samples
  RF215_AVGS_32 = 2, //!< 32 samples
  RF215_AVGS_64 = 3, //!< 64 samples
} rf215_agc_avgs_t;

/**
 * Depending on the external frontend setup (external LNA), a specific AGC gain
 * mapping is applied considering the gain of the external LNA (about 9dB or
 * 12dB external LNA gain
 */
typedef enum
{
  RF215_AGC_MAP_INTERNAL = 0, //!< Internal AGC, no external LNA used
  RF215_AGC_MAP_9DB =
      1, //!< AGC back-off for external LNA of about 9dB gain
  RF215_AGC_MAP_12DB =
      2 //!< AGC back-off for external LNA of about 12dB gain
} rf215_agc_map_t;

/**
 * Internal power amplifier voltage
 */
typedef enum
{
  RF215_PAVC_2_0V = 0, //!< 2V
  RF215_PAVC_2_2V = 1, //!< 2.2V
  RF215_PAVC_2_4V = 2  //!< 2.4V
} rf215_pavc_t;

struct rf215_config {
    struct spi_dt_spec rf215_spi;
    struct gpio_dt_spec rf215_rst;
    struct gpio_dt_spec rf215_irq;
    const struct rf215_bb_conf *presets;
    uint8_t num_presets;
    uint8_t preset_idx;
    uint8_t id;
};

// Set struct for chip irq triggers and lock spi access
struct rf215_data {
    const struct device *dev;
    const struct rf215_bb_conf *preset;
    struct gpio_callback irq_cb;
    struct k_work irq_work;
    struct k_mutex bus_lock;   /* if other threads also touch the chip */
    struct k_event events;
    rf215_chpm_t op_mode;
    uint8_t version;        /* RF_VN, read at init */
    uint8_t num_presets;
    uint8_t  preset_idx;    /* which preset is active */
    uint16_t channel;       /* runtime */
    int8_t   tx_power;      /* runtime */
};

//Abstraction layer over SPI driver 
int rf215_blk_write(
	const struct device *dev,
	uint16_t address,
	const uint8_t *data,
	size_t length
);
int rf215_blk_read(
	const struct device *dev,
	uint16_t address,
	uint8_t *data,
	size_t length
);
int rf215_cmd(
        const struct device *dev,
        rf_cmd_t cmd, 
        rf215_radio_t radio
);

#endif /*ZEPHYR_DRIVERS_RF215_INTERNAL_H*/
