#!/usr/bin/env python3
"""
Runs the generated flowgraph on simulated captures, with the HackRF swapped for a file.
From ground-station/GNUradio, in `nix develop .#ground`: python3 test_flowgraph.py

As with test_fsk_decode.py, a pass says nothing about the real chip's bit orders or FCS.
"""
import os
import sys
import tempfile
import time
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import numpy as np
from gnuradio import blocks, gr, soapy
from PyQt5 import Qt

import fsk_decode
import fsk_demod
import greenbear_packet as gbp
import test_fsk_decode as sim

app = Qt.QApplication(sys.argv[:1])
capture = None


def jittered(bursts, seed=3):
    """Random lead-in, so the symbol sync doesn't meet every frame at the same phase."""
    rng = np.random.default_rng(seed)
    return [np.concatenate((np.zeros(int(rng.integers(0, 2000))), b)) for b in bursts]


class FileSource(gr.hier_block2):
    """Stands in for soapy.source: plays an int8 capture, and ignores the tuning calls."""

    def __init__(self, *args):
        gr.hier_block2.__init__(self, "file_source", gr.io_signature(0, 0, 0), gr.io_signature(1, 1, gr.sizeof_gr_complex))
        self.file = blocks.file_source(1, capture, False)
        self.to_complex = blocks.interleaved_char_to_complex(False, 128.0)
        self.connect(self.file, self.to_complex, self)

    def __getattr__(self, name):
        if name.startswith("set_"):
            return lambda *args: None
        raise AttributeError(name)


soapy.source = FileSource


