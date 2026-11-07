/*
 * Hardware test: can a receiver see the radio at all? Sends one maximum length
 * frame of known bytes on RF09 every 2 seconds, to be spotted on a waterfall.
 *
 * THIS TRANSMITS AS SOON AS IT BOOTS. Have an antenna on J3 before flashing, keep
 * the receiver's antenna apart, and never cable J3 to a receiver without an
 * attenuator.
 *
 * Console is 115200 8N1. Look for a burst of about 330 ms at 915 MHz, every 2 s.
 * It is about 100 kHz wide, so tune the receiver a few hundred kHz to one side to
 * keep it off the receiver's own DC spike.
 *
 * setup: power and baseband can only be set in TRXOFF, and PAC resets to maximum.
 *        Both are read back, and nothing is sent unless they hold. RF24 is asleep:
 *        this is the application's call, not the driver's.
 * burst: frame sent and TXFE seen, and the radio is back in TRXOFF. It goes back
 *        even after a failure, and TXPREP's locked PLL can't draw a line between
 *        bursts.
 *
 * A pass here only means the chip says it sent a frame. The receiver is the test.
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"

#define BURST_LEN        2047   /* TXFL's limit. The chip overwrites the last 4 with the FCS */
#define BURST_PERIOD_MS  2000   /* start of one burst to start of the next */
#define BURST_TIMEOUT_MS 1000
#define TX_POWER         15     /* 0..31, about 1 dB steps, 31 is about +14.5 dBm */

#define PAC_EXPECTED     ((RF215_PACUR_NO_RED << 5) | TX_POWER)
#define PC_FCSOK         BIT(5)
#define PC_EXPECTED      0x15   /* preset 0 with BBEN set */

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(at86rf215));
static const char pattern[] = "GOAAATWDE";
static uint8_t psdu[BURST_LEN];

static bool setup(void)
{
    const struct rf215_api *api = radio->api;
    uint8_t pac = 0xAA, pc = 0xAA;
    int ret = api->set_sleep(radio, RF24, 1);

    ret |= api->set_tx_power(radio, RF09, TX_POWER);
    ret |= api->bb_enable(radio, RF09, 1);
    ret |= rf215_blk_read(radio, REG_RF09_PAC, &pac, 1);
    ret |= rf215_blk_read(radio, REG_BBC0_PC, &pc, 1);

    bool pass = ret == 0 && pac == PAC_EXPECTED && (pc & ~PC_FCSOK) == PC_EXPECTED;

    printk("setup %s: ret=%d pac=0x%02x pc=0x%02x\n", pass ? "PASS" : "FAIL", ret, pac, pc);
    return pass;
}

int main(void)
{
    const struct rf215_api *api = radio->api;
    unsigned int round = 0, sent = 0;

    if (!device_is_ready(radio)) {
        printk("AT86RF215 not ready, driver init failed\n");
        return 0;
    }
    if (!setup()) {
        printk("not transmitting\n");
        return 0;
    }

    for (size_t i = 0; i < sizeof(psdu); i++) {
        psdu[i] = pattern[i % (sizeof(pattern) - 1)];
    }

    int64_t next = k_uptime_get();

    while (1) {
        int ret = api->radio_tx(radio, RF09, psdu, sizeof(psdu), BURST_TIMEOUT_MS);
        int off = api->set_sleep(radio, RF09, 0); /* en = 0 is the TRXOFF command */

        k_msleep(1);

        uint8_t state = 0xAA;
        int read = rf215_blk_read(radio, REG_RF09_STATE, &state, 1);
        bool pass = ret == 0 && off == 0 && read == 0 && state == STATE_RF_TRXOFF;

        round++;
        sent += pass;
        printk("[%u] burst %s: ret=%d state=0x%02x, sent %u/%u\n", round,
               pass ? "PASS" : "FAIL", ret, state, sent, round);

        next += BURST_PERIOD_MS;
        k_sleep(K_TIMEOUT_ABS_MS(next));
    }

    return 0;
}
