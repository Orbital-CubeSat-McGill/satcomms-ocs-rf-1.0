#ifndef ZEPHYR_DRIVERS_RF215_INTERNAL_H
#define ZEPHYR_DRIVERS_RF215_INTERNAL_H


#define RADIO_NODE DT_NODELABEL(at86rf215)


// RF09 and RF24 status byte bits
#define RF_TRXRDY BIT(1)
#define RF_TRXERR BIT(4)

// TODO BBC0 and BBC1 status byte bits


//Initialization of module

static void rf215_irq_isr(  const struct device *port,
                            struct gpio_callback *cb, 
                            uint32_t pins);
static int rf215_init(const struct device *dev);
static int rf215_reset(const struct device *dev);

//General purpose functions
static int rf215_get_state(const struct device *dev, uint8_t *state); //determine state of transceiver


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


//Enums from gitlab project
/**
 * Maximum PDU size that the frame buffer can hold
 */
#define AT86RF215_MAX_PDU (2047)

/**
 * AT86RF215 driver error codes
 */
typedef enum
{
  AT86RF215_OK = 0,        //!< No error
  AT86RF215_INVAL_PARAM,   //!< Invalid parameter
  AT86RF215_NO_INIT,       //!< The IC has not been initialized
  AT86RF215_NOT_IMPL,      //!< The requested configuration is not implemented
  AT86RF215_INVAL_VAL,     //!< Invalid value
  AT86RF215_UNKNOWN_IC,    //!< The IC is not valid
  AT86RF215_NOT_SUPPORTED, //!< The requested configuration/mode is not
                           //!< supported
  AT86RF215_INVAL_CONF,    //!< The requested configuration is invalid
  AT86RF215_INVAL_CHPM,    //!< Invalid chip mode for the requested operation
  AT86RF215_TIMEOUT,       //!< A timeout event occured
  AT86RF215_PLL_UNLOCK     //!< The PLL lock error occured
} at86rf215_error_t;

/**
 * Device Family
 */
typedef enum
{
  AT86RF215   = 0x34, //!< Dual band transceiver and I/Q radio
  AT86RF215IQ = 0x35, //!< I/Q radio
  AT86RF215M  = 0x36  //!< Sub-1GHz Transceiver and I/Q radio
} at86rf215_family_t;

/**
 * Radio front-end
 */
typedef enum
{
  AT86RF215_RF09 = 0, //!< Sub-1 GHz radio
  AT86RF215_RF24 = 1, //!< 2.4 GHz radio
} at86rf215_radio_t;

/**
 * CLKO pad driver strength
 */
typedef enum
{
  AT86RF215_RF_DRVCLKO2 = 0, //!< 2 mA
  AT86RF215_RF_DRVCLKO4,     //!< 4 mA
  AT86RF215_RF_DRVCLKO6,     //!< 6 mA
  AT86RF215_RF_DRVCLKO8      //!< 8 mA
} at86rf215_rf_clko_drv_t;

/**
 * CLKO output selection
 */
typedef enum
{
  AT86RF215_RF_CLKO_OFF = 0,
  AT86RF215_RF_CLKO_26_MHZ,
  AT86RF215_RF_CLKO_32_MHZ,
  AT86RF215_RF_CLKO_16_MHZ,
  AT86RF215_RF_CLKO_8_MHZ,
  AT86RF215_RF_CLKO_4_MHZ,
  AT86RF215_RF_CLKO_2_MHZ,
  AT86RF215_RF_CLKO_1_MHZ,
} at86rf215_rf_clko_os_t;

/**
 * External Frontend Control
 */
typedef enum
{
  AT86RF215_RF_FEMODE0 = 0,
  AT86RF215_RF_FEMODE1,
  AT86RF215_RF_FEMODE2,
  AT86RF215_RF_FEMODE3,
} at86rf215_external_rffe_control_t;

