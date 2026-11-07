/*
 * Host-side COBS decode tests (plain gcc, no Zephyr).
 * Vectors from docs/greenbear-protocol.md and Ion stub_frames.py.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "../src/cobs.h"

static int failures;

static void expect_eq(const char *name, const uint8_t *got, int got_len,
		      const uint8_t *want, size_t want_len)
{
	if (got_len < 0) {
		fprintf(stderr, "FAIL %s: decode returned %d\n", name, got_len);
		failures++;
		return;
	}
	if ((size_t)got_len != want_len || memcmp(got, want, want_len) != 0) {
		fprintf(stderr, "FAIL %s: got %d bytes, want %zu\n", name, got_len, want_len);
		failures++;
		return;
	}
	printf("ok   %s\n", name);
}

static void hex_to_bytes(const char *hex, uint8_t *out, size_t *out_len)
{
	size_t n = 0;
	for (const char *p = hex; *p; ) {
		while (*p == ' ') {
			p++;
		}
		if (!*p) {
			break;
		}
		unsigned v;
		if (sscanf(p, "%2x", &v) != 1) {
			fprintf(stderr, "bad hex: %s\n", p);
			exit(2);
		}
		out[n++] = (uint8_t)v;
		p += 2;
	}
	*out_len = n;
}

static void check_vector(const char *name, const char *enc_hex, const char *plain_hex)
{
	uint8_t enc[1100], plain[1100], out[1100];
	size_t enc_len = 0, plain_len = 0;

	hex_to_bytes(enc_hex, enc, &enc_len);
	hex_to_bytes(plain_hex, plain, &plain_len);
	int n = cobs_decode(enc, enc_len, out, sizeof(out));
	expect_eq(name, out, n, plain, plain_len);
}

static void check_malformed(const char *name, const char *enc_hex)
{
	uint8_t enc[64], out[64];
	size_t enc_len = 0;

	hex_to_bytes(enc_hex, enc, &enc_len);
	int n = cobs_decode(enc, enc_len, out, sizeof(out));
	if (n != -1) {
		fprintf(stderr, "FAIL %s: expected -1, got %d\n", name, n);
		failures++;
		return;
	}
	printf("ok   %s\n", name);
}

int main(void)
{
	/* Published vectors (encoded without trailing delimiter) */
	check_vector("empty", "01", "");
	check_vector("00", "01 01", "00");
	check_vector("00 00", "01 01 01", "00 00");
	check_vector("11", "02 11", "11");
	check_vector("11 22", "03 11 22", "11 22");
	check_vector("11 00", "02 11 01", "11 00");
	check_vector("11 00 00", "02 11 01 01", "11 00 00");
	check_vector("11 22 00 33", "03 11 22 02 33", "11 22 00 33");
	check_vector("11 22 33 44", "05 11 22 33 44", "11 22 33 44");

	/* stub_frames.py: wire without trailing 0x00 vs logical */
	check_vector(
		"stub empty_seq1234",
		"0b564533204a4b420134120101010101031ee6",
		"564533204a4b420134120000000000001ee6");
	check_vector(
		"stub hello",
		"0a564533204a4b4201010105f15365050868656c6c6f8c0f",
		"564533204a4b4201010000f15365050068656c6c6f8c0f");
	check_vector(
		"stub two_bytes",
		"0a564533204a4b420107022a0101020205aabb5f23",
		"564533204a4b420107002a0000000200aabb5f23");

	/* 254 non-zeros → code 0xFF */
	{
		uint8_t plain[254], enc[256], out[256];
		memset(plain, 0x11, sizeof(plain));
		enc[0] = 0xFF;
		memcpy(enc + 1, plain, 254);
		int n = cobs_decode(enc, 255, out, sizeof(out));
		expect_eq("254 nonzero", out, n, plain, 254);
	}

	check_malformed("empty input", "");
	check_malformed("truncated", "05 11 22");
	check_malformed("embedded zero", "03 11 00 22");

	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("all ok\n");
	return 0;
}
