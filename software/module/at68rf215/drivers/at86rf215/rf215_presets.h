#ifndef ZEPHYR_DRIVERS_RF215_PRESETS_H
#define ZEPHYR_DRIVERS_RF215_PRESETS_H

#include "rf215.h"

/**
 * MR-FSK baseband presets. The modulator is always GFSK, and bt sets its filter
 * (section 6.10.6.1). Pre-emphasis would bypass bt (section 6.10.4.2), so it stays off.
 *
 * Index is the value passed to select_preset().
 */
#define RF215_NUM_PRESETS 1

static const struct rf215_bb_conf rf215_fsk_presets[RF215_NUM_PRESETS] = {

	/* ------------------------------------------------------------------ */
	/* Preset 0                                                           */
	/* ------------------------------------------------------------------ */
	[0] = {
		.ctx    = 0,                 /* continuous tx mode: 0 or 1 */
		.fcsfe  = 0,                 /* frame check sequence filter enable: 0 or 1 */
		.txafcs = 1,                 /* tx auto frame check sequence: 0 or 1 */
		.fcst   = RF215_FCS_32,      /* frame check sequence type: RF215_FCS_16 | RF215_FCS_32 */
		.pt     = RF215_BB_MRFSK,    /* phy type: RF215_BB_PHYOFF | RF215_BB_MRFSK | RF215_BB_MROFDM | RF215_BB_MROQPSK */
		/* 50 kHz, h = 1, v.3 chip: tables 6-51 and 6-54, then 6-62 for RF09 or 6-63 for RF24 */
		.fe = {
			[RF09] = {
				.paramp = 3, .lpfcut = 0, .tx_rcut = 4, .tx_sr = 8,
				.rx_bw = 0, .rx_ifs = 0, .rx_rcut = 1, .rx_sr = 10,
				.agc_avgs = 0, .agc_tgt = 1,
			},
			[RF24] = {
				.paramp = 3, .lpfcut = 0, .tx_rcut = 4, .tx_sr = 8,
				.rx_bw = 1, .rx_ifs = 0, .rx_rcut = 1, .rx_sr = 10,
				.agc_avgs = 0, .agc_tgt = 1,
			},
		},
		.fsk = {
			.mord             = RF215_2FSK,           /* modulation order: RF215_2FSK | RF215_4FSK */
			.midx             = 3,                    /* modulation index: 0..7 (h = 0.375..2.0) */
			.midxs            = RF215_MIDXS_88,       /* modulation index scale: RF215_MIDXS_78 | RF215_MIDXS_88 | RF215_MIDXS_98 | RF215_MIDXS_108 */
			.bt               = RF215_FSK_BT_10,      /* bandwidth time product: RF215_FSK_BT_05 | RF215_FSK_BT_10 | RF215_FSK_BT_15 | RF215_FSK_BT_20 */
			.srate            = RF215_FSK_SRATE_50,   /* symbol rate: RF215_FSK_SRATE_50 | _100 | _150 | _200 | _300 | _400 */
			.fi               = 0,                    /* frequency inversion: 0 or 1 */
			.preamble_length  = 8,                    /* preamble length: 0..1023 octets */
			.rxo              = RF215_FSK_RXO_DISABLED, /* receiver override: RF215_FSK_RXO_6DB | _12DB | _18DB | _DISABLED */
			.pdtm             = 0,                    /* preamble detector mode: 0 or 1 */
			.rxpto            = 0,                    /* rx preamble timeout: 0 or 1 */
			.mse              = 0,                    /* modulation soft decision / mse: 0 or 1 */
			.pri              = 0,                    /* preamble inversion: 0 or 1 */
			.fecie            = 0,                    /* fec interleaving enable: 0 or 1 */
			.fecs             = RF215_FSK_FEC_NRNSC,  /* fec scheme: RF215_FSK_FEC_NRNSC | RF215_FSK_FEC_RSC */
			.sfd_threshold    = 8,                    /* sfd detector threshold: 0..15 (lower = more sensitive) */
			.preamble_threshold = 5,                  /* preamble detector threshold: 0..15 (lower = more sensitive) */
			.sfdq             = 0,                    /* sfd decision: 0 soft | 1 hard */
			.sfd32            = 0,                    /* sfd search mode: 0 dual 16-bit | 1 single 32-bit */
			.rawrbit          = 0,                    /* raw mode bit order: 0 or 1 (1 = msb first) */
			.csfd1            = RF215_SFD_CODED_IEEE, /* sfd1 configuration: RF215_SFD_UNCODED_IEEE | _UNCODED_RAW | _CODED_IEEE | _CODED_RAW */
			.csfd0            = RF215_SFD_UNCODED_IEEE, /* sfd0 configuration: RF215_SFD_UNCODED_IEEE | _UNCODED_RAW | _CODED_IEEE | _CODED_RAW */
			.sfd0             = 0x7209,               /* sfd0 pattern: 16-bit value (lsb first) */
			.sfd1             = 0x72F6,               /* sfd1 pattern: 16-bit value (lsb first) */
			.sfd              = 0,                    /* tx sfd select: 0 or 1 */
			.dw               = 0,                    /* psdu data whitening: 0 or 1 */
			.rb2              = 0,                    /* reserved phr bit 2: 0 or 1 */
			.rb1              = 0,                    /* reserved phr bit 1: 0 or 1 */
			.dm               = 1,                    /* direct modulation (FSKDM): register value */
			.preemphasis      = 0,                    /* tx preemphasis enable: 0 or 1 */
			.preemphasis_taps = 0,                    /* preemphasis taps: 24-bit value if enabled */
			.fskrrxf          = 0,                    /* raw rx frame length: 0..2047 */
		},
	},
};

#endif /* ZEPHYR_DRIVERS_RF215_PRESETS_H */