typedef enum
{
  AT86RF215_STATE_RF_TRXOFF     = 0x2,
  AT86RF215_STATE_RF_TXPREP     = 0x3,
  AT86RF215_STATE_RF_TX         = 0x4,
  AT86RF215_STATE_RF_RX         = 0x5,
  AT86RF215_STATE_RF_TRANSITION = 0x6,
  AT86RF215_STATE_RF_RESET      = 0x7
} at86rf215_rf_state_t;

/**
 * Radio command affecting the FSM
 */
typedef enum
{
  AT86RF215_CMD_RF_NOP    = 0x0, //!< AT86RF215_CMD_RF_NOP
  AT86RF215_CMD_RF_SLEEP  = 0x1, //!< AT86RF215_CMD_RF_SLEEP
  AT86RF215_CMD_RF_TRXOFF = 0x2, //!< AT86RF215_CMD_RF_TRXOFF
  AT86RF215_CMD_RF_TXPREP = 0x3, //!< AT86RF215_CMD_RF_TXPREP
  AT86RF215_CMD_RF_TX     = 0x4, //!< AT86RF215_CMD_RF_TX
  AT86RF215_CMD_RF_RX     = 0x5, //!< AT86RF215_CMD_RF_RX
  AT86RF215_CMD_RF_RESET  = 0x7  //!< AT86RF215_CMD_RF_RESET
} at86rf215_rf_cmd_t;

/**
 * Chip mode
 */
typedef enum
{
  AT86RF215_RF_MODE_BBRF =
      0x0, //!< RF enabled, baseband (BBC0, BBC1) enabled, I/Q IF disabled
  AT86RF215_RF_MODE_RF =
      0x1, //!< RF enabled, baseband (BBC0, BBC1) disabled, I/Q IF enabled
  AT86RF215_RF_MODE_BBRF09 =
      0x4, //!< RF enabled, baseband (BBC0) disabled and (BBC1) enabled, I/Q IF
           //!< for sub-GHz Transceiver enabled
  AT86RF215_RF_MODE_BBRF24 =
      0x5 //!< RF enabled, baseband (BBC1) disabled and (BBC0) enabled, I/Q IF
          //!< for 2.4GHz Transceiver enabled
} at86rf215_chpm_t;

/**
 * Ramp up/down time of the power amplifier
 */
typedef enum
{
  AT86RF215_RF_PARAMP4U  = 0x0, //!< 4 us
  AT86RF215_RF_PARAMP8U  = 0x1, //!< 8 us
  AT86RF215_RF_PARAMP16U = 0x2, //!< 16 us
  AT86RF215_RF_PARAMP32U = 0x3, //!< 32 us
} at86rf215_paramp_t;

/**
 * Low pass filter configuration
 */
typedef enum
{
  AT86RF215_RF_FLC80KHZ   = 0x0,
  AT86RF215_RF_FLC100KHZ  = 0x1,
  AT86RF215_RF_FLC125KHZ  = 0x2,
  AT86RF215_RF_FLC160KHZ  = 0x3,
  AT86RF215_RF_FLC200KHZ  = 0x4,
  AT86RF215_RF_FLC250KHZ  = 0x5,
  AT86RF215_RF_FLC315KHZ  = 0x6,
  AT86RF215_RF_FLC400KHZ  = 0x7,
  AT86RF215_RF_FLC500KHZ  = 0x8,
  AT86RF215_RF_FLC624KHZ  = 0x9,
  AT86RF215_RF_FLC800KHZ  = 0xA,
  AT86RF215_RF_FLC1000KHZ = 0xB
} at86rf215_lpfcut_t;

typedef enum
{
  AT86RF215_RF_BW160KHZ_IF250KHZ   = 0,
  AT86RF215_RF_BW200KHZ_IF250KHZ   = 0x1,
  AT86RF215_RF_BW250KHZ_IF250KHZ   = 0x2,
  AT86RF215_RF_BW320KHZ_IF500KHZ   = 0x3,
  AT86RF215_RF_BW400KHZ_IF500KHZ   = 0x4,
  AT86RF215_RF_BW500KHZ_IF500KHZ   = 0x5,
  AT86RF215_RF_BW630KHZ_IF1000KHZ  = 0x6,
  AT86RF215_RF_BW800KHZ_IF1000KHZ  = 0x7,
  AT86RF215_RF_BW1000KHZ_IF1000KHZ = 0x8,
  AT86RF215_RF_BW1250KHZ_IF2000KHZ = 0x9,
  AT86RF215_RF_BW1600KHZ_IF2000KHZ = 0xA,
  AT86RF215_RF_BW2000KHZ_IF2000KHZ = 0xB
} at86rf215_rx_bw_t;

