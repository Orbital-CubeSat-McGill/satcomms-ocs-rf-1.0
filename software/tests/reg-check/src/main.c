/*
 * Hardware test: does the chip hold the register values the driver's init means
 * to leave there, and do basic register accesses behave? This is the gate test
 * for Phase 1 and grows with it.
 *
 * Console is 115200 8N1. The radio is never told to transmit, and nothing here
 * starts the PLL: channel registers only set where the radio would tune.
 *
 * id:     RF_PN and RF_VN read one at a time. Part must be 0x34, version 0x03.
 * block:  RF_PN and RF_VN read as one block must match the single reads.
 * state:  both radios must be in TRXOFF.
 * iqifc1: CHPM (bits 6:4) must be the driver's chip mode and SKEWDRV (bits 1:0)
 *         must keep its reset value of 2 (section 4.5.8.2). FAILSF (bit 7) is
 *         read-only status, so it is printed but not checked.
 * common: RF_CFG, RF_CLKO, RF_XOC and both PADFE must hold what init writes.
 *         These all equal their reset values, so a pass alone doesn't prove the
 *         writes happened.
 * write:  RF09_CNL must read back two patterns, then its old value is restored.
 * conf:   radio_conf must write what the datasheet's channel scheme says, shift
 *         LBW into bits 5:4, keep PLL bit 3, clear the channel and leave RF24's
 *         PLL alone. Bad arguments must be refused with no register changed. RF09
 *         is left on 905 MHz. Nothing starts the PLL.
 * front:  both radios' frontends must hold preset 0, from tables 6-51, 6-54, and 6-62
 *         for RF09 or 6-63 for RF24. AGCC's other bits must keep their reset values
 *         (0x81). AGCS.GCW is live gain, so it is masked. Only AGCC's expected value
 *         equals its reset value. RF24 is applied here, not by init.
 * boot:   RF24 and BBC1 must be on reset values at boot: the driver leaves RF24 to
 *         the application.
 * wake:   RF24 must leave SLEEP with a WAKEUP event, in TRXOFF with its registers
 *         back on reset values, even though the last round configured it.
 * sleep:  SLEEP on RF24 must leave RF09 in TRXOFF and configured. set_channel must
 *         refuse the sleeping radio, whose state is neither TRXOFF nor TXPREP. main
 *         keeps RF24 asleep outside the tests that use it, as the application will.
 * txprep: with RF24 asleep, RF09 must reach TXPREP with a TRXRDY event and a locked
 *         PLL, refuse radio_conf and select_preset, relock on a channel change, then
 *         return to TRXOFF. TXPREP doesn't transmit: the analog transmitter is still
 *         disabled (section 5.2.7).
 * freq:   RF09 must be on the devicetree's 905 MHz and 200 kHz, channel 0, default
 *         LBW. Read once at boot, before any test here writes these registers, so
 *         it shows what init did. CCF0 differs from reset (902.2 MHz).
 * chan:   set_channel must take the whole 9-bit range and write the high bit to
 *         CNM, refuse anything over 9 bits, and refuse a frequency that leaves the
 *         chip's band or the base frequency's range, changing nothing.
 * preset: RF09's baseband registers must hold preset 0, which init applies. FCSOK
 *         (PC bit 5) is read-only status, so it is masked. FSKC0 and FSKPHRTX
 *         differ from their reset values, so a pass does prove the writes happened.
 * pin:    PA20 must be low, nothing pending. A ready device has already seen
 *         WAKEUP: init fails without it.
 *
 * "not ready" means init failed. That is the expected result with the hat off.
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "rf215.h"
#include "rf215_internal.h"
#include "rf215_regs.h"

#define IQIFC1_FAILSF    BIT(7)
#define IQIFC1_SKEW_INIT 0x02
#define PART_EXPECTED    0x34
#define VERSION_EXPECTED 0x03

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(at86rf215));

static bool test_id(void)
{
    uint8_t part = 0xAA, version = 0xAA;
    int ret = rf215_blk_read(radio, REG_RF_PN, &part, 1);

    ret |= rf215_blk_read(radio, REG_RF_VN, &version, 1);

    bool pass = ret == 0 && part == PART_EXPECTED && version == VERSION_EXPECTED;

    printk("id     %s: ret=%d part=0x%02x version=0x%02x\n",
           pass ? "PASS" : "FAIL", ret, part, version);
    return pass;
}

static bool test_block(void)
{
    uint8_t single[2] = {0xAA, 0xAA}, block[2] = {0x55, 0x55};
    int ret = rf215_blk_read(radio, REG_RF_PN, &single[0], 1);

    ret |= rf215_blk_read(radio, REG_RF_VN, &single[1], 1);
    ret |= rf215_blk_read(radio, REG_RF_PN, block, sizeof(block));

    bool pass = ret == 0 && block[0] == single[0] && block[1] == single[1];

    printk("block  %s: ret=%d single=%02x %02x block=%02x %02x\n",
           pass ? "PASS" : "FAIL", ret, single[0], single[1], block[0], block[1]);
    return pass;
}

static bool test_state(void)
{
    uint8_t rf09 = 0xAA, rf24 = 0xAA;
    int ret = rf215_blk_read(radio, REG_RF09_STATE, &rf09, 1);

    ret |= rf215_blk_read(radio, REG_RF24_STATE, &rf24, 1);

    bool pass = ret == 0 && rf09 == STATE_RF_TRXOFF && rf24 == STATE_RF_TRXOFF;

    printk("state  %s: ret=%d rf09=0x%02x rf24=0x%02x\n",
           pass ? "PASS" : "FAIL", ret, rf09, rf24);
    return pass;
}

static bool test_iqifc1(void)
{
    const struct rf215_data *data = radio->data;
    const uint8_t expected = (data->op_mode << 4) | IQIFC1_SKEW_INIT;
    uint8_t val = 0xAA;
    int ret = rf215_blk_read(radio, REG_RF_IQIFC1, &val, 1);

    bool pass = ret == 0 && (val & ~IQIFC1_FAILSF) == expected;

    printk("iqifc1 %s: ret=%d val=0x%02x expected=0x%02x\n",
           pass ? "PASS" : "FAIL", ret, val, expected);
    return pass;
}

static bool test_common(void)
{
    static const struct {
        uint16_t reg;
        uint8_t expected;
    } regs[] = {
        { REG_RF_CFG, 0x09 },
        { REG_RF_CLKO, 0x09 },
        { REG_RF_XOC, 0x10 },
        { REG_RF09_PADFE, 0x00 },
        { REG_RF24_PADFE, 0x00 },
    };
    uint8_t val[ARRAY_SIZE(regs)];
    bool pass = true;

    for (size_t i = 0; i < ARRAY_SIZE(regs); i++) {
        val[i] = 0xAA;
        int ret = rf215_blk_read(radio, regs[i].reg, &val[i], 1);

        pass = pass && ret == 0 && val[i] == regs[i].expected;
    }

    printk("common %s: cfg=0x%02x clko=0x%02x xoc=0x%02x padfe09=0x%02x padfe24=0x%02x\n",
           pass ? "PASS" : "FAIL", val[0], val[1], val[2], val[3], val[4]);
    return pass;
}

static bool test_write(void)
{
    const uint8_t pattern[2] = {0x5A, 0xA5};
    uint8_t saved = 0xAA, back[2] = {0xAA, 0xAA};
    int ret = rf215_blk_read(radio, REG_RF09_CNL, &saved, 1);

    for (size_t i = 0; i < ARRAY_SIZE(pattern); i++) {
        ret |= rf215_blk_write(radio, REG_RF09_CNL, &pattern[i], 1);
        ret |= rf215_blk_read(radio, REG_RF09_CNL, &back[i], 1);
    }
    ret |= rf215_blk_write(radio, REG_RF09_CNL, &saved, 1);

    uint8_t restored = 0xAA;

    ret |= rf215_blk_read(radio, REG_RF09_CNL, &restored, 1);

    bool pass = ret == 0 && back[0] == pattern[0] && back[1] == pattern[1] &&
                restored == saved;

    printk("write  %s: ret=%d back=%02x %02x restored=0x%02x\n",
           pass ? "PASS" : "FAIL", ret, back[0], back[1], restored);
    return pass;
}

/* Reads CS, CCF0L, CCF0H, CNL, CNM, then PLL. Returns true on a match. */
static bool conf_regs_match(uint16_t cs_reg, uint16_t pll_reg, const uint8_t expected[6])
{
    uint8_t val[6];
    int ret = rf215_blk_read(radio, cs_reg, val, 5);

    ret |= rf215_blk_read(radio, pll_reg, &val[5], 1);

    bool pass = ret == 0 && memcmp(val, expected, sizeof(val)) == 0;

    printk("  regs%s: %02x %02x %02x %02x %02x pll=%02x\n", pass ? "" : " WRONG",
           val[0], val[1], val[2], val[3], val[4], val[5]);
    return pass;
}

