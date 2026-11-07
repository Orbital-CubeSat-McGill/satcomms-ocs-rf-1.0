/*
 * The hat's application: sends one counted beacon frame on RF09 every second, for the
 * ground station to decode (ground-station/fsk_decode.py reads this frame).
 *
 * THIS TRANSMITS AS SOON AS IT BOOTS. Have an antenna on J3 before flashing, keep
 * the receiver's antenna apart, and never cable J3 to a receiver without an
 * attenuator.
 *
 * Frame, multi-byte fields little endian:
 *    0  version
 *    1  call sign, ASCII, padded with spaces (10 bytes)
 *   11  frame counter (4), counts attempts, so a failed send shows as a lost frame
 *   15  uptime in ms when the frame was built (4)
 *   19  reset cause, PM_RCAUSE as read at boot
 *   20  TXPWR, 0..31
 *   21  TX errors so far (4)
 *   25  pattern seeded by the frame counter, so the ground can count bit errors
 *  124  FCS (4), written by the chip
 */
#include <soc.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"

#define CALL_SIGN        "VA2AOX"   /* the licence holder's call sign. NOCALL never transmits */

#define FRAME_VERSION    1
#define FRAME_LEN        128    /* the chip overwrites the last 4 with the FCS */
#define FRAME_PERIOD_MS  1000   /* start of one frame to start of the next */
#define FRAME_TIMEOUT_MS 200    /* a frame is on air for about 23 ms */
#define TX_POWER         15     /* 0..31, about 1 dB steps, 31 is about +14.5 dBm */

#define OFS_VERSION      0
#define OFS_CALL_SIGN    1
#define CALL_SIGN_LEN    10
#define OFS_COUNTER      11
#define OFS_UPTIME       15
#define OFS_RESET_CAUSE  19
#define OFS_TX_POWER     20
#define OFS_TX_ERRORS    21
#define OFS_PATTERN      25
#define PATTERN_LEN      (FRAME_LEN - 4 - OFS_PATTERN)
#define PATTERN_SEED     0x6C078965 /* so counter 0 doesn't seed the generator with 0 */

#define PAC_EXPECTED     ((RF215_PACUR_NO_RED << 5) | TX_POWER)
#define PC_FCSOK         BIT(5)
#define PC_EXPECTED      0x15   /* preset 0 with BBEN set */

#define RADIO_STACK_SIZE 1024
#define RADIO_PRIORITY   5

BUILD_ASSERT(sizeof(CALL_SIGN) - 1 <= CALL_SIGN_LEN, "call sign doesn't fit the frame");

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(at86rf215));
static uint8_t reset_cause;

/* xorshift32. fsk_decode.py's app_pattern has to match this */
static void fill_pattern(uint8_t *out, size_t len, uint32_t counter)
{
    uint32_t x = counter ^ PATTERN_SEED;

    for (size_t i = 0; i < len; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        out[i] = (uint8_t)x;
    }
}

static void build_frame(uint8_t *frame, uint32_t counter, uint32_t uptime, uint8_t cause,
                        uint32_t tx_errors)
{
    frame[OFS_VERSION] = FRAME_VERSION;
    memset(&frame[OFS_CALL_SIGN], ' ', CALL_SIGN_LEN);
    memcpy(&frame[OFS_CALL_SIGN], CALL_SIGN, sizeof(CALL_SIGN) - 1);
    sys_put_le32(counter, &frame[OFS_COUNTER]);
    sys_put_le32(uptime, &frame[OFS_UPTIME]);
    frame[OFS_RESET_CAUSE] = cause;
    frame[OFS_TX_POWER] = TX_POWER;
    sys_put_le32(tx_errors, &frame[OFS_TX_ERRORS]);
    fill_pattern(&frame[OFS_PATTERN], PATTERN_LEN, counter);
    memset(&frame[FRAME_LEN - 4], 0, 4);
}

static bool setup(void)
{
    const struct rf215_api *api = radio->api;
    uint8_t pac = 0xAA, pc = 0xAA;
    int ret = api->set_sleep(radio, RF24, 1); /* RF24 isn't used. This is the app's call, not the driver's */

    ret |= api->set_tx_power(radio, RF09, TX_POWER);
    ret |= api->bb_enable(radio, RF09, 1);
    ret |= rf215_blk_read(radio, REG_RF09_PAC, &pac, 1);
    ret |= rf215_blk_read(radio, REG_BBC0_PC, &pc, 1);

    bool pass = ret == 0 && pac == PAC_EXPECTED && (pc & ~PC_FCSOK) == PC_EXPECTED;

    printk("setup %s: ret=%d pac=0x%02x pc=0x%02x rcause=0x%02x call=%s\n", pass ? "PASS" : "FAIL",
           ret, pac, pc, reset_cause, CALL_SIGN);
    return pass;
}

static void radio_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct rf215_api *api = radio->api;
    static uint8_t frame[FRAME_LEN];
    uint32_t counter = 0, tx_errors = 0;
    int64_t next = k_uptime_get();

    while (1) {
        build_frame(frame, counter, k_uptime_get_32(), reset_cause, tx_errors);

        int ret = api->radio_tx(radio, RF09, frame, sizeof(frame), FRAME_TIMEOUT_MS);
        int off = api->set_sleep(radio, RF09, 0); /* en = 0 is the TRXOFF command */

        k_msleep(1);

        uint8_t state = 0xAA;
        int read = rf215_blk_read(radio, REG_RF09_STATE, &state, 1);
        bool pass = ret == 0 && off == 0 && read == 0 && state == STATE_RF_TRXOFF;

        tx_errors += !pass;
        printk("[%u] frame %s: ret=%d state=0x%02x tx_errors=%u\n", counter,
               pass ? "PASS" : "FAIL", ret, state, tx_errors);
        counter++;

        next += FRAME_PERIOD_MS;
        k_sleep(K_TIMEOUT_ABS_MS(next));
    }
}

K_THREAD_DEFINE(radio_tid, RADIO_STACK_SIZE, radio_thread, NULL, NULL, NULL, RADIO_PRIORITY, 0,
                SYS_FOREVER_MS);

int main(void)
{
    reset_cause = PM->RCAUSE.reg;

    if (!device_is_ready(radio)) {
        printk("AT86RF215 not ready, driver init failed\n");
        return 0;
    }
    if (strcmp(CALL_SIGN, "NOCALL") == 0) {
        printk("CALL_SIGN isn't set, not transmitting\n");
        return 0;
    }
    if (!setup()) {
        printk("not transmitting\n");
        return 0;
    }

    k_thread_start(radio_tid);
    return 0;
}
