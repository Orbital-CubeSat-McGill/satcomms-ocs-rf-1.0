#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <ocs/mcp251863_loopback.h>

/* MCP251863 datasheet section 4; FIFO1 is TX, FIFO2 is RX. */
#define CON 0x000U
#define NBTCFG 0x004U
#define INT 0x01cU
#define TREC 0x034U
#define TXCON 0x05cU
#define TXSTA 0x060U
#define TXUA 0x064U
#define RXCON 0x068U
#define RXSTA 0x06cU
#define RXUA 0x070U
#define FLTCON 0x1d0U
#define FLTOBJ0 0x1f0U
#define MASK0 0x1f4U
#define CRCSTAT 0xe08U
#define RAM_START 0x400U
#define RAM_END 0xc00U
#define FRAME_ID 0x123U
#define MODE_CONFIG 4U
#define MODE_INTERNAL 2U

struct session {
    const struct mcp251863_loopback_io *io;
    int64_t deadline;
    bool cleanup;
};

static int guard(const struct session *s)
{
    int level = s->io->standby_high(s->io->gpio_ctx);
    return level < 0 ? level : level == 0 ? -EBUSY : 0;
}

static int exchange(struct session *s, const uint8_t *tx, uint8_t *rx, size_t n)
{
    if (k_uptime_get() >= s->deadline) { return -ETIMEDOUT; }
    return s->io->bus.transfer(s->io->bus.ctx, tx, rx, n);
}

static int read_bytes(struct session *s, uint16_t address, uint8_t *data, size_t n)
{
    bool ram = address >= RAM_START && address < RAM_END;
    if (n == 0 || n > 32 || (ram && ((address % 4) || (n % 4) || address + n > RAM_END))) {
        return -EINVAL;
    }
    uint8_t tx[37] = {0xb0U | (uint8_t)(address >> 8), (uint8_t)address,
                      (uint8_t)(ram ? n / 4 : n)};
    uint8_t rx[37] = {0};
    int ret = exchange(s, tx, rx, n + 5);
    if (ret != 0) { return ret; }
    uint16_t crc = crc16(0x8005, 0xffff, tx, 3);
    if (crc16(0x8005, crc, rx + 3, n + 2) != 0) { return -EBADMSG; }
    memcpy(data, rx + 3, n);
    return 0;
}

static int read_word(struct session *s, uint16_t address, uint32_t *value)
{
    uint8_t bytes[4];
    int ret = read_bytes(s, address, bytes, sizeof(bytes));
    if (ret == 0) { *value = sys_get_le32(bytes); }
    return ret;
}

/* WRITE_SAFE checks the command/address/data CRC before updating one SFR byte.
 * Cleanup is allowed to request Configuration mode even if STBY fell low.
 */
static int write_byte(struct session *s, uint16_t address, uint8_t value)
{
    int ret = s->cleanup ? 0 : guard(s);
    if (ret != 0) { return ret; }
    uint8_t tx[5] = {0xc0U | (uint8_t)(address >> 8), (uint8_t)address, value};
    uint8_t rx[5] = {0};
    sys_put_be16(crc16(0x8005, 0xffff, tx, 3), tx + 3);
    return exchange(s, tx, rx, sizeof(tx));
}

static int write_word(struct session *s, uint16_t address, uint32_t value)
{
    for (unsigned int i = 0; i < 4; i++) {
        int ret = write_byte(s, address + i, (uint8_t)(value >> (8 * i)));
        if (ret != 0) { return ret; }
    }
    return 0;
}

static int write_ram(struct session *s, uint16_t address, const uint8_t *data, size_t n)
{
    if (address < RAM_START || address + n > RAM_END || address % 4 ||
        n == 0 || n > 32 || n % 4) { return -EINVAL; }
    int ret = guard(s);
    if (ret != 0) { return ret; }
    uint8_t tx[37] = {0xa0U | (uint8_t)(address >> 8), (uint8_t)address, (uint8_t)(n / 4)};
    uint8_t rx[37] = {0};
    memcpy(tx + 3, data, n);
    sys_put_be16(crc16(0x8005, 0xffff, tx, n + 3), tx + n + 3);
    return exchange(s, tx, rx, n + 5);
}

static int wait_word(struct session *s, uint16_t address, uint32_t mask, uint32_t wanted)
{
    for (;;) {
        int ret = s->cleanup ? 0 : guard(s);
        uint32_t value;
        if (ret == 0) { ret = read_word(s, address, &value); }
        if (ret != 0) { return ret; }
        if ((value & mask) == wanted) { return 0; }
        k_msleep(1);
    }
}