static bool test_conf(void)
{
    const struct rf215_api *api = radio->api;
    const uint8_t stale_channel = 0x05;
    bool pass = true;

    /* Non-default spacing and LBW, over a stale channel, so a pass isn't just reset values */
    rf215_blk_write(radio, REG_RF09_CNL, &stale_channel, 1);
    struct rf215_radio_conf conf = {
        .cm = RF215_CM_IEEE, .cs = 400000, .base_freq = 915000000,
        .lbw = RF215_PLL_LBW_LARGER,
    };
    const uint8_t rf09_large[6] = {0x10, 0xF8, 0x8E, 0x00, 0x00, 0x28};

    pass &= api->radio_conf(radio, RF09, &conf) == 0;
    pass &= conf_regs_match(REG_RF09_CS, REG_RF09_PLL, rf09_large);

    /* 1.5 GHz offset. LBW is RF09 only, so RF24's PLL has to stay at its reset value */
    struct rf215_radio_conf conf24 = {
        .cm = RF215_CM_IEEE, .cs = 1000000, .base_freq = 2450000000U,
        .lbw = RF215_PLL_LBW_LARGER,
    };
    const uint8_t rf24_regs[6] = {0x28, 0x70, 0x94, 0x00, 0x00, 0x08};

    pass &= api->radio_conf(radio, RF24, &conf24) == 0;
    pass &= conf_regs_match(REG_RF24_CS, REG_RF24_PLL, rf24_regs);

    /* The values the driver will really use: 905 MHz, 200 kHz, default LBW */
    conf.cs = 200000;
    conf.base_freq = 905000000;
    conf.lbw = RF215_PLL_LBW_DEFAULT;
    const uint8_t rf09_final[6] = {0x08, 0x68, 0x8D, 0x00, 0x00, 0x08};

    pass &= api->radio_conf(radio, RF09, &conf) == 0;
    pass &= conf_regs_match(REG_RF09_CS, REG_RF09_PLL, rf09_final);

    /* Each of these changes one field, and has to be refused */
    const struct rf215_radio_conf bad[] = {
        { .cm = RF215_CM_IEEE, .cs = 200000, .base_freq = 600000000 },  /* between the bands */
        { .cm = RF215_CM_IEEE, .cs = 200000, .base_freq = 915000001 },  /* not a 25 kHz step */
        { .cm = RF215_CM_IEEE, .cs = 200001, .base_freq = 915000000 },  /* not a 25 kHz step */
        { .cm = RF215_CM_IEEE, .cs = 6400000, .base_freq = 915000000 }, /* doesn't fit CS */
        { .cm = RF215_CM_FINE_RES_09, .cs = 200000, .base_freq = 915000000 },
        { .cm = RF215_CM_IEEE, .cs = 200000, .base_freq = 915000000, .lbw = 3 },
    };
    int refused = 0;

    for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
        refused += api->radio_conf(radio, RF09, &bad[i]) != 0;
    }
    pass &= refused == ARRAY_SIZE(bad);
    pass &= conf_regs_match(REG_RF09_CS, REG_RF09_PLL, rf09_final);

    /* RF09's frequency isn't valid on RF24 */
    bool refused24 = api->radio_conf(radio, RF24, &conf) != 0;

    pass &= refused24;

    printk("conf   %s: refused=%d/%u rf24=%d\n", pass ? "PASS" : "FAIL",
           refused, (unsigned int)ARRAY_SIZE(bad), refused24);
    return pass;
}

