/*
 * Green Bear USB bridge: CDC ACM in → COBS-decode → CRC/length check → radio_tx.
 *
 * Radio path is byte-for-byte the working tx-burst setup (RF24 asleep, RF09,
 * preset 0, TX_POWER 15, TRXOFF after each send).
 *
 * THIS TRANSMITS WHEN A VALID FRAME ARRIVES. Antenna on J3 before power.
 * Console is SERCOM5 115200 8N1. Look for "sent" / drop counters.
 *
 * Protocol: MPM-Ion0-software/docs/greenbear-protocol.md
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

#include <string.h>

#include "cobs.h"
#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"

#define TX_POWER         15
#define TX_TIMEOUT_MS    1000
#define PAC_EXPECTED     ((RF215_PACUR_NO_RED << 5) | TX_POWER)
#define PC_FCSOK         BIT(5)
#define PC_EXPECTED      0x15

#define HEADER_SIZE      16
#define CRC_SIZE         2
#define MAX_CONTENT      1024
#define MAX_LOGICAL      (HEADER_SIZE + MAX_CONTENT + CRC_SIZE)
#define ACCUM_MAX        1100

#define CALLSIGN_0       'V'
#define CALLSIGN_1       'E'
#define CALLSIGN_2       '3'
#define CALLSIGN_3       ' '
#define CALLSIGN_4       'J'
#define CALLSIGN_5       'K'
#define CALLSIGN_6       'B'
#define VERSION          1

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(at86rf215));
static const struct device *const cdc = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

static uint8_t accum[ACCUM_MAX];
static size_t accum_len;
static uint8_t logical[MAX_LOGICAL];

static unsigned int sent_ok;
static unsigned int drop_cobs;
static unsigned int drop_crc;
static unsigned int drop_len;
static unsigned int drop_hdr;
static unsigned int drop_tx;

static bool radio_setup(void)
{
	const struct rf215_api *api = radio->api;
	uint8_t pac = 0xAA, pc = 0xAA;
	int ret = api->set_sleep(radio, RF24, 1);

	ret |= api->set_tx_power(radio, RF09, TX_POWER);
	ret |= api->bb_enable(radio, RF09, 1);
	ret |= rf215_blk_read(radio, REG_RF09_PAC, &pac, 1);
	ret |= rf215_blk_read(radio, REG_BBC0_PC, &pc, 1);

	bool pass = ret == 0 && pac == PAC_EXPECTED && (pc & ~PC_FCSOK) == PC_EXPECTED;

	printk("setup %s: ret=%d pac=0x%02x pc=0x%02x\n",
	       pass ? "PASS" : "FAIL", ret, pac, pc);
	return pass;
}

static bool callsign_ok(const uint8_t *f)
{
	return f[0] == CALLSIGN_0 && f[1] == CALLSIGN_1 && f[2] == CALLSIGN_2 &&
	       f[3] == CALLSIGN_3 && f[4] == CALLSIGN_4 && f[5] == CALLSIGN_5 &&
	       f[6] == CALLSIGN_6 && f[7] == VERSION;
}

static void handle_cobs_frame(const uint8_t *enc, size_t enc_len)
{
	const struct rf215_api *api = radio->api;
	int n = cobs_decode(enc, enc_len, logical, sizeof(logical));

	if (n < 0) {
		drop_cobs++;
		printk("drop cobs (drops cobs=%u crc=%u len=%u hdr=%u tx=%u)\n",
		       drop_cobs, drop_crc, drop_len, drop_hdr, drop_tx);
		return;
	}
	if (n < HEADER_SIZE + CRC_SIZE) {
		drop_len++;
		printk("drop short n=%d\n", n);
		return;
	}
	if (!callsign_ok(logical)) {
		drop_hdr++;
		printk("drop hdr\n");
		return;
	}

	uint16_t content_len = (uint16_t)logical[14] | ((uint16_t)logical[15] << 8);
	if (content_len > MAX_CONTENT) {
		drop_len++;
		printk("drop content_len=%u\n", content_len);
		return;
	}
	int expect = HEADER_SIZE + (int)content_len + CRC_SIZE;
	if (n != expect) {
		drop_len++;
		printk("drop len n=%d expect=%d\n", n, expect);
		return;
	}

	uint16_t want = (uint16_t)logical[n - 2] | ((uint16_t)logical[n - 1] << 8);
	uint16_t got = crc16_itu_t(0xFFFF, logical, (size_t)(n - CRC_SIZE));
	if (got != want) {
		drop_crc++;
		printk("drop crc got=0x%04x want=0x%04x\n", got, want);
		return;
	}

	int ret = api->radio_tx(radio, RF09, logical, (size_t)n, TX_TIMEOUT_MS);
	int off = api->set_sleep(radio, RF09, 0);
	k_msleep(1);

	uint8_t state = 0xAA;
	int read = rf215_blk_read(radio, REG_RF09_STATE, &state, 1);
	bool pass = ret == 0 && off == 0 && read == 0 && state == STATE_RF_TRXOFF;

	if (!pass) {
		drop_tx++;
		printk("drop tx ret=%d state=0x%02x\n", ret, state);
		return;
	}

	uint16_t seq = (uint16_t)logical[8] | ((uint16_t)logical[9] << 8);
	sent_ok++;
	printk("sent seq=%u len=%u n=%d ok=%u\n", seq, content_len, n, sent_ok);
}

static void feed_byte(uint8_t b)
{
	if (b == 0x00) {
		if (accum_len > 0) {
			handle_cobs_frame(accum, accum_len);
		}
		accum_len = 0;
		return;
	}
	if (accum_len >= ACCUM_MAX) {
		/* Overflow: discard and resync at next delimiter. */
		accum_len = 0;
		drop_cobs++;
		printk("drop overflow\n");
		return;
	}
	accum[accum_len++] = b;
}

int main(void)
{
	if (!device_is_ready(radio)) {
		printk("AT86RF215 not ready\n");
		return 0;
	}
	if (!device_is_ready(cdc)) {
		printk("CDC ACM not ready\n");
		return 0;
	}
	if (!radio_setup()) {
		printk("not transmitting\n");
		return 0;
	}

	printk("usb-bridge ready (cdc=%s)\n", cdc->name);

	while (1) {
		uint8_t b;
		int got = 0;

		while (uart_poll_in(cdc, &b) == 0) {
			feed_byte(b);
			got = 1;
		}
		if (!got) {
			k_msleep(1);
		}
	}

	return 0;
}
