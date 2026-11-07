#!/usr/bin/env python3
"""Tests for greenbear_packet.py — keep in sync with Ion stub_frames / protocol."""

from __future__ import annotations

import unittest

import greenbear_packet as gbp


# From Ion/src/ion/greenbear/stub_frames.py
STUBS = {
    "empty_seq1234": bytes.fromhex("564533204a4b420134120000000000001ee6"),
    "hello": bytes.fromhex("564533204a4b4201010000f15365050068656c6c6f8c0f"),
    "two_bytes": bytes.fromhex("564533204a4b420107002a0000000200aabb5f23"),
}


class TestCrc(unittest.TestCase):
    def test_check_value(self):
        self.assertEqual(gbp.crc16(b"123456789"), 0x29B1)


class TestParseStubs(unittest.TestCase):
    def test_empty(self):
        p = gbp.parse(STUBS["empty_seq1234"])
        self.assertEqual(p.callsign, b"VE3 JKB")
        self.assertEqual(p.version, 1)
        self.assertEqual(p.seq, 0x1234)
        self.assertEqual(p.time, 0)
        self.assertEqual(p.content, b"")

    def test_hello(self):
        p = gbp.parse(STUBS["hello"])
        self.assertEqual(p.seq, 1)
        self.assertEqual(p.time, 1_700_000_000)
        self.assertEqual(p.content, b"hello")

    def test_two_bytes(self):
        p = gbp.parse(STUBS["two_bytes"])
        self.assertEqual(p.seq, 7)
        self.assertEqual(p.time, 42)
        self.assertEqual(p.content, b"\xaa\xbb")


class TestBuildRoundTrip(unittest.TestCase):
    def test_round_trip(self):
        frame = gbp.build(seq=99, time=12345, content=b"abc\x00")
        p = gbp.parse(frame)
        self.assertEqual(p.seq, 99)
        self.assertEqual(p.time, 12345)
        self.assertEqual(p.content, b"abc\x00")

    def test_build_matches_stub_empty(self):
        self.assertEqual(gbp.build(0x1234, 0, b""), STUBS["empty_seq1234"])

    def test_bad_crc(self):
        bad = bytearray(STUBS["hello"])
        bad[-1] ^= 0xFF
        with self.assertRaises(ValueError):
            gbp.parse(bytes(bad))

    def test_bad_callsign(self):
        bad = bytearray(STUBS["hello"])
        bad[0] = ord("X")
        # fix CRC so we fail on callsign not CRC
        body = bad[:-2]
        crc = gbp.crc16(bytes(body)).to_bytes(2, "little")
        with self.assertRaises(ValueError):
            gbp.parse(bytes(body) + crc)


if __name__ == "__main__":
    unittest.main()
