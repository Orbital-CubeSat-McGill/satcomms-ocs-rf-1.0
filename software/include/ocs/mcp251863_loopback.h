#pragma once
#include <ocs/mcp251863_probe.h>

struct mcp251863_loopback_io {
    struct mcp251863_probe_bus bus;
    void *gpio_ctx;
    int (*standby_high)(void *ctx);
    int (*interrupt_active)(void *ctx);
};

struct mcp251863_loopback_result {
    const char *stage;
    uint16_t id;
    uint8_t length;
    uint8_t data[8];
    bool irq_asserted;
    bool irq_cleared;
    int cleanup_error;
};

/* Exclusive bench diagnostic, not a general CAN driver. Requires configuration
 * mode and STBY high. Resets/configures U4, verifies internal mode 2, sends one
 * classic frame (ID 0x123, eight bytes "CAN TEST"), validates RX/IRQ, and returns
 * to configuration mode. Never requests normal or external loopback mode.
 * No other task/driver may use this bus/device concurrently.
 * Result contains the last stage and cleanup error even on failure; received
 * bytes are read from RX RAM, never prefilled with the expected payload.
 */
int mcp251863_loopback_run(const struct mcp251863_loopback_io *io,
                          struct mcp251863_loopback_result *result,
                          int32_t timeout_ms);
