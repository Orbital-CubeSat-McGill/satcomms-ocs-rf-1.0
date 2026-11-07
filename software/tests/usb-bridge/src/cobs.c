#include "cobs.h"

int cobs_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max)
{
	size_t i = 0;
	size_t o = 0;

	if (in == NULL || out == NULL || in_len == 0) {
		return -1;
	}

	while (i < in_len) {
		uint8_t code = in[i++];

		if (code == 0) {
			return -1;
		}

		/* copy code-1 data bytes */
		for (uint8_t n = 1; n < code; n++) {
			if (i >= in_len) {
				return -1;
			}
			if (in[i] == 0) {
				return -1;
			}
			if (o >= out_max) {
				return -1;
			}
			out[o++] = in[i++];
		}

		/* insert a zero unless this was a full block (0xFF) or end */
		if (code != 0xFF && i < in_len) {
			if (o >= out_max) {
				return -1;
			}
			out[o++] = 0;
		}
	}

	return (int)o;
}
