/* Plain COBS decode for Green Bear USB framing.
 * Spec: MPM-Ion0-software/docs/greenbear-protocol.md
 */
#ifndef USB_BRIDGE_COBS_H
#define USB_BRIDGE_COBS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode a plain-COBS frame (no trailing 0x00 delimiter).
 *
 * @param in      Encoded bytes (must contain no 0x00).
 * @param in_len  Length of @p in.
 * @param out     Output buffer.
 * @param out_max Capacity of @p out.
 * @return Decoded length on success, or -1 on malformed input / overflow.
 */
int cobs_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

#ifdef __cplusplus
}
#endif

#endif /* USB_BRIDGE_COBS_H */