/**
 * Sampling rate configuration
 */
typedef enum
{
  AT86RF215_SR_4000KHZ = 0x1,
  AT86RF215_SR_2000KHZ = 0x2,
  AT86RF215_SR_1333KHZ = 0x3,
  AT86RF215_SR_1000KHZ = 0x4,
  AT86RF215_SR_800KHZ  = 0x5,
  AT86RF215_SR_666KHZ  = 0x6,
  AT86RF215_SR_500KHZ  = 0x8,
  AT86RF215_SR_400KHZ  = 0xA,
} at86rf215_sr_t;

/**
 * @brief Relative cut-off frequency, where 1.0 refers to half the sample
 * frequency Fs.
 *
 */
typedef enum
{
  AT86RF215_RCUT_25FS2  = 0x0, //!< 0.25 *fs/2
  AT86RF215_RCUT_37FS2  = 0x1, //!< 0.375 *fs/2
  AT86RF215_RCUT_50FS2  = 0x2, //!< 0.50 *fs/2
  AT86RF215_RCUT_75FS2  = 0x3, //!< 0.75 *fs/2
  AT86RF215_RCUT_100FS2 = 0x4  //!< 1.00 *fs/2
} at86rf215_rcut_t;

/**
 * Power amplifier current settings
 */
typedef enum
{
  AT86RF215_PACUR_22mA_RED = 0x0, //!< current reduction by about 22mA (3dB
                                  //!< reduction of max. small signal gain)
  AT86RF215_PACUR_18mA_RED = 0x1, //!< current reduction by about 18mA (2dB
                                  //!< reduction of max. small signal gain)
  AT86RF215_PACUR_11mA_RED = 0x2, //!< current reduction by about 11mA (1dB
                                  //!< reduction of max. small signal gain)
  AT86RF215_PACUR_NO_RED = 0x3, //!< No power amplifier current reduction (max.
                                //!< transmit small signal gain)
} at86rf215_pacur_t;

/**
 * Channel mode configuration
 */
typedef enum
{
  AT86RF215_CM_IEEE = 0, //!< IEEE compliant
  AT86RF215_CM_FINE_RES_04 =
      1, //!< Fine resolution (389.5-510.0)MHz with 99.182Hz channel stepping
  AT86RF215_CM_FINE_RES_09 =
      2, //!< Fine resolution (779-1020)MHz with 198.364Hz channel stepping
  AT86RF215_CM_FINE_RES_24 =
      3 //!< Fine resolution (2400-2483.5)MHz with 396.728Hz channel stepping
} at86rf215_cm_t;

/**
 * PLL loop bandwidth setting
 */
typedef enum
{
  AT86RF215_PLL_LBW_DEFAULT = 0, //!< Default
  AT86RF215_PLL_LBW_SMALLER = 1, //!< 15% smaller PLL loop bandwidth
  AT86RF215_PLL_LBW_LARGER  = 2  //!< 15% larger PLL loop bandwidth
} at86rf215_pll_lbw_t;

/**
 * PLL lock status
 */
typedef enum
{
  AT86RF215_PLL_UNLOCKED = 0, //!< PLL is not locked
  AT86RF215_PLL_LOCKED   = 1  //!< PLL is locked
} at86rf215_pll_ls_t;

/**
 * Output pads (IRQ, MISO,  FEA09, FEB09, FEA24, FEB24)) driving current
 * strength
 */
typedef enum
{
  AT86RF215_RF_DRV2 = 0, //!< 2 mA
  AT86RF215_RF_DRV4 = 1, //!< 4 mA
  AT86RF215_RF_DRV6 = 2, //!< 6 mA
  AT86RF215_RF_DRV8 = 3  //!< 8 mA
} at86rf215_drv_t;

