/*
 * Hardware test: how weak can the link get before frames fail? Sends the app's frame
 * on RF09 every second, stepping TXPWR down from 15 to 0 and starting again.
 * ground-station/weak_signal.py turns the flowgraph's log into a table per level.
 *
 * THIS TRANSMITS AS SOON AS IT BOOTS. Have an antenna on J3 before flashing, keep
 * the receiver's antenna apart, and never cable J3 to a receiver without an
 * attenuator.
 *
 * Console is 115200 8N1. A pass of all 16 levels takes 8 minutes.
 *
 * schedule: the level comes from the frame counter, so the ground knows which level
 *           a frame was sent at even when it never heard that frame. weak_signal.py
 *           has to match TX_POWER_TOP and STEP_FRAMES.
 * frame:    the same layout as software/app/src/main.c, which fsk_decode.py reads.
 * level:    power can only be set in TRXOFF, so it's set after the last frame's
 *           TRXOFF and read back. A frame is never sent at a level that didn't hold.
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

#define CALL_SIGN        "VA2AOX"   /* the licence holder's call sign */

#define TX_POWER_TOP     15     /* the app's level. 0..31, about 1 dB steps, 31 is about +14.5 dBm */
#define STEP_FRAMES      30     /* frames at each level */

#define FRAME_VERSION    1
#define FRAME_LEN        128    /* the chip overwrites the last 4 with the FCS */
#define FRAME_PERIOD_MS  1000   /* start of one frame to start of the next */
#define FRAME_TIMEOUT_MS 200    /* a frame is on air for about 23 ms */

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

#define PC_FCSOK         BIT(5)
#define PC_EXPECTED      0x15   /* preset 0 with BBEN set */

BUILD_ASSERT(sizeof(CALL_SIGN) - 1 <= CALL_SIGN_LEN, "call sign doesn't fit the frame");

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(at86rf215));
static uint8_t frame[FRAME_LEN];

/* weak_signal.py's level_for has to match this */
static uint8_t level_for(uint32_t counter)
{
    return TX_POWER_TOP - (counter / STEP_FRAMES) % (TX_POWER_TOP + 1);
}

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

static void build_frame(uint8_t *out, uint32_t counter, uint32_t uptime, uint8_t cause,
                        uint8_t level, uint32_t tx_errors)
{
    out[OFS_VERSION] = FRAME_VERSION;
    memset(&out[OFS_CALL_SIGN], ' ', CALL_SIGN_LEN);
    memcpy(&out[OFS_CALL_SIGN], CALL_SIGN, sizeof(CALL_SIGN) - 1);
    sys_put_le32(counter, &out[OFS_COUNTER]);
    sys_put_le32(uptime, &out[OFS_UPTIME]);
    out[OFS_RESET_CAUSE] = cause;
    out[OFS_TX_POWER] = level;
    sys_put_le32(tx_errors, &out[OFS_TX_ERRORS]);
    fill_pattern(&out[OFS_PATTERN], PATTERN_LEN, counter);
    memset(&out[FRAME_LEN - 4], 0, 4);
}

static bool set_level(uint8_t level)
{
    const struct rf215_api *api = radio->api;
    uint8_t pac = 0xAA;
    int ret = api->set_tx_power(radio, RF09, level);

    ret |= rf215_blk_read(radio, REG_RF09_PAC, &pac, 1);
    return ret == 0 && pac == ((RF215_PACUR_NO_RED << 5) | level);
}

static bool setup(uint8_t cause)
{
    const struct rf215_api *api = radio->api;
    uint8_t pc = 0xAA;
    int ret = api->set_sleep(radio, RF24, 1); /* RF24 isn't used. This is the app's call, not the driver's */

    ret |= api->bb_enable(radio, RF09, 1);
    ret |= rf215_blk_read(radio, REG_BBC0_PC, &pc, 1);

    bool pass = ret == 0 && (pc & ~PC_FCSOK) == PC_EXPECTED;

    printk("setup %s: ret=%d pc=0x%02x rcause=0x%02x call=%s\n", pass ? "PASS" : "FAIL", ret, pc,
           cause, CALL_SIGN);
    return pass;
}

int main(void)
{
    const struct rf215_api *api = radio->api;
    uint8_t cause = PM->RCAUSE.reg;
    uint32_t counter = 0, tx_errors = 0;

    if (!device_is_ready(radio)) {
        printk("AT86RF215 not ready, driver init failed\n");
        return 0;
    }
    if (!setup(cause)) {
        printk("not transmitting\n");
        return 0;
    }

    int64_t next = k_uptime_get();

    while (1) {
        uint8_t level = level_for(counter);
        bool held = set_level(level);
        int ret = 0;

        if (held) {
            build_frame(frame, counter, k_uptime_get_32(), cause, level, tx_errors);
            ret = api->radio_tx(radio, RF09, frame, sizeof(frame), FRAME_TIMEOUT_MS);
        }

        int off = api->set_sleep(radio, RF09, 0); /* en = 0 is the TRXOFF command */

        k_msleep(1);

        uint8_t state = 0xAA;
        int read = rf215_blk_read(radio, REG_RF09_STATE, &state, 1);
        bool pass = held && ret == 0 && off == 0 && read == 0 && state == STATE_RF_TRXOFF;

        tx_errors += !pass;
        printk("[%u] pwr=%u frame %s: held=%d ret=%d state=0x%02x tx_errors=%u\n", counter, level,
               pass ? "PASS" : "FAIL", held, ret, state, tx_errors);
        counter++;

        next += FRAME_PERIOD_MS;
        k_sleep(K_TIMEOUT_ABS_MS(next));
    }

    return 0;
}