static int fifo_address(struct session *s, uint16_t reg, uint16_t *address)
{
    uint32_t offset;
    int ret = read_word(s, reg, &offset);
    if (ret != 0) { return ret; }
    if (offset % 4 || offset > 2048U - 16U) { return -ERANGE; }
    *address = RAM_START + offset;
    return 0;
}

static int cleanup(struct session *s)
{
    s->cleanup = true;
    s->deadline = k_uptime_get() + 200;
    int first = write_byte(s, INT + 2, 0);
    int ret = write_byte(s, INT + 3, 0);
    if (first == 0) { first = ret; }
    ret = write_byte(s, CON + 3, MODE_CONFIG);
    if (first == 0) { first = ret; }
    ret = wait_word(s, CON, 7U << 21, MODE_CONFIG << 21);
    if (first == 0) { first = ret; }
    /* SPI success alone does not prove a WRITE_SAFE was accepted by U4. */
    uint32_t interrupts;
    ret = read_word(s, INT, &interrupts);
    if (ret == 0 && (interrupts >> 16) != 0) { ret = -EIO; }
    return first != 0 ? first : ret;
}

int mcp251863_loopback_run(const struct mcp251863_loopback_io *io,
                          struct mcp251863_loopback_result *out,
                          int32_t timeout_ms)
{
    if (io == NULL || out == NULL || io->bus.transfer == NULL ||
        io->standby_high == NULL || io->interrupt_active == NULL || timeout_ms <= 0) {
        return -EINVAL;
    }
    *out = (struct mcp251863_loopback_result){.stage = "preflight"};
    struct session s = {.io = io, .deadline = k_uptime_get() + timeout_ms};
    struct mcp251863_probe_snapshot snapshot;
    int ret = guard(&s);
    if (ret == 0) { ret = mcp251863_probe_read(&io->bus, &snapshot); }
    if (ret != 0) { return ret; }
    if (((snapshot.con >> 21) & 7U) != MODE_CONFIG) { return -EBUSY; }
    if (!mcp251863_probe_clock_ready(&snapshot)) { return -EAGAIN; }
    if ((snapshot.osc & 1U) != 0) { return -ENOTSUP; } /* This board uses a 40 MHz crystal. */

    bool touched = false;
    uint32_t word;
    uint16_t tx_address, rx_address;
    uint8_t object[16] = {0}, received[16];
#define STEP(label_, call_) do { out->stage = label_; ret = (call_); if (ret != 0) { goto done; } } while (0)
    out->stage = "reset in configuration mode";
    ret = guard(&s);
    if (ret != 0) { return ret; }
    const uint8_t reset[2] = {0}; uint8_t ignored[2];
    touched = true; /* Even a failed transfer can have partially reached U4. */
    STEP("reset", exchange(&s, reset, ignored, sizeof(reset)));
    k_msleep(5);
    STEP("clock after reset", mcp251863_probe_read(&io->bus, &snapshot));
    if (!mcp251863_probe_clock_ready(&snapshot)) { ret = -EAGAIN; goto done; }
    if ((snapshot.osc & 1U) != 0) { ret = -ENOTSUP; goto done; }

    /* Configure only while OPMOD=4; disable TXQ/TEF and all interrupt sources.
     * NBTCFG: 40 MHz/(1*(1+63+16))=500 kb/s; halve TQ counts for SYSCLK/2.
     */
    STEP("configuration state", wait_word(&s, CON, 7U << 21, MODE_CONFIG << 21));
    STEP("controller configuration", write_word(&s, CON, 0x04000760));
    STEP("interrupt disable", write_word(&s, INT, 0));
    uint32_t timing = snapshot.osc & 0x10U ? 0x001e0707U : 0x003e0f0fU;
    STEP("bit timing", write_word(&s, NBTCFG, timing));
    STEP("bit timing readback", read_word(&s, NBTCFG, &word));
    if (word != timing) { ret = -EIO; goto done; }
    for (unsigned int i = 0; i < 32; i++) {
        STEP("filter disable", write_byte(&s, FLTCON + i, 0));
    }
    const uint8_t zeros[32] = {0};
    for (uint16_t address = RAM_START; address < RAM_END; address += sizeof(zeros)) {
        STEP("message RAM initialization", write_ram(&s, address, zeros, sizeof(zeros)));
    }
    STEP("TX FIFO configuration", write_word(&s, TXCON, 0x480));
    STEP("RX FIFO configuration", write_word(&s, RXCON, 0x401));
    STEP("filter ID", write_word(&s, FLTOBJ0, FRAME_ID));
    STEP("filter mask", write_word(&s, MASK0, 0x400007ff));
    STEP("filter destination", write_byte(&s, FLTCON, 0x82));
    STEP("receive interrupt enable", write_word(&s, INT, 0x00020000));
    STEP("enter internal loopback", write_byte(&s, CON + 3, MODE_INTERNAL));
    STEP("verify OPMOD=2", wait_word(&s, CON, 7U << 21, MODE_INTERNAL << 21));
    STEP("TX FIFO reset complete", wait_word(&s, TXCON, 1U << 10, 0));
    STEP("RX FIFO reset complete", wait_word(&s, RXCON, 1U << 10, 0));
    STEP("initial RX FIFO empty", read_word(&s, RXSTA, &word));
    if (word & 1U) { ret = -EIO; goto done; }
    ret = io->interrupt_active(io->gpio_ctx);
    if (ret != 0) { ret = ret < 0 ? ret : -EIO; out->stage = "initial INT inactive"; goto done; }
    STEP("TX FIFO user address", fifo_address(&s, TXUA, &tx_address));
    STEP("RX FIFO user address", fifo_address(&s, RXUA, &rx_address));
    if (tx_address < rx_address + 16U && rx_address < tx_address + 16U) {
        ret = -ERANGE; goto done;
    }
    STEP("TX FIFO space", wait_word(&s, TXSTA, 1U, 1U));
    sys_put_le32(FRAME_ID, object);
    sys_put_le32(8, object + 4); /* DLC=8, IDE/RTR/FDF/BRS=0. */
    memcpy(object + 8, "CAN TEST", 8);
    STEP("TX object write", write_ram(&s, tx_address, object, sizeof(object)));
    STEP("TX object readback", read_bytes(&s, tx_address, received, sizeof(received)));
    if (memcmp(object, received, sizeof(object)) != 0) { ret = -EIO; goto done; }
    STEP("SPI write CRC status", read_word(&s, CRCSTAT, &word));
    if (word & 0x30000U) { ret = -EBADMSG; goto done; }
    /* A fresh read must still confirm INTERNAL mode immediately before TXREQ. */
    STEP("mode before TXREQ", read_word(&s, CON, &word));
    if (((word >> 21) & 7U) != MODE_INTERNAL) { ret = -EIO; goto done; }
    STEP("internal TX request", write_byte(&s, TXCON + 1, 3)); /* UINC + TXREQ. */
    STEP("TX completion", wait_word(&s, TXCON, 1U << 9, 0));
    STEP("RX completion", wait_word(&s, RXSTA, 1U, 1U));
    STEP("RX interrupt status", read_word(&s, INT, &word));
    if ((word & 2U) == 0) { ret = -EIO; goto done; }
    ret = io->interrupt_active(io->gpio_ctx);
    if (ret != 1) { ret = ret < 0 ? ret : -EIO; out->stage = "INT asserted"; goto done; }
    out->irq_asserted = true;
    STEP("received object", read_bytes(&s, rx_address, received, sizeof(received)));
    out->id = sys_get_le32(received) & 0x7ffU;
    out->length = received[4] & 15U;
    memcpy(out->data, received + 8, 8);
    /* IDE=0: only SID[10:0] carries the identifier; ignore unused EID bits. */
    if (out->id != FRAME_ID ||
        (sys_get_le32(received + 4) & 0x1ffU) != 8 || memcmp(out->data, "CAN TEST", 8) != 0) {
        ret = -EBADMSG; goto done;
    }
    STEP("RX FIFO release", write_byte(&s, RXCON + 1, 1));
    STEP("RX FIFO empty", wait_word(&s, RXSTA, 1U, 0));
    ret = io->interrupt_active(io->gpio_ctx);
    if (ret != 0) { ret = ret < 0 ? ret : -EIO; out->stage = "INT cleared"; goto done; }
    out->irq_cleared = true;
    STEP("TX FIFO errors", read_word(&s, TXSTA, &word));
    if (word & 0xf0U) { ret = -EIO; goto done; }
    STEP("CAN error counters", read_word(&s, TREC, &word));
    if (word & 0x3fffffU) { ret = -EIO; goto done; }
    STEP("final standby check", guard(&s));
done:
    if (touched) { out->cleanup_error = cleanup(&s); }
    if (ret == 0 && out->cleanup_error != 0) { out->stage = "cleanup"; ret = out->cleanup_error; }
    if (ret == 0) { out->stage = "complete"; }
    return ret;
#undef STEP
}