static bool test_preset(void)
{
    /* 32-bit auto FCS. BBEN stays 0 and PT is MR-FSK */
    const uint8_t pc_expected = 0x11;
    /* FSKC0..FSKPHRTX: 2-FSK, h = 1, BT = 1, 50 kHz, PDT 5, SFD1 coded, 8 octet preamble */
    const uint8_t fsk_expected[11] = {
        0x56, 0x00, 0x60, 0x85, 0x08, 0x08, 0x09, 0x72, 0xF6, 0x72, 0x00,
    };
    /* FSKDM..FSKPE2: direct modulation on, pre-emphasis off */
    const uint8_t dm_expected[4] = {0x01, 0x00, 0x00, 0x00};
    uint8_t pc = 0xAA, fsk[11], dm[4];
    int ret = rf215_blk_read(radio, REG_BBC0_PC, &pc, 1);

    ret |= rf215_blk_read(radio, REG_BBC0_FSKC0, fsk, sizeof(fsk));
    ret |= rf215_blk_read(radio, REG_BBC0_FSKDM, dm, sizeof(dm));

    bool pass = ret == 0 && (pc & ~BIT(5)) == pc_expected &&
                memcmp(fsk, fsk_expected, sizeof(fsk)) == 0 &&
                memcmp(dm, dm_expected, sizeof(dm)) == 0;

    printk("preset %s: ret=%d pc=%02x fsk=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x dm=%02x\n",
           pass ? "PASS" : "FAIL", ret, pc, fsk[0], fsk[1], fsk[2], fsk[3], fsk[4], fsk[5],
           fsk[6], fsk[7], fsk[8], fsk[9], fsk[10], dm[0]);
    return pass;
}