class Flowgraph(unittest.TestCase):
    def setUp(self):
        global capture
        self.dir = tempfile.TemporaryDirectory()
        capture = os.path.join(self.dir.name, "capture.iq")

    def tearDown(self):
        self.dir.cleanup()

    def run_graph(self, bursts, offset_hz=0, **kw):
        sim.write_capture(capture, jittered(bursts), **kw)
        tb = fsk_demod.fsk_demod(log_dir=self.dir.name)
        tb.set_offset_hz(offset_hz)
        start = time.monotonic()
        tb.start()
        tb.wait()
        self.speed = os.path.getsize(capture) / 2 / (time.monotonic() - start)
        self.tb = tb
        return tb.epy_block_0.frames

    def assert_all_good(self, frames, payload, count):
        self.assertEqual(sum(f["fcs_ok"] for f in frames), count)
        for f in frames:
            if f["fcs_ok"]:
                self.assertEqual(f["psdu"][:-4], payload)

    def test_tx_burst(self):
        payload = sim.tx_burst_payload()
        frames = self.run_graph([sim.modulate(sim.frame_bits(payload), cfo=1e3, ppm=20)] * 3)
        self.assert_all_good(frames, payload, 3)
        self.assertEqual(frames[0]["polarity"], 1)
        print(f"\ngraph ran at {self.speed / 1e6:.1f} MS/s, needs {sim.RATE / 1e6:.0f}", file=sys.stderr)

    def test_slider_removes_a_large_offset(self):
        payload = bytes(range(124))
        frames = self.run_graph([sim.modulate(sim.frame_bits(payload), cfo=30e3)] * 3, offset_hz=30e3)
        self.assert_all_good(frames, payload, 3)
        self.assertAlmostEqual(frames[0]["offset"], 0, delta=1500)

    def test_offset_is_reported(self):
        payload = bytes(range(124))
        frames = self.run_graph([sim.modulate(sim.frame_bits(payload), cfo=5e3)] * 3)
        self.assert_all_good(frames, payload, 3)
        self.assertAlmostEqual(frames[0]["offset"], 5e3, delta=1500)

    def test_inverted_spectrum(self):
        payload = bytes(range(124))
        frames = self.run_graph([sim.modulate(sim.frame_bits(payload), inverted=True)] * 3)
        self.assert_all_good(frames, payload, 3)
        self.assertEqual(frames[0]["polarity"], -1)

    def test_many_frames_lock_every_time(self):
        payload = bytes(range(124))
        frames = self.run_graph([sim.modulate(sim.frame_bits(payload), cfo=10e3, ppm=20)] * 30, gap_s=0.02)
        self.assert_all_good(frames, payload, 30)

    def test_app_frames(self):
        """Frames 3 and 7 are never sent. Loss, bit errors and SNR come out per frame."""
        counters = [c for c in range(30) if c not in (3, 7)]
        bursts = [sim.modulate(sim.frame_bits(sim.app_payload(c)), cfo=2e3, ppm=20) for c in counters]
        frames = self.run_graph(bursts, amp=20.0, snr_db=20.0, gap_s=0.02)
        link = fsk_decode.AppLink()
        for f in frames:
            self.assertEqual(f["len"], fsk_decode.APP_LEN)
            link.update(f["psdu"], f["fcs_ok"])
            self.assertAlmostEqual(f["snr"], 20.0, delta=1.5)
        self.assertEqual((link.frames, link.lost, link.bad, link.bit_errors), (28, 2, 0, 0))

    def test_logs_and_iq(self):
        """The text log gets the settings and every frame, and the IQ file holds the capture's own bytes."""
        payload = bytes(range(124))
        sim.write_capture(capture, jittered([sim.modulate(sim.frame_bits(payload))] * 3))
        tb = fsk_demod.fsk_demod(log_dir=self.dir.name)
        tb.set_record_iq(True)
        tb.start()
        tb.wait()
        tb.set_rf_gain(32)
        tb.set_record_iq(False)
        tb.blocks_file_sink_0.do_update()

        iq = [n for n in os.listdir(self.dir.name) if n.endswith(".iq") and n != os.path.basename(capture)]
        self.assertEqual(len(iq), 1)
        with open(capture, "rb") as a, open(os.path.join(self.dir.name, iq[0]), "rb") as b:
            self.assertTrue(a.read() == b.read())
        results = fsk_decode.decode_capture(os.path.join(self.dir.name, iq[0]), sim.RATE, sim.TUNE, sim.FREQ)
        self.assertEqual(sum(r["fcs_ok"] for r in results), 3)

        with open(tb.log_file) as f:
            lines = [line.split(" ", 1)[1] for line in f.read().splitlines()]
        self.assertTrue(lines[0].startswith("# fsk_demod start"))
        self.assertIn("# freq=905000000.0", lines)
        self.assertIn(f"# iq recording to {os.path.join(self.dir.name, iq[0])}", lines)
        self.assertEqual(sum("fcs=ok" in line for line in lines), 3)
        self.assertEqual(lines[-2:], ["# rf_gain=32", "# iq off"])

    def test_greenbear_packets(self):
        """Sent with 4 spare bytes for the FCS: whole messages back, the gap counted, and both in the log."""
        seqs = [10, 11, 12, 15]
        packets = [gbp.build(s, 1700000000 + s, f"log line {s}: ".encode() + bytes(range(65, 125))) for s in seqs]
        self.run_graph([sim.modulate(sim.frame_bits(p), cfo=1e3) for p in packets])
        link = self.tb.epy_block_0.gb
        self.assertEqual([p.seq for p in link.packets], seqs)
        self.assertEqual([p.content for p in link.packets], [gbp.parse(p).content for p in packets])
        self.assertEqual((link.lost, link.tail_lost), (2, 0))
        with open(self.tb.log_file) as f:
            log = f.read()
        self.assertIn(f"msg: {gbp.parse(packets[-1]).content.decode()!r}", log)
        self.assertIn("GAP missed=2", log)

    def test_greenbear_tail_overwritten(self):
        """usb-bridge sending only the packet's length: the chip's FCS overwrites its last 4 bytes."""
        packet = gbp.build(5, 42, b"hello world")
        self.run_graph([sim.modulate(sim.frame_bits(packet[:-4]))])
        link = self.tb.epy_block_0.gb
        self.assertEqual(([p.content for p in link.packets], link.tail_lost), ([b"hello wor"], 1))

    def test_weak_signal(self):
        payload = bytes(range(124))
        frames = self.run_graph([sim.modulate(sim.frame_bits(payload), cfo=2e3)] * 30, amp=8.0, snr_db=14.0, gap_s=0.02)
        self.assert_all_good(frames, payload, 30)

if __name__ == "__main__":
    unittest.main()
