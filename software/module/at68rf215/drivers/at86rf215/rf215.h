#ifndef ZEPHYR_DRIVERS_AT86RF215_H
#define ZEPHYR_DRIVERS_AT86RF215_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

/**
 * Radio front-end
 */
typedef enum
{
  RF09 = 0, //!< Sub-1 GHz radio
  RF24 = 1, //!< 2.4 GHz radio
} rf215_radio_t;

// Values that can be read with get_value, useful for logging and evaluating link resilience
typedef enum
{
    RSSI,
    EDV,
    AGC,
} get_variable;

/**
 * Channel mode configuration
 */
typedef enum
{
  RF215_CM_IEEE = 0, //!< IEEE compliant
  RF215_CM_FINE_RES_04 =
  1, //!< Fine resolution (389.5-510.0)MHz with 99.182Hz channel stepping
  RF215_CM_FINE_RES_09 =
  2, //!< Fine resolution (779-1020)MHz with 198.364Hz channel stepping
  RF215_CM_FINE_RES_24 =
  3 //!< Fine resolution (2400-2483.5)MHz with 396.728Hz channel stepping
} rf215_cm_t;

/**
 * PLL loop bandwidth setting
 */
typedef enum
{
  RF215_PLL_LBW_DEFAULT = 0, //!< Default
  RF215_PLL_LBW_SMALLER = 1, //!< 15% smaller PLL loop bandwidth
  RF215_PLL_LBW_LARGER  = 2  //!< 15% larger PLL loop bandwidth
} rf215_pll_lbw_t;

struct rf215_radio_conf
{
  rf215_cm_t          cm;        //!< Channel scheme, only RF215_CM_IEEE is implemented
  uint32_t            cs;        //!< Channel spacing in Hz, a multiple of 25 kHz up to 6.375 MHz
  uint32_t            base_freq; //!< Frequency of channel 0 in Hz, a multiple of 25 kHz
  rf215_pll_lbw_t     lbw;       //!< PLL loop bandwidth, RF09 only (ignored for RF24)
};

struct rf215_api {
    int (*conn_check)(const struct device *dev, uint8_t *part); //confirm at86rf215 is connected and alive, part gets the raw part number
    int (*set_sleep)(const struct device *dev, rf215_radio_t radio, uint8_t en); //set sleep state of transceiver or trxoff
    int (*radio_conf)(const struct device *dev, rf215_radio_t radio, const struct rf215_radio_conf *conf); //set channel spacing, base frequency and PLL bandwidth, needs TRXOFF, resets to channel 0
    int (*select_preset)(const struct device *dev, rf215_radio_t radio, uint8_t idx); /* validate, lock, apply */
    int (*set_channel)(const struct device *dev, rf215_radio_t radio, uint16_t ch); /* checked against active preset */
    int (*set_tx_power)(const struct device *dev, rf215_radio_t radio, uint8_t txpwr); /* raw TXPWR register step, 0..31 */
    int (*bb_enable)(const struct device *dev, rf215_radio_t radio, uint8_t en);
    int (*get_value)(const struct device *dev, rf215_radio_t radio, get_variable var, uint8_t *value); //get a variable from get_variable enum to value
    int (*tx_buffer)(const struct device *dev, rf215_radio_t radio, const uint8_t *psdu, size_t len); // write to tx buffer
    int (*radio_tx)(const struct device *dev, rf215_radio_t radio, const uint8_t *psdu, size_t len, size_t timeout_ms); // flush tx buffer and transmit data
    int (*radio_rx)(const struct device *dev, rf215_radio_t radio, size_t timeout_ms); // set transceiver to RX mode
    int (*rx_frame)(const struct device *dev, rf215_radio_t radio, uint8_t *psdu, size_t len); // get data from RX buffer
};

//------------------------------------------------------------------------------
// Start of Declaration of radio configuration
//------------------------------------------------------------------------------
//to be defined by user

