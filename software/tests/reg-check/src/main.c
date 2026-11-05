/*
 * Hardware test: does the chip hold the register values the driver's init means
 * to leave there, and do basic register accesses behave? This is the gate test
 * for Phase 1 and grows with it.
 *
 * Console is 115200 8N1. The radio is never told to transmit: CNL only takes
 * effect once CNM is written (section 6.3.3.4), and nothing here writes CNM.
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
 *         is left on 915 MHz. Nothing starts the PLL.
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

    /* The values the driver will really use: 915 MHz, 200 kHz, default LBW */
    conf.cs = 200000;
    conf.lbw = RF215_PLL_LBW_DEFAULT;
    const uint8_t rf09_final[6] = {0x08, 0xF8, 0x8E, 0x00, 0x00, 0x08};

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

    while (1) {
        if (!device_is_ready(radio)) {
            printk("AT86RF215 not ready, driver init failed\n");
        } else {
            bool pass = true;

            round++;
            pass &= test_id();
            pass &= test_block();
            pass &= test_state();
            pass &= test_iqifc1();
            pass &= test_common();
            pass &= test_write();
            pass &= test_conf();
            pass &= test_pin();
            clean += pass;
            printk("[%u] totals: clean rounds %u/%u\n", round, clean, round);
        }
        k_msleep(1000);
    }

    return 0;
}
