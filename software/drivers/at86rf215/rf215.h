#ifndef ZEPHYR_DRIVERS_AT86RF215_H
#define ZEPHYR_DRIVERS_AT86RF215_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

struct rf215_api {
    //api from tutorial might need to remove later depending on if we want to have a public function get the state of each transceiver 
    int(*get)(const struct device *dev, uint8_t *state);
    //api we might need
    int(*validate)(const struct device *dev, uint8_t *rtn);
    int(*set_param)(const struct device *dev, const struct rf215_param);
    int(*
};

struct rf215_config {
    struct spi_dt_spec rf215_spi;
    struct gpio_dt_spec rf215_rst;
    struct gpio_dt_spec rf215_irq;
    const struct rf215_bb_conf *presets;   /* array in flash, built from DT */
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
    uint8_t num_presets;
    uint8_t  preset_idx;    /* which preset is active */
    uint16_t channel;       /* runtime */
    int8_t   tx_power;      /* runtime */
};

// Enums for get functions
typedef enum
{
    something,
}r215_get;

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
