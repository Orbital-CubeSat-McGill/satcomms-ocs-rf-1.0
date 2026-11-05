#!/usr/bin/env python3
"""
Tests fsk_decode.py on simulated captures: python3 ground-station/test_fsk_decode.py

The simulator is built from the same reading of the datasheet as the decoder, so
a pass shows the demodulator copes with offsets, drift and noise. It says nothing
about whether the bit orders and FCS match the real chip. Only a capture can.
"""
import os
import tempfile
import unittest
import zlib

import numpy as np

import fsk_decode as fd

RATE = 8e6
TUNE = 915.5e6
FREQ = 915e6


def frame_bits(payload, length=None, crc=None):
    """Preamble of 8 octets, SFD, PHR, then payload and FCS, LSB first."""
    fcs = zlib.crc32(payload).to_bytes(4, "little") if crc is None else crc
    psdu = np.frombuffer(payload + fcs, dtype=np.uint8)
    length = len(psdu) if length is None else length
    phr = [0, 0, 0, 0, 0] + [int(b) for b in f"{length:011b}"]
    return np.array([0, 1] * 32 + fd.SFD_BITS + phr + list(np.unpackbits(psdu, bitorder="little")), dtype=np.uint8)


def modulate(bits, cfo=0.0, ppm=0.0, inverted=False):
    """GFSK with BT = 1 at the capture's rate, 500 kHz below the tuned frequency."""
    sps = RATE / (fd.SYMBOL_RATE * (1 + ppm * 1e-6))
    n = int(len(bits) * sps)
    nrz = (2.0 * bits[(np.arange(n) / sps).astype(int)] - 1) * fd.DEVIATION

    sigma = np.sqrt(np.log(2)) / (2 * np.pi * 1.0) * RATE / fd.SYMBOL_RATE
    k = np.arange(-int(4 * sigma), int(4 * sigma) + 1)
    pulse = np.exp(-k ** 2 / (2 * sigma ** 2))
    freq = np.convolve(nrz, pulse / pulse.sum(), "same")
    if inverted:
        freq = -freq

    phase = 2 * np.pi * np.cumsum(freq + (FREQ - TUNE) + cfo) / RATE
    return np.exp(1j * phase)


def write_capture(path, bursts, amp=60.0, snr_db=None, gap_s=0.1, seed=1):
    """Bursts separated by gaps, scaled to int8. snr_db is measured in 100 kHz."""
    gap = np.zeros(int(gap_s * RATE), dtype=np.complex128)
    x = gap
    for b in bursts:
        x = np.concatenate((x, amp * b, gap))

    rng = np.random.default_rng(seed)
    if snr_db is None:
        sigma = 0.5
    else:
        sigma = np.sqrt(amp ** 2 / 10 ** (snr_db / 10) * RATE / 100e3 / 2)
    x = x + sigma * (rng.standard_normal(len(x)) + 1j * rng.standard_normal(len(x)))

    iq = np.empty(2 * len(x), dtype=np.int8)
    iq[0::2] = np.clip(np.round(x.real), -127, 127)
    iq[1::2] = np.clip(np.round(x.imag), -127, 127)
    iq.tofile(path)


PATTERN = fd.PATTERNS["tx-burst"]


def tx_burst_payload():
    return (PATTERN * 100)[:2043]


