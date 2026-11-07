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


def app_payload(counter, call=b"VE3ABC", uptime_ms=None, reset_cause=0x40, tx_power=15, tx_errors=0):
    """The app's frame without its FCS, laid out here by hand so the test checks the parser."""
    uptime_ms = 1000 * counter if uptime_ms is None else uptime_ms
    return (bytes([1]) + call.ljust(10) + counter.to_bytes(4, "little") + uptime_ms.to_bytes(4, "little")
            + bytes([reset_cause, tx_power]) + tx_errors.to_bytes(4, "little") + fd.app_pattern(counter))


def app_psdu(counter, **kw):
    payload = app_payload(counter, **kw)
    return payload + zlib.crc32(payload).to_bytes(4, "little")


def flip_bits(psdu, bits):
    out = bytearray(psdu)
    for b in bits:
        out[b // 8] ^= 1 << (b % 8)
    return bytes(out)


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

    def test_high_duty_cycle(self):
        r = self.decode([modulate(frame_bits(tx_burst_payload()))] * 3, gap_s=0.02)
        self.assertEqual(sum(f.get("fcs_ok", False) for f in r), 3)

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


class AppFrames(unittest.TestCase):
    PATTERN_BIT = 8 * fd.APP_PATTERN

    def test_frame_length_is_the_apps(self):
        self.assertEqual(len(app_psdu(0)), fd.APP_LEN)

    def test_pattern_changes_with_the_counter(self):
        for a, b in ((0, 1), (1000, 1001)):
            errors, total = fd.count_bit_errors(fd.app_pattern(a), fd.app_pattern(b))
            self.assertAlmostEqual(errors / total, 0.5, delta=0.1)

    def test_fields(self):
        f = fd.parse_app_frame(app_psdu(70000, call=b"VA3XYZ/M", uptime_ms=123456, reset_cause=0x01, tx_power=31, tx_errors=7))
        self.assertEqual(f, {"version": 1, "call": "VA3XYZ/M", "counter": 70000, "uptime_ms": 123456,
                             "reset_cause": 0x01, "tx_power": 31, "tx_errors": 7})

    def test_clean_run(self):
        link = fd.AppLink()
        for c in range(5):
            app = link.update(app_psdu(c), True)
            self.assertEqual((app["counter"], app["errors"]), (c, 0))
        self.assertEqual((link.frames, link.lost, link.bad), (5, 0, 0))

    def test_gap_counts_as_lost(self):
        link = fd.AppLink()
        for c in (10, 11, 15):
            link.update(app_psdu(c), True)
        self.assertEqual((link.frames, link.lost), (3, 3))

    def test_bad_frame_counts_bit_errors(self):
        link = fd.AppLink()
        link.update(app_psdu(4), True)
        app = link.update(flip_bits(app_psdu(5), [self.PATTERN_BIT + 3, self.PATTERN_BIT + 90]), False)
        self.assertEqual((app["counter"], app["errors"], app["good"]), (5, 2, False))
        link.update(app_psdu(6), True)
        self.assertEqual((link.lost, link.bad, link.bit_errors), (0, 1, 2))

    def test_bad_frame_with_a_damaged_counter_is_still_identified(self):
        link = fd.AppLink()
        link.update(app_psdu(4), True)
        app = link.update(flip_bits(app_psdu(7), [8 * fd.APP_COUNTER + 5, self.PATTERN_BIT]), False)
        self.assertEqual((app["counter"], app["errors"]), (7, 1))
        self.assertEqual(link.lost, 2)

    def test_garbage_is_unidentified_then_lost(self):
        link = fd.AppLink()
        link.update(app_psdu(4), True)
        garbage = np.random.default_rng(1).bytes(fd.APP_LEN)
        self.assertIsNone(link.update(garbage, False)["counter"])
        link.update(app_psdu(6), True)
        self.assertEqual((link.unidentified, link.bad, link.lost), (1, 0, 1))
        self.assertIn("unidentified", fd.format_app(link.update(garbage, False)))

    def test_reboot_is_not_loss(self):
        link = fd.AppLink()
        for c in (100, 101, 0, 1):
            link.update(app_psdu(c), True)
        self.assertEqual((link.reboots, link.lost, link.frames), (1, 0, 4))

    def test_other_frames_are_left_alone(self):
        link = fd.AppLink()
        burst = tx_burst_payload()
        self.assertIsNone(link.update(burst + zlib.crc32(burst).to_bytes(4, "little"), True))
        other = bytes([2]) + app_psdu(0)[1:]
        self.assertIsNone(link.update(other, True))
        self.assertEqual(link.frames, 0)

    def test_printed_line(self):
        line = fd.format_app(fd.AppLink().update(app_psdu(3, uptime_ms=3500), True))
        self.assertEqual(line, "VE3ABC cnt=3 up=3.5s rst=0x40 pwr=15 txerr=0 lost=0 bad=0 bit_err=0/792")

    def test_capture_end_to_end(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "capture.iq")
            write_capture(path, [modulate(frame_bits(app_payload(c)), cfo=3e3) for c in (0, 1, 3)], gap_s=0.02)
            results = fd.decode_capture(path, RATE, TUNE, FREQ)
        link = fd.AppLink()
        for r in results:
            self.assertEqual(r["phr"]["len"], fd.APP_LEN)
            link.update(r["psdu"], r["fcs_ok"])
        self.assertEqual((link.frames, link.lost, link.bad, link.bit_errors), (3, 1, 0, 0))


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


class Stream(unittest.TestCase):
    """FrameSync, fed soft symbols the way the flowgraph's symbol sync would."""

    def soft(self, bits, polarity=1, offset=0.0):
        return ((2.0 * bits - 1) * polarity + offset / fd.DEVIATION).astype(np.float32)

    def noise(self, n, seed=1):
        return np.random.default_rng(seed).standard_normal(n).astype(np.float32)

    def run_stream(self, stream, chunk=1000):
        sync = fd.FrameSync()
        out = []
        for i in range(0, len(stream), chunk):
            out += sync.feed(stream[i:i + chunk])
        return out

    def test_frame_split_across_chunks(self):
        payload = bytes(range(124))
        for chunk in (1, 7, 512, 100000):
            with self.subTest(chunk=chunk):
                r = self.run_stream(np.concatenate((self.noise(500), self.soft(frame_bits(payload)), self.noise(50))), chunk)
                self.assertEqual(len(r), 1)
                self.assertTrue(r[0]["fcs_ok"])
                self.assertEqual(r[0]["psdu"][:-4], payload)
                self.assertEqual(r[0]["polarity"], 1)

    def test_inverted_stream(self):
        r = self.run_stream(self.soft(frame_bits(bytes(range(124))), polarity=-1))
        self.assertTrue(r[0]["fcs_ok"])
        self.assertEqual(r[0]["polarity"], -1)

    def test_offset_is_measured(self):
        r = self.run_stream(self.soft(frame_bits(bytes(range(124))), offset=5e3))
        self.assertAlmostEqual(r[0]["offset"], 5e3, delta=100)

    def test_consecutive_frames(self):
        payloads = [bytes([i]) * 50 for i in range(5)]
        stream = np.concatenate([np.concatenate((self.noise(100, i), self.soft(frame_bits(p)))) for i, p in enumerate(payloads)])
        r = self.run_stream(stream)
        self.assertEqual([f["psdu"][:-4] for f in r], payloads)

    def test_false_sync_does_not_swallow_a_real_frame(self):
        """A false match claiming the longest length, just before the real frame."""
        false_start = np.array(fd.SYNC_BITS + [0, 0, 0, 0, 0] + [1] * 11, dtype=np.uint8)
        payload = bytes(range(124))
        stream = np.concatenate((self.soft(false_start), self.soft(frame_bits(payload)), self.noise(20000)))
        good = [f for f in self.run_stream(stream) if f["fcs_ok"]]
        self.assertEqual(len(good), 1)
        self.assertEqual(good[0]["psdu"][:-4], payload)

    def test_bad_fcs_is_reported(self):
        r = self.run_stream(self.soft(frame_bits(bytes(range(124)), crc=b"\xde\xad\xbe\xef")))
        self.assertEqual(len(r), 1)
        self.assertFalse(r[0]["fcs_ok"])

    def test_snr_comes_from_the_scatter_and_ignores_the_offset(self):
        rng = np.random.default_rng(1)
        bits = np.unpackbits(np.frombuffer(rng.bytes(500), dtype=np.uint8))
        for spread_db in (10, 20):
            sigma = 10 ** (-spread_db / 20)
            soft = 2.0 * bits - 1 + 0.3 + sigma * rng.standard_normal(len(bits))
            self.assertAlmostEqual(fd.estimate_snr(soft), spread_db + fd.SNR_CAL_DB, delta=0.5)

    def test_snr_with_nothing_to_measure(self):
        self.assertIsNone(fd.estimate_snr(np.ones(100)))

    def test_noise_alone_reports_nothing(self):
        self.assertEqual(self.run_stream(self.noise(500000)), [])


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