/**
 * LVDS driving current strength
 */
typedef enum
{
  AT86RF215_LVDS_DRV1 = 0, //!< 1 mA
  AT86RF215_LVDS_DRV2 = 1, //!< 2 mA
  AT86RF215_LVDS_DRV3 = 2, //!< 3 mA
  AT86RF215_LVDS_DRV4 = 3, //!< 4 mA
} at86rf215_lvds_drv_t;

/**
 * LVDS common mode voltage
 */
typedef enum
{
  AT86RF215_LVDS_CMV150 = 0, //!< 150 mV
  AT86RF215_LVDS_CMV200 = 1, //!< 200 mV
  AT86RF215_LVDS_CMV250 = 2, //!< 250 mV
  AT86RF215_LVDS_CMV300 = 3, //!< 300 mV
} at86rf215_lvds_cmv_t;

struct at86rf215_radio_conf
{
  at86rf215_cm_t      cm;
  uint32_t            cs;
  uint32_t            base_freq;
  at86rf215_pll_lbw_t lbw;
};

/**
 * Physical layer type of the baseband core
 */
typedef enum
{
  AT86RF215_BB_PHYOFF  = 0, //!< BB core is OFF
  AT86RF215_BB_MRFSK   = 1, //!< MR-FSK
  AT86RF215_BB_MROFDM  = 2, //!< MR-OFDM
  AT86RF215_BB_MROQPSK = 3  //!< MR-OQPSK
} at86rf215_phy_t;

/**
 * FCS type
 */
typedef enum
{
  AT86RF215_FCS_16 = 0, //!< 16-bit
  AT86RF215_FCS_32 = 1  //!< 32-bit
} at86rf215_fcs_t;

/**
 * FSK 3-dB bandwidth of the shaping filter
 */
typedef enum
{
  AT86RF215_FSK_BT_05 = 0, //!< BT = 0.5
  AT86RF215_FSK_BT_10 = 1, //!< BT = 1.0
  AT86RF215_FSK_BT_15 = 2, //!< BT = 1.5
  AT86RF215_FSK_BT_20 = 3, //!< BT = 2.0
} at86rf215_fsk_bt_t;

/**
 * Scaling factor with regard to the modulation index.
 * The effectively applied modulation index results in the modulation index
 * configured with MIDX times the scaling factor.
 */
typedef enum
{
  AT86RF215_MIDXS_78  = 0, //!< s = 1.0 - 1/8 (7/8)
  AT86RF215_MIDXS_88  = 1, //!< s = 1.0 (8/8)
  AT86RF215_MIDXS_98  = 2, //!< s = 1.0 + 1/8 (9/8)
  AT86RF215_MIDXS_108 = 3, //!< s = 1.0 + 1/4 (10/8)
} at86rf215_fsk_midxs_t;

/**
 * Modulation index
 */
typedef enum
{
  AT86RF215_MIDX_0 = 0, //!< Modulation index = 0.375
  AT86RF215_MIDX_1 = 1, //!< Modulation index = 0.5
  AT86RF215_MIDX_2 = 2, //!< Modulation index = 0.75
  AT86RF215_MIDX_3 = 3, //!< Modulation index = 1.0
  AT86RF215_MIDX_4 = 4, //!< Modulation index = 1.25
  AT86RF215_MIDX_5 = 5, //!< Modulation index = 1.5
  AT86RF215_MIDX_6 = 6, //!< Modulation index = 1.75
  AT86RF215_MIDX_7 = 7, //!< Modulation index = 2.0
} at86rf215_fsk_midx_t;

/**
 * FSK type
 */
typedef enum
{
  AT86RF215_2FSK = 0, //!< 2-FSK
  AT86RF215_4FSK = 1  //!< 4-FSK
} at86rf215_fsk_t;

/**
 * FSK symbol rates
 */