// Config Enums
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MR-FSK>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
/**
 * FSK type
 */
typedef enum
{
  RF215_2FSK = 0, //!< 2-FSK
  RF215_4FSK = 1  //!< 4-FSK
} rf215_fsk_t;

// Modulation index h: how wide the FSK frequency deviation is relative to the symbol rate
// This enum is a duplicate of at86rf215_fsk_midx_t in rf215_internal.h (the libre space enum)

typedef enum {
  RF215_MIDX_0 = 0,  /* h = 0.375 */
  RF215_MIDX_1 = 1,  /* h = 0.5 */
  RF215_MIDX_2 = 2,  /* h = 0.75 */
  RF215_MIDX_3 = 3,  /* h = 1.0 */
  RF215_MIDX_4 = 4,  /* h = 1.25 */
  RF215_MIDX_5 = 5,  /* h = 1.5 */
  RF215_MIDX_6 = 6,  /* h = 1.75 */
  RF215_MIDX_7 = 7,  /* h = 2.0 */
} rf215_fsk_midx_t;

/**
 * Scaling factor with regard to the modulation index.
 * The effectively applied modulation index results in the modulation index
 * configured with MIDX times the scaling factor.
 */
typedef enum
{
  RF215_MIDXS_78  = 0, //!< s = 1.0 - 1/8 (7/8)
  RF215_MIDXS_88  = 1, //!< s = 1.0 (8/8)
  RF215_MIDXS_98  = 2, //!< s = 1.0 + 1/8 (9/8)
  RF215_MIDXS_108 = 3, //!< s = 1.0 + 1/4 (10/8)
} rf215_fsk_midxs_t;


/**
 * FSK 3-dB bandwidth of the shaping filter
 */
typedef enum
{
  RF215_FSK_BT_05 = 0, //!< BT = 0.5
  RF215_FSK_BT_10 = 1, //!< BT = 1.0
  RF215_FSK_BT_15 = 2, //!< BT = 1.5
  RF215_FSK_BT_20 = 3, //!< BT = 2.0
} rf215_fsk_bt_t;

/**
 * FSK symbol rates
 */
typedef enum
{
  RF215_FSK_SRATE_50  = 0, //!< 50 kHz
  RF215_FSK_SRATE_100 = 1, //!< 100 kHz
  RF215_FSK_SRATE_150 = 2, //!< 150 kHz
  RF215_FSK_SRATE_200 = 3, //!< 200 kHz
  RF215_FSK_SRATE_300 = 4, //!< 300 kHz
  RF215_FSK_SRATE_400 = 5  //!< 400 kHz
} rf215_fsk_srate_t;

/**
 * Receiver override setting.
 * If Receiver Override is enabled during frame reception,
 * the FSK receiver restarts synchronization after a rapid input signal
 * increase. The minimum input signal level is -110dBm.
 */
typedef enum
{
  RF215_FSK_RXO_6DB  = 0,    //!< Receiver restarted by >6dB stronger frame
  RF215_FSK_RXO_12DB = 1,    //!< Receiver restarted by >12dB stronger frame
  RF215_FSK_RXO_18DB = 2,    //!< Receiver restarted by >18dB stronger frame
  RF215_FSK_RXO_DISABLED = 3 //!< Receiver override disabled
} rf215_fsk_rxo_t;

/**
 * FSK FEC Scheme
 */
typedef enum
{
  RF215_FSK_FEC_NRNSC =
      0, //!< non-recursive and non-systematic convolutional cod
  RF215_FSK_FEC_RSC = 1 //!< recursive and systematic convolutional code
} rf215_fsk_fecs_t;

/**
 * SFD condifuration
 */
typedef enum
{
  RF215_SFD_UNCODED_IEEE = 0, //!< Uncoded IEEE mode
  RF215_SFD_UNCODED_RAW  = 1, //!< Uncoded RAW mode
  RF215_SFD_CODED_IEEE   = 2, //!< Coded IEEE mode
  RF215_SFD_CODED_RAW    = 3, //!< Coded RAW mode
} rf215_sfd_mode_t;

