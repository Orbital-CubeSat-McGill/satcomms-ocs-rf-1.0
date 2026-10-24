#pragma once

#include <stdbool.h>
#include <ocs/at86rf215.h>
#include <ocs/radio.h>

/* Internal engine: caller holds the public API mutex for the entire operation. */
struct at86rf215_packet {
    struct at86rf215 *radio;
    bool configured;
    /* Staging preserves the caller's output if a later SPI chunk fails.
     * Lives with the singleton radio, not on the caller's small thread stack.
     */
    uint8_t rx_payload[OCS_RADIO_MAX_PAYLOAD];
};

int at86rf215_packet_configure(struct at86rf215_packet *packet,
                               const struct ocs_radio_config *config);
int at86rf215_packet_send(struct at86rf215_packet *packet,
                          const void *data, size_t length, int64_t deadline);
int at86rf215_packet_receive(struct at86rf215_packet *packet,
                             void *data, size_t capacity, size_t *length,
                             int64_t deadline);