class Decoder(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.dir.name, "capture.iq")

    def tearDown(self):
        self.dir.cleanup()

    def decode(self, bursts, **kw):
        write_capture(self.path, bursts, **kw)
        return fd.decode_capture(self.path, RATE, TUNE, FREQ)

    def assert_good(self, r, payload):
        self.assertEqual(r["status"], "frame")
        self.assertTrue(r["fcs_ok"])
        self.assertEqual(r["psdu"][:-4], payload)

    def test_short_frame_length_and_offset(self):
        payload = bytes(range(124))
        r = self.decode([modulate(frame_bits(payload), cfo=7e3)])
        self.assertEqual(len(r), 1)
        self.assert_good(r[0], payload)
        self.assertEqual(r[0]["phr"]["len"], 128)
        self.assertEqual(r[0]["polarity"], 1)
        self.assertAlmostEqual(r[0]["cfo"], 7e3, delta=500)
        self.assertAlmostEqual(r[0]["deviation"], 25e3, delta=2.5e3)

    def test_max_length_with_clock_drift(self):
        for ppm in (-40, 40):
            with self.subTest(ppm=ppm):
                r = self.decode([modulate(frame_bits(tx_burst_payload()), cfo=-15e3, ppm=ppm)])
                self.assert_good(r[0], tx_burst_payload())
                self.assertEqual(fd.count_bit_errors(r[0]["psdu"][:-4], PATTERN), (0, 8 * 2043))

    def test_large_offset(self):
        payload = bytes(range(124))
        r = self.decode([modulate(frame_bits(payload), cfo=40e3)])
        self.assert_good(r[0], payload)
        self.assertAlmostEqual(r[0]["cfo"], 40e3, delta=500)

    def test_inverted_spectrum_is_reported(self):
        payload = bytes(range(124))
        r = self.decode([modulate(frame_bits(payload), inverted=True)])
        self.assert_good(r[0], payload)
        self.assertEqual(r[0]["polarity"], -1)

    def test_weak_signal(self):
        payload = bytes(range(124))
        r = self.decode([modulate(frame_bits(payload), cfo=5e3)] * 3, amp=12.0, snr_db=18.0)
        self.assertEqual(len(r), 3)
        for f in r:
            self.assert_good(f, payload)
            self.assertAlmostEqual(f["snr"], 18.0, delta=2.0)

    def test_several_frames_are_separated(self):
        payloads = [bytes([i]) * 60 for i in range(4)]
        r = self.decode([modulate(frame_bits(p)) for p in payloads], gap_s=0.01)
        self.assertEqual(len(r), 4)
        for f, p in zip(r, payloads):
            self.assert_good(f, p)

    def test_lopsided_payload_keeps_the_offset(self):
        for fill in (0x00, 0xFF):
            with self.subTest(fill=fill):
                payload = bytes([fill]) * 124
                r = self.decode([modulate(frame_bits(payload), cfo=7e3)])
                self.assert_good(r[0], payload)
                self.assertAlmostEqual(r[0]["cfo"], 7e3, delta=500)

    def test_bad_fcs_stays_visible(self):
        payload = bytes(range(124))
        r = self.decode([modulate(frame_bits(payload, crc=b"\xde\xad\xbe\xef"))])
        self.assertEqual(r[0]["status"], "frame")
        self.assertFalse(r[0]["fcs_ok"])
        self.assertEqual(r[0]["psdu"][:-4], payload)

    def test_noise_alone_gives_nothing(self):
        self.assertEqual(self.decode([], snr_db=10.0), [])

    def test_carrier_without_frame_has_no_sync(self):
        tone = np.exp(2j * np.pi * (FREQ - TUNE) * np.arange(int(0.02 * RATE)) / RATE)
        r = self.decode([tone])
        self.assertEqual([f["status"] for f in r], ["no-sync"])

    def test_truncated_frame_is_flagged(self):
        bits = frame_bits(bytes(range(124)))
        r = self.decode([modulate(bits[:len(bits) // 2])])
        self.assertEqual(r[0]["status"], "truncated")


class Fcs(unittest.TestCase):
    payload = bytes(range(200))

    def test_search_finds_the_default(self):
        psdu = self.payload + zlib.crc32(self.payload).to_bytes(4, "little")
        self.assertTrue(fd.fcs_ok(psdu))
        self.assertIn("in=little init=0xffffffff xorout=0xffffffff out=rev-little", fd.fcs_search(psdu))

    def test_search_finds_big_endian(self):
        psdu = self.payload + zlib.crc32(self.payload).to_bytes(4, "big")
        self.assertFalse(fd.fcs_ok(psdu))
        self.assertIn("in=little init=0xffffffff xorout=0xffffffff out=rev-big", fd.fcs_search(psdu))

    def test_search_rejects_a_wrong_fcs(self):
        self.assertEqual(fd.fcs_search(self.payload + b"\xde\xad\xbe\xef"), [])


class Phr(unittest.TestCase):
    def test_length_is_msb_first(self):
        bits = np.array([0, 0, 0, 0, 0] + [int(b) for b in f"{128:011b}"])
        self.assertEqual(fd.parse_phr(bits)["len"], 128)

    def test_rejects_mode_switch_and_whitening(self):
        base = [0, 0, 0, 0, 0] + [int(b) for b in f"{128:011b}"]
        self.assertTrue(fd.parse_phr(np.array(base))["valid"])
        for field in (0, 3, 4):
            bits = list(base)
            bits[field] = 1
            self.assertFalse(fd.parse_phr(np.array(bits))["valid"])


if __name__ == "__main__":
    unittest.main()