typedef enum
{
  AT86RF215_FSK_SRATE_50  = 0, //!< 50 kHz
  AT86RF215_FSK_SRATE_100 = 1, //!< 100 kHz
  AT86RF215_FSK_SRATE_150 = 2, //!< 150 kHz
  AT86RF215_FSK_SRATE_200 = 3, //!< 200 kHz
  AT86RF215_FSK_SRATE_300 = 4, //!< 300 kHz
  AT86RF215_FSK_SRATE_400 = 5  //!< 400 kHz
} at86rf215_fsk_srate_t;

/**
 * Receiver override setting.
 * If Receiver Override is enabled during frame reception,
 * the FSK receiver restarts synchronization after a rapid input signal
 * increase. The minimum input signal level is -110dBm.
 */
typedef enum
{
  AT86RF215_FSK_RXO_6DB  = 0,    //!< Receiver restarted by >6dB stronger frame
  AT86RF215_FSK_RXO_12DB = 1,    //!< Receiver restarted by >12dB stronger frame
  AT86RF215_FSK_RXO_18DB = 2,    //!< Receiver restarted by >18dB stronger frame
  AT86RF215_FSK_RXO_DISABLED = 3 //!< Receiver override disabled
} at86rf215_fsk_rxo_t;

/**
 * FSK FEC Scheme
 */
typedef enum
{
  AT86RF215_FSK_FEC_NRNSC =
      0, //!< non-recursive and non-systematic convolutional cod
  AT86RF215_FSK_FEC_RSC = 1 //!< recursive and systematic convolutional code
} at86rf215_fsk_fecs_t;

/**
 * SFD condifuration
 */
typedef enum
{
  AT86RF215_SFD_UNCODED_IEEE = 0, //!< Uncoded IEEE mode
  AT86RF215_SFD_UNCODED_RAW  = 1, //!< Uncoded RAW mode
  AT86RF215_SFD_CODED_IEEE   = 2, //!< Coded IEEE mode
  AT86RF215_SFD_CODED_RAW    = 3, //!< Coded RAW mode
} at86rf215_sfd_mode_t;

/**
 * The AGC target level relative to ADC full scale
 */
typedef enum
{
  AT86RF215_TGT_M21 = 0, //!< Target=-21dB
  AT86RF215_TGT_M24 = 1, //!< Target=-24dB
  AT86RF215_TGT_M27 = 2, //!< Target=-27dB
  AT86RF215_TGT_M30 = 3, //!< Target=-30dB
  AT86RF215_TGT_M33 = 4, //!< Target=-33dB
  AT86RF215_TGT_M36 = 5, //!< Target=-36dB
  AT86RF215_TGT_M39 = 6, //!< Target=-39dB
  AT86RF215_TGT_M42 = 7, //!< Target=-42dB
} at86rf215_agc_tgt_t;

/**
 * AGC Average Time in Number of Samples
 */
typedef enum
{
  AT86RF215_AVGS_8  = 0, //!< 8 samples
  AT86RF215_AVGS_16 = 1, //!< 16 samples
  AT86RF215_AVGS_32 = 2, //!< 32 samples
  AT86RF215_AVGS_64 = 3, //!< 64 samples
} at86rf215_agc_avgs_t;

/**
 * Depending on the external frontend setup (external LNA), a specific AGC gain
 * mapping is applied considering the gain of the external LNA (about 9dB or
 * 12dB external LNA gain
 */
typedef enum
{
  AT86RF215_AGC_MAP_INTERNAL = 0, //!< Internal AGC, no external LNA used
  AT86RF215_AGC_MAP_9DB =
      1, //!< AGC back-off for external LNA of about 9dB gain
  AT86RF215_AGC_MAP_12DB =
      2 //!< AGC back-off for external LNA of about 12dB gain
} at86rf215_agc_map_t;

/**
 * Internal power amplifier voltage
 */
typedef enum
{
  AT86RF215_PAVC_2_0V = 0, //!< 2V
  AT86RF215_PAVC_2_2V = 1, //!< 2.2V
  AT86RF215_PAVC_2_4V = 2  //!< 2.4V
} at86rf215_pavc_t;

#endif /*ZEPHYR_DRIVERS_RF215_INTERNAL_H*/