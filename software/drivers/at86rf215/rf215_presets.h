#ifndef ZEPHYR_DRIVERS_RF215_PRESETS_H
#define ZEPHYR_DRIVERS_RF215_PRESETS_H

#include "rf215.h"

/**
 * Four MR-FSK baseband presets.
 *
 *  These are all options from the AT86RF215 datasheet. 
 *
 * Index 0..3 are the values passed to select_preset().

 * TODO: modify values to define 4 presets (2x 2FSK and 2x 4FSK)
 */
#define RF215_NUM_PRESETS 4

static const struct rf215_bb_conf rf215_fsk_presets[RF215_NUM_PRESETS] = {

	/* ------------------------------------------------------------------ */
	/* Preset 0                                                           */
	/* ------------------------------------------------------------------ */
	[0] = {
		.ctx    = 0,                 /* continuous tx mode: 0 or 1 */
		.fcsfe  = 0,                 /* frame check sequence filter enable: 0 or 1 */
		.txafcs = 0,                 /* tx auto frame check sequence: 0 or 1 */
		.fcst   = RF215_FCS_32,      /* frame check sequence type: RF215_FCS_16 | RF215_FCS_32 */
		.pt     = RF215_BB_MRFSK,    /* phy type: RF215_BB_PHYOFF | RF215_BB_MRFSK | RF215_BB_MROFDM | RF215_BB_MROQPSK */
		.fsk = {
			.mord             = RF215_2FSK,           /* modulation order: RF215_2FSK | RF215_4FSK */
			.midx             = 0,                    /* modulation index: 0..7 (h = 0.375..2.0) */
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
			.preamble_threshold = 8,                  /* preamble detector threshold: 0..15 (lower = more sensitive) */
			.sfdq             = 0,                    /* sfd decision: 0 soft | 1 hard */
			.sfd32            = 0,                    /* sfd search mode: 0 dual 16-bit | 1 single 32-bit */
			.rawrbit          = 0,                    /* raw mode bit order: 0 or 1 (1 = msb first) */
			.csfd1            = RF215_SFD_UNCODED_IEEE, /* sfd1 configuration: RF215_SFD_UNCODED_IEEE | _UNCODED_RAW | _CODED_IEEE | _CODED_RAW */
			.csfd0            = RF215_SFD_UNCODED_IEEE, /* sfd0 configuration: RF215_SFD_UNCODED_IEEE | _UNCODED_RAW | _CODED_IEEE | _CODED_RAW */
			.sfd0             = 0x7209,               /* sfd0 pattern: 16-bit value (lsb first) */
			.sfd1             = 0x0000,               /* sfd1 pattern: 16-bit value (lsb first) */
			.sfd              = 0,                    /* tx sfd select: 0 or 1 */
			.dw               = 0,                    /* psdu data whitening: 0 or 1 */
			.rb2              = 0,                    /* reserved phr bit 2: 0 or 1 */
			.rb1              = 0,                    /* reserved phr bit 1: 0 or 1 */
			.dm               = 0,                    /* direct modulation (FSKDM): register value */
			.preemphasis      = 0,                    /* tx preemphasis enable: 0 or 1 */
			.preemphasis_taps = 0,                    /* preemphasis taps: 24-bit value if enabled */
			.fskrrxf          = 0,                    /* raw rx frame length: 0..2047 */
		},
	},

	/* ------------------------------------------------------------------ */
	/* Preset 1                                                           */
	/* ------------------------------------------------------------------ */
	[1] = {
		.ctx    = 0,
		.fcsfe  = 0,
		.txafcs = 0,
		.fcst   = RF215_FCS_32,
		.pt     = RF215_BB_MRFSK,
		.fsk = {
			.mord             = RF215_2FSK,
			.midx             = 0,
			.midxs            = RF215_MIDXS_88,
			.bt               = RF215_FSK_BT_10,
			.srate            = RF215_FSK_SRATE_100,
			.fi               = 0,
			.preamble_length  = 8,
			.rxo              = RF215_FSK_RXO_DISABLED,
			.pdtm             = 0,
			.rxpto            = 0,
			.mse              = 0,
			.pri              = 0,
			.fecie            = 0,
			.fecs             = RF215_FSK_FEC_NRNSC,
			.sfd_threshold    = 8,
			.preamble_threshold = 8,
			.sfdq             = 0,
			.sfd32            = 0,
			.rawrbit          = 0,
			.csfd1            = RF215_SFD_UNCODED_IEEE,
			.csfd0            = RF215_SFD_UNCODED_IEEE,
			.sfd0             = 0x7209,
			.sfd1             = 0x0000,
			.sfd              = 0,
			.dw               = 0,
			.rb2              = 0,
			.rb1              = 0,
			.dm               = 0,
			.preemphasis      = 0,
			.preemphasis_taps = 0,
			.fskrrxf          = 0,
		},
	},

	/* ------------------------------------------------------------------ */
	/* Preset 2                                                           */
	/* ------------------------------------------------------------------ */
	[2] = {
		.ctx    = 0,
		.fcsfe  = 0,
		.txafcs = 0,
		.fcst   = RF215_FCS_32,
		.pt     = RF215_BB_MRFSK,
		.fsk = {
			.mord             = RF215_4FSK,
			.midx             = 0,
			.midxs            = RF215_MIDXS_88,
			.bt               = RF215_FSK_BT_10,
			.srate            = RF215_FSK_SRATE_200,
			.fi               = 0,
			.preamble_length  = 8,
			.rxo              = RF215_FSK_RXO_DISABLED,
			.pdtm             = 0,
			.rxpto            = 0,
			.mse              = 0,
			.pri              = 0,
			.fecie            = 0,
			.fecs             = RF215_FSK_FEC_NRNSC,
			.sfd_threshold    = 8,
			.preamble_threshold = 8,
			.sfdq             = 0,
			.sfd32            = 0,
			.rawrbit          = 0,
			.csfd1            = RF215_SFD_UNCODED_IEEE,
			.csfd0            = RF215_SFD_UNCODED_IEEE,
			.sfd0             = 0x7209,
			.sfd1             = 0x0000,
			.sfd              = 0,
			.dw               = 0,
			.rb2              = 0,
			.rb1              = 0,
			.dm               = 0,
			.preemphasis      = 0,
			.preemphasis_taps = 0,
			.fskrrxf          = 0,
		},
	},

	/* ------------------------------------------------------------------ */
	/* Preset 3                                                           */
	/* ------------------------------------------------------------------ */
	[3] = {
		.ctx    = 0,
		.fcsfe  = 0,
		.txafcs = 0,
		.fcst   = RF215_FCS_32,
		.pt     = RF215_BB_MRFSK,
		.fsk = {
			.mord             = RF215_4FSK,
			.midx             = 0,
			.midxs            = RF215_MIDXS_88,
			.bt               = RF215_FSK_BT_10,
			.srate            = RF215_FSK_SRATE_400,
			.fi               = 0,
			.preamble_length  = 8,
			.rxo              = RF215_FSK_RXO_DISABLED,
			.pdtm             = 0,
			.rxpto            = 0,
			.mse              = 0,
			.pri              = 0,
			.fecie            = 0,
			.fecs             = RF215_FSK_FEC_NRNSC,
			.sfd_threshold    = 8,
			.preamble_threshold = 8,
			.sfdq             = 0,
			.sfd32            = 0,
			.rawrbit          = 0,
			.csfd1            = RF215_SFD_UNCODED_IEEE,
			.csfd0            = RF215_SFD_UNCODED_IEEE,
			.sfd0             = 0x7209,
			.sfd1             = 0x0000,
			.sfd              = 0,
			.dw               = 0,
			.rb2              = 0,
			.rb1              = 0,
			.dm               = 0,
			.preemphasis      = 0,
			.preemphasis_taps = 0,
			.fskrrxf          = 0,
		},
	},
};

#endif /* ZEPHYR_DRIVERS_RF215_PRESETS_H */