/* Reads RXBWC, RXDFE, AGCC, AGCS, TXCUTC, TXDFE. Returns true on a match. */
static bool frontend_matches(const char *name, uint16_t reg_rxbwc, uint16_t reg_txcutc,
                             uint8_t rxbwc, uint8_t rxdfe)
{
    uint8_t rx[4] = {0}, tx[2] = {0};
    int ret = rf215_blk_read(radio, reg_rxbwc, rx, sizeof(rx));

    ret |= rf215_blk_read(radio, reg_txcutc, tx, sizeof(tx));

    bool pass = ret == 0 && rx[0] == rxbwc && rx[1] == rxdfe && rx[2] == 0x81 &&
                (rx[3] & 0xE0) == 0x20 && tx[0] == 0xC0 && tx[1] == 0x98;

    printk("  %s%s: rxbwc=%02x rxdfe=%02x agcc=%02x agcs=%02x txcutc=%02x txdfe=%02x\n", name,
           pass ? "" : " WRONG", rx[0], rx[1], rx[2], rx[3], tx[0], tx[1]);
    return pass;
}

static bool test_frontend(void)
{
    const struct rf215_api *api = radio->api;
    bool pass = frontend_matches("rf09", REG_RF09_RXBWC, REG_RF09_TXCUTC, 0x00, 0x2A);

    pass &= api->select_preset(radio, RF24, 0) == 0;
    pass &= frontend_matches("rf24", REG_RF24_RXBWC, REG_RF24_TXCUTC, 0x01, 0x2A);

    printk("front  %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

static bool test_freq(const uint8_t boot_regs[6])
{
    /* CS=200 kHz, CCF0=905 MHz (0x8D68 * 25 kHz), ch 0, default LBW */
    const uint8_t expected[6] = {0x08, 0x68, 0x8D, 0x00, 0x00, 0x08};
    bool pass = memcmp(boot_regs, expected, sizeof(expected)) == 0;

    printk("freq   %s: boot regs %02x %02x %02x %02x %02x pll=%02x\n", pass ? "PASS" : "FAIL",
           boot_regs[0], boot_regs[1], boot_regs[2], boot_regs[3], boot_regs[4], boot_regs[5]);
    return pass;
}

/* set_channel must accept ch and leave exactly these CNL and CNM values */
static bool channel_takes(rf215_radio_t which, uint16_t ch, uint8_t cnl, uint8_t cnm)
{
    const uint16_t reg_cnl = (which == RF09) ? REG_RF09_CNL : REG_RF24_CNL;
    uint8_t val[2] = {0xAA, 0xAA};
    int set = ((const struct rf215_api *)radio->api)->set_channel(radio, which, ch);
    int ret = rf215_blk_read(radio, reg_cnl, val, sizeof(val));
    bool pass = set == 0 && ret == 0 && val[0] == cnl && val[1] == cnm;

    if (!pass) {
        printk("  chan %u WRONG: set=%d ret=%d cnl=%02x cnm=%02x\n", ch, set, ret, val[0], val[1]);
    }
    return pass;
}

/* set_channel must refuse ch, and CNL and CNM must keep the values from the last accepted one */
static bool channel_refuses(rf215_radio_t which, uint16_t ch, uint8_t cnl, uint8_t cnm)
{
    const uint16_t reg_cnl = (which == RF09) ? REG_RF09_CNL : REG_RF24_CNL;
    uint8_t val[2] = {0xAA, 0xAA};
    int set = ((const struct rf215_api *)radio->api)->set_channel(radio, which, ch);
    int ret = rf215_blk_read(radio, reg_cnl, val, sizeof(val));
    bool pass = set != 0 && ret == 0 && val[0] == cnl && val[1] == cnm;

    if (!pass) {
        printk("  chan %u WRONG (expected refusal): set=%d ret=%d cnl=%02x cnm=%02x\n", ch, set, ret, val[0], val[1]);
    }
    return pass;
}

static bool test_channel(void)
{
    const struct rf215_api *api = radio->api;
    struct rf215_radio_conf conf = {
        .cm = RF215_CM_IEEE, .cs = 200000, .base_freq = 915000000,
        .lbw = RF215_PLL_LBW_DEFAULT,
    };
    bool pass = true;

    /* 915 MHz at 200 kHz: 9 bits never leaves the band, so only the limit applies */
    pass &= api->radio_conf(radio, RF09, &conf) == 0;
    pass &= channel_takes(RF09, 0x20, 0x20, 0x00);  /* the old limit refused this */
    pass &= channel_takes(RF09, 0x100, 0x00, 0x01); /* needs CNH */
    pass &= channel_takes(RF09, 0x1FF, 0xFF, 0x01); /* 1017.2 MHz */
    pass &= channel_refuses(RF09, 0x200, 0xFF, 0x01);

    /* Widest spacing, so channels can reach the band edge: 915 + 16 * 6.375 = 1017 MHz */
    conf.cs = 6375000;
    pass &= api->radio_conf(radio, RF09, &conf) == 0;
    pass &= channel_takes(RF09, 16, 16, 0x00);
    pass &= channel_refuses(RF09, 17, 16, 0x00);    /* 1023.375 MHz, over 1020 */
    pass &= channel_refuses(RF09, 0x1FF, 16, 0x00); /* 4172.6 MHz, far over the band */

    /* From the low range: 500 + 6.375 = 506.375 MHz */
    conf.base_freq = 500000000;
    pass &= api->radio_conf(radio, RF09, &conf) == 0;
    pass &= channel_takes(RF09, 1, 1, 0x00);
    pass &= channel_refuses(RF09, 2, 1, 0x00);      /* 512.75 MHz, between the ranges */
    pass &= channel_refuses(RF09, 50, 1, 0x00);     /* 818.75 MHz, valid but in the other range */

    /* RF24 has the 1.5 GHz offset: 2450 + 33 = 2483 MHz */
    struct rf215_radio_conf conf24 = {
        .cm = RF215_CM_IEEE, .cs = 1000000, .base_freq = 2450000000U,
        .lbw = RF215_PLL_LBW_DEFAULT,
    };

    pass &= api->radio_conf(radio, RF24, &conf24) == 0;
    pass &= channel_takes(RF24, 33, 33, 0x00);
    pass &= channel_refuses(RF24, 34, 33, 0x00);    /* 2484 MHz, over 2483.5 */

    /* Maximum CS and CN on RF24, about 5.7 GHz */
    conf24.cs = 6375000;
    pass &= api->radio_conf(radio, RF24, &conf24) == 0;
    pass &= channel_refuses(RF24, 0x1FF, 0x00, 0x00);

    /* Back to what the driver uses */
    conf.cs = 200000;
    conf.base_freq = 905000000;
    pass &= api->radio_conf(radio, RF09, &conf) == 0;

    printk("chan   %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

static bool test_boot(const uint8_t rf24_boot[6])
{
    /* RXBWC, RXDFE, AGCS, TXCUTC, TXDFE, then BBC1_PC */
    const uint8_t expected[6] = {0x0B, 0x01, 0x77, 0x08, 0x01, 0x54};
    bool pass = memcmp(rf24_boot, expected, sizeof(expected)) == 0;

    printk("boot   %s: rf24 %02x %02x %02x %02x %02x pc=%02x\n", pass ? "PASS" : "FAIL",
           rf24_boot[0], rf24_boot[1], rf24_boot[2], rf24_boot[3], rf24_boot[4], rf24_boot[5]);
    return pass;
}

static void rf24_read_boot(uint8_t val[6])
{
    uint8_t rx[4] = {0}, tx[2] = {0};

    rf215_blk_read(radio, REG_RF24_RXBWC, rx, sizeof(rx));
    rf215_blk_read(radio, REG_RF24_TXCUTC, tx, sizeof(tx));
    rf215_blk_read(radio, REG_BBC1_PC, &val[5], 1);
    val[0] = rx[0];
    val[1] = rx[1];
    val[2] = rx[3];
    val[3] = tx[0];
    val[4] = tx[1];
}

static bool test_wake(void)
{
    const struct rf215_api *api = radio->api;
    struct rf215_data *data = radio->data;

    k_event_clear(&data->events, RF24_EVT_WAKEUP);

    int ret = api->set_sleep(radio, RF24, 0);
    uint32_t events = k_event_wait(&data->events, RF24_EVT_WAKEUP, false, K_MSEC(100));
    uint8_t state = 0xAA, rxbwc = 0xAA;

    ret |= rf215_blk_read(radio, REG_RF24_STATE, &state, 1);
    ret |= rf215_blk_read(radio, REG_RF24_RXBWC, &rxbwc, 1);

    bool pass = ret == 0 && events != 0 && state == STATE_RF_TRXOFF && rxbwc == 0x0B;

    printk("wake   %s: ret=%d events=0x%03x state=0x%02x rxbwc=%02x\n", pass ? "PASS" : "FAIL",
           ret, events, state, rxbwc);
    return pass;
}

static bool test_sleep(void)
{
    const struct rf215_api *api = radio->api;
    int ret = api->set_sleep(radio, RF24, 1);
    uint8_t rf24 = 0xAA, rf09 = 0xAA, rxbwc = 0xAA;

    k_msleep(1);
    ret |= rf215_blk_read(radio, REG_RF24_STATE, &rf24, 1);
    ret |= rf215_blk_read(radio, REG_RF09_STATE, &rf09, 1);
    ret |= rf215_blk_read(radio, REG_RF09_RXBWC, &rxbwc, 1);

    int chan_ret = api->set_channel(radio, RF24, 0);
    bool pass = ret == 0 && rf24 != STATE_RF_TRXOFF && rf09 == STATE_RF_TRXOFF && rxbwc == 0x00 &&
                chan_ret == RF215_INVAL_CONF;

    printk("sleep  %s: ret=%d rf24=0x%02x rf09=0x%02x rf09 rxbwc=%02x chan=%d\n", pass ? "PASS" : "FAIL",
           ret, rf24, rf09, rxbwc, chan_ret);
    return pass;
}

/* Waits for the PLL's TRXRDY on RF09, which channel changes trigger as well */
static bool rf09_wait_ready(void)
{
    struct rf215_data *data = radio->data;
    uint32_t events = k_event_wait(&data->events, RF09_EVT_TRXRDY | RF09_EVT_ERROR, false,
                                   K_MSEC(100));
    uint8_t lock = 0;
    int ret = ((const struct rf215_api *)radio->api)->get_pll_ls(radio, &lock, RF09);
    bool pass = ret == 0 && (events & RF09_EVT_TRXRDY) && !(events & RF09_EVT_ERROR) && lock == 1;

    if (!pass) {
        printk("  pll WRONG: ret=%d events=0x%03x lock=%u\n", ret, events, lock);
    }
    return pass;
}

static bool test_txprep(void)
{
    const struct rf215_api *api = radio->api;
    struct rf215_data *data = radio->data;
    const uint32_t ready = RF09_EVT_TRXRDY | RF09_EVT_ERROR;
    const uint8_t txprep = CMD_TXPREP, trxoff = CMD_TRXOFF;
    const struct rf215_radio_conf conf = {
        .cm = RF215_CM_IEEE, .cs = 400000, .base_freq = 905000000,
        .lbw = RF215_PLL_LBW_DEFAULT,
    };
    uint8_t state = 0xAA, cs = 0xAA, lock = 0xAA;
    bool pass = true;

    k_event_clear(&data->events, ready);
    pass &= rf215_blk_write(radio, REG_RF09_CMD, &txprep, 1) == 0;
    pass &= rf09_wait_ready();
    pass &= rf215_blk_read(radio, REG_RF09_STATE, &state, 1) == 0 && state == STATE_RF_TXPREP;

    int conf_ret = api->radio_conf(radio, RF09, &conf);
    int preset_ret = api->select_preset(radio, RF09, 0);

    pass &= conf_ret == RF215_INVAL_CONF && preset_ret == RF215_INVAL_CONF;
    pass &= rf215_blk_read(radio, REG_RF09_CS, &cs, 1) == 0 && cs == 0x08;

    /* 905.2 MHz, then back. Writing CNM makes the PLL lock again (section 6.3.1) */
    k_event_clear(&data->events, ready);
    int chan_ret = api->set_channel(radio, RF09, 1);

    pass &= chan_ret == 0 && rf09_wait_ready();
    k_event_clear(&data->events, ready);
    chan_ret = api->set_channel(radio, RF09, 0);
    pass &= chan_ret == 0 && rf09_wait_ready();

    pass &= rf215_blk_write(radio, REG_RF09_CMD, &trxoff, 1) == 0;
    k_msleep(1);

    uint8_t off = 0xAA;

    pass &= rf215_blk_read(radio, REG_RF09_STATE, &off, 1) == 0 && off == STATE_RF_TRXOFF;
    pass &= api->get_pll_ls(radio, &lock, RF09) == 0;

    printk("txprep %s: state=0x%02x conf=%d preset=%d cs=%02x off=0x%02x lock after off=%u\n",
           pass ? "PASS" : "FAIL", state, conf_ret, preset_ret, cs, off, lock);
    return pass;
}

static bool test_pin(void)
{
    const struct rf215_config *cfg = radio->config;
    int pin = gpio_pin_get_dt(&cfg->rf215_irq);

    bool pass = pin == 0;

    printk("pin    %s: pin=%d\n", pass ? "PASS" : "FAIL", pin);
    return pass;
}

int main(void)
{
    unsigned int round = 0, clean = 0;
    uint8_t boot_regs[6] = {0}, rf24_boot[6] = {0};

    /* Before test_conf and test_channel write these. Failures show up in test_freq */
    if (device_is_ready(radio)) {
        rf215_blk_read(radio, REG_RF09_CS, boot_regs, 5);
        rf215_blk_read(radio, REG_RF09_PLL, &boot_regs[5], 1);
        rf24_read_boot(rf24_boot);
        /* Stands in for the application, which decides what RF24 does */
        ((const struct rf215_api *)radio->api)->set_sleep(radio, RF24, 1);
        k_msleep(1);
    }

    while (1) {
        if (!device_is_ready(radio)) {
            printk("AT86RF215 not ready, driver init failed\n");
        } else {
            bool pass = true;

            round++;
            pass &= test_wake();
            pass &= test_id();
            pass &= test_block();
            pass &= test_state();
            pass &= test_iqifc1();
            pass &= test_common();
            pass &= test_write();
            pass &= test_conf();
            pass &= test_preset();
            pass &= test_boot(rf24_boot);
            pass &= test_frontend();
            pass &= test_freq(boot_regs);
            pass &= test_channel();
            pass &= test_sleep();
            pass &= test_txprep();
            pass &= test_pin();
            clean += pass;
            printk("[%u] totals: clean rounds %u/%u\n", round, clean, round);
        }
        k_msleep(1000);
    }

    return 0;
}
