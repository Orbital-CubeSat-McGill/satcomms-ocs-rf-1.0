#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <ocs/mcp251863_probe.h>

#define READ_CRC 0xb0U
#define CRC_POLY 0x8005U
#define CRC_SEED 0xffffU
#define REG_CICON 0x000U
#define REG_OSC 0xe00U
#define REG_DEVID 0xe14U
#define OSC_READY 0x400U
#define OSC_DISABLED 0x4U
#define PLL_ENABLED 0x1U
#define PLL_READY 0x100U

static int read_status_word(const struct mcp251863_probe_bus *bus,
                            uint16_t address, uint32_t *value)
{
    /* Two command/address bytes, byte count, four data bytes, two CRC bytes. */
    uint8_t tx[9] = {READ_CRC | (uint8_t)(address >> 8), (uint8_t)address, 4};
    uint8_t rx[9] = {0};
    int result = bus->transfer(bus->ctx, tx, rx, sizeof(tx));
    if (result != 0) { return result; }

    uint16_t crc = crc16(CRC_POLY, CRC_SEED, tx, 3);
    /* Including the received CRC yields zero only for a valid response. */
    if (crc16(CRC_POLY, crc, rx + 3, 6) != 0) { return -EBADMSG; }
    *value = sys_get_le32(rx + 3);
    return 0;
}

int mcp251863_probe_read(const struct mcp251863_probe_bus *bus,
                        struct mcp251863_probe_snapshot *snapshot)
{
    if (bus == NULL || bus->transfer == NULL || snapshot == NULL) { return -EINVAL; }
    struct mcp251863_probe_snapshot result;
    int error = read_status_word(bus, REG_CICON, &result.con);
    if (error == 0) { error = read_status_word(bus, REG_OSC, &result.osc); }
    if (error == 0) { error = read_status_word(bus, REG_DEVID, &result.devid); }
    if (error == 0) { *snapshot = result; }
    return error;
}

bool mcp251863_probe_clock_ready(const struct mcp251863_probe_snapshot *snapshot)
{
    if (snapshot == NULL) { return false; }
    uint32_t osc = snapshot->osc;
    if ((osc & OSC_READY) == 0 || (osc & OSC_DISABLED) != 0) { return false; }
    return (osc & PLL_ENABLED) == 0 || (osc & PLL_READY) != 0;
}