//Define configurations struct
struct rf215_mrfsk_conf
{
  rf215_fsk_t       mord;
  rf215_fsk_midx_t  midx;
  rf215_fsk_midxs_t midxs;
  rf215_fsk_bt_t    bt;    //!< Bandwidth time product
  rf215_fsk_srate_t srate; //!< FSK rate
  uint8_t               fi              : 1;
  uint16_t              preamble_length : 10; //!< Preamble length in octets
  rf215_fsk_rxo_t   rxo;
  uint8_t               pdtm  : 1;
  uint8_t               rxpto : 1;
  uint8_t               mse   : 1;
  uint8_t               pri   : 1;
  uint8_t               fecie : 1;
  rf215_fsk_fecs_t  fecs;
  uint8_t sfd_threshold : 4;      //!< Lower values increase the SFD detector
                                  //!< sensitivity
  uint8_t preamble_threshold : 4; //!< Lower values increase the preamble
                                  //!< detector sensitivity
  uint8_t sfdq  : 1;   //!< Set to 1 to use hard decision, 0 to use soft
  uint8_t sfd32 : 1;   //!< If set t0 0, receiver searches for two 16-bit SFD
                       //!< fields configured with {FSKSFD0H;FSKSFD0L} and
                       //!< {FSKSFD1H;FSKSFD1L}.  If set to 1, the FSK receiver
                       //!< searches for a single 32-bit SFD
                       //!< {FSKSFD1H;FSKSFD1L;FSKSFD0H;FSKSFD0L}. (LSB 1st)
  uint8_t rawrbit : 1; //!< If set to 1, the payload bytes in RAW mode are
                       //!< transmitted MSB first
  rf215_sfd_mode_t csfd1;
  rf215_sfd_mode_t csfd0;
  uint16_t             sfd0; //!< 16-bit SFD. LSB first
  uint16_t sfd1; //!< 16-bit SFD. Tranmitted after SFD0 if SFD32 is set
  uint8_t  sfd : 1;
  uint8_t  dw  : 1; //!< If set to 1, whitening of the PSDU is enabled
  uint8_t
      rb2 : 1; //!< Sets the content of the reserved FSK PHR bit 2 for transmit
  uint8_t
      rb1 : 1; //!< Sets the content of the reserved FSK PHR bit 1 for transmit
  uint8_t  dm;
  uint8_t  preemphasis      : 1;
  uint32_t preemphasis_taps : 24;
  uint16_t fskrrxf          : 11; //!< FSK RX frame length for RAW mode
};

//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MR-O-QPSK>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//to be implemented
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MR-OFDM>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//to be implemented
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<BaseBand>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
/**
 * Physical layer type of the baseband core
 */
typedef enum
{
  RF215_BB_PHYOFF  = 0, //!< BB core is OFF
  RF215_BB_MRFSK   = 1, //!< MR-FSK
  RF215_BB_MROFDM  = 2, //!< MR-OFDM
  RF215_BB_MROQPSK = 3  //!< MR-OQPSK
} rf215_phy_t;

/**
 * FCS type
 */
typedef enum
{
  RF215_FCS_16 = 0, //!< 16-bit
  RF215_FCS_32 = 1  //!< 32-bit
} rf215_fcs_t;

struct rf215_bb_conf
{
  uint8_t         ctx    : 1;
  uint8_t         fcsfe  : 1;
  uint8_t         txafcs : 1;
  rf215_fcs_t fcst;
  rf215_phy_t pt;
  union
  {
    struct rf215_mrfsk_conf fsk;
  };
};

//------------------------------------------------------------------------------
// End of Declaration of radio configuration
//------------------------------------------------------------------------------
#endif /*ZEPHYR_DRIVERS_AT86RF215_H*/
