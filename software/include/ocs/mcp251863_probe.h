#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Read-only diagnostic transport, separate from the AT86RF215 SPI protocol. */
struct mcp251863_probe_bus {
    void *ctx;
    int (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t length);
};

struct mcp251863_probe_snapshot {
    uint32_t con;
    uint32_t osc;
    uint32_t devid;
};

/* Read only CiCON, OSC and DEVID using READ_CRC. No writes or reset commands.
 * The caller owns/serializes the bus. Output stays unchanged on failure.
 * Returns 0, -EINVAL, -EBADMSG for CRC errors, or a transport error.
 * DEVID can legitimately contain zero; it is not the radio's PN register.
 */
int mcp251863_probe_read(const struct mcp251863_probe_bus *bus,
                        struct mcp251863_probe_snapshot *snapshot);

/* Check oscillator enabled/ready, and PLL ready only when PLL is enabled. */
bool mcp251863_probe_clock_ready(const struct mcp251863_probe_snapshot *snapshot);
