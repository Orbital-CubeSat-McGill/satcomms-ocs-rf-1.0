#include <errno.h>
#include <stdbool.h>

#include <zephyr/kernel.h>

#include <ocs/at86rf215_zephyr.h>
#include <ocs/radio.h>
#include "at86rf215_packet.h"

BUILD_ASSERT(OCS_RADIO_MAX_TRANSFER == AT86RF215_IO_MAX);

static struct at86rf215 radio;
static struct at86rf215_packet packet = {.radio = &radio};
static bool initialized;
K_MUTEX_DEFINE(radio_mutex);

static int lock_radio(void)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }

    return k_mutex_lock(&radio_mutex, K_FOREVER);
}

int ocs_radio_init(void)
{
    int result = lock_radio();

    if (result != 0) {
        return result;
    }

    /* Never retain a usable connection after a failed reinitialization. */
    initialized = false;
    packet.configured = false;
    radio = (struct at86rf215){0};
    result = at86rf215_zephyr_init(&radio);

    if (result == 0) {
        uint8_t part_number;

        result = at86rf215_read_reg(&radio, OCS_RADIO_REG_PN, &part_number);

        if (result == 0 && part_number != 0x34) {
            result = -ENODEV;
        }
    }

    initialized = (result == 0);
    k_mutex_unlock(&radio_mutex);
    return result;
}

int ocs_radio_read(uint16_t address, void *data, size_t length)
{
    int result = lock_radio();

    if (result != 0) {
        return result;
    }

    result = initialized
        ? at86rf215_read_block(&radio, address, data, length)
        : -ENODEV;

    k_mutex_unlock(&radio_mutex);
    return result;
}

int ocs_radio_write(uint16_t address, const void *data, size_t length)
{
    int result = lock_radio();

    if (result != 0) {
        return result;
    }

    /* Raw writes may change PHY/state registers or partly reach the device. */
    packet.configured = false;
    result = initialized
        ? at86rf215_write_block(&radio, address, data, length)
        : -ENODEV;

    k_mutex_unlock(&radio_mutex);
    return result;
}

int ocs_radio_read_reg(uint16_t address, uint8_t *value)
{
    return ocs_radio_read(address, value, 1);
}

int ocs_radio_write_reg(uint16_t address, uint8_t value)
{
    return ocs_radio_write(address, &value, 1);
}

int ocs_radio_configure(const struct ocs_radio_config *config)
{
    int result = lock_radio();
    if (result != 0) { return result; }
    result = initialized ? at86rf215_packet_configure(&packet, config) : -ENODEV;
    k_mutex_unlock(&radio_mutex);
    return result;
}

static int lock_packet(int32_t timeout_ms, int64_t *deadline)
{
    if (k_is_in_isr()) { return -EWOULDBLOCK; }
    if (timeout_ms <= 0) { return -EINVAL; }
    *deadline = k_uptime_get() + timeout_ms;
    int result = k_mutex_lock(&radio_mutex, K_MSEC(timeout_ms));
    return result == -EAGAIN ? -ETIMEDOUT : result;
}

int ocs_radio_send(const void *data, size_t length, int32_t timeout_ms)
{
    int64_t deadline;
    int result = lock_packet(timeout_ms, &deadline);
    if (result != 0) { return result; }
    result = initialized
        ? at86rf215_packet_send(&packet, data, length, deadline) : -ENODEV;
    k_mutex_unlock(&radio_mutex);
    return result;
}

int ocs_radio_receive(void *data, size_t capacity, size_t *length,
                      int32_t timeout_ms)
{
    int64_t deadline;
    int result = lock_packet(timeout_ms, &deadline);
    if (result != 0) { return result; }
    result = initialized
        ? at86rf215_packet_receive(&packet, data, capacity, length, deadline)
        : -ENODEV;
    k_mutex_unlock(&radio_mutex);
    return result;
}
