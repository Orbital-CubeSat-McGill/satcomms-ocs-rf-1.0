#!/usr/bin/env python3
"""
Tests weak_signal.py on hand-written log lines: python3 ground-station/test_weak_signal.py

GNUradio/test_flowgraph.py runs it on the flowgraph's real output as well.
"""
import unittest

import weak_signal as ws


def good(counter, pwr=None, snr=30.0, txerr=0):
    pwr = ws.level_for(counter) if pwr is None else pwr
    return (f"[   1] len=128 fcs=ok  ok=1 pol=+ sync_err=0 offset= -0.1kHz dev=24.4kHz snr={snr:4.1f}dB VA2AOX"
            f" cnt={counter} up={counter}.0s rst=0x10 pwr={pwr} txerr={txerr} lost=0 bad=0 bit_err=0/792")


def bad(counter, errors=5, snr=12.0):
    return (f"[   1] len=128 fcs=BAD ok=1 pol=+ sync_err=0 offset= -0.1kHz dev=24.4kHz snr={snr:4.1f}dB"
            f" cnt={counter} lost=0 bad=1 bit_err={errors}/792")


def run(lines, **kw):
    sweep = ws.Sweep(**kw)
    for line in lines:
        sweep.add_line(line)
    return sweep


class Schedule(unittest.TestCase):
    def test_steps_down_and_wraps(self):
        self.assertEqual(ws.level_for(0), 15)
        self.assertEqual(ws.level_for(29), 15)
        self.assertEqual(ws.level_for(30), 14)
        self.assertEqual(ws.level_for(16 * 30 - 1), 0)
        self.assertEqual(ws.level_for(16 * 30), 15)


class Sweep(unittest.TestCase):
    def test_clean_run(self):
        s = run(good(c) for c in range(60))
        self.assertEqual((s.levels[15].sent, s.levels[15].ok, s.levels[14].ok), (30, 30, 30))
        self.assertEqual(s.levels[15].fer, 0)
        self.assertIn("No frame failed", ws.format_verdict(s))

    def test_a_gap_is_charged_to_the_level_it_was_sent_at(self):
        """28 and 29 are at 15, 30 and 31 at 14, and none of them was heard."""
        s = run(good(c) for c in [27, 32])
        self.assertEqual((s.levels[15].sent, s.levels[15].ok, s.levels[15].lost), (3, 1, 2))
        self.assertEqual((s.levels[14].sent, s.levels[14].ok, s.levels[14].lost), (3, 1, 2))

    def test_a_level_never_heard_is_still_counted(self):
        s = run([good(29), good(60)])
        self.assertEqual((s.levels[14].sent, s.levels[14].ok, s.levels[14].lost), (30, 0, 30))
        self.assertEqual(s.first_with(lambda v: v.ok == 0), 14)

    def test_bad_frames_count_bit_errors(self):
        s = run([good(0), bad(1, errors=7), bad(2, errors=3), good(3)])
        self.assertEqual((s.levels[15].bad, s.levels[15].lost, s.levels[15].bit_errors), (2, 0, 10))
        self.assertEqual(s.levels[15].fer, 0.5)

    def test_unidentified_frames_end_up_lost(self):
        s = run([good(0), "[   1] len=128 fcs=BAD ok=1 snr=12.0dB unidentified lost=0 bad=0", good(2)])
        self.assertEqual((s.unidentified, s.levels[15].lost), (1, 1))

    def test_damaged_length_is_not_a_frame(self):
        s = run([good(0), "[   1] len=8 fcs=BAD ok=1 snr=12.0dB b'cnt=99 '", good(1)])
        self.assertEqual((s.other, s.levels[15].sent), (1, 2))

    def test_verdict_goes_from_the_top_down(self):
        lines = [good(c) for c in range(30)]                      # 15 clean
        lines += [good(c) if c % 3 else bad(c) for c in range(30, 60)]  # 14 a third bad
        lines += [good(c) for c in range(60, 90) if c % 2]        # 13 half lost
        lines += [good(120)]                                      # 12 nothing
        verdict = ws.format_verdict(run(lines))
        self.assertIn("First failure at pwr=14", verdict)
        self.assertIn("Half or more failed from pwr=13", verdict)
        self.assertIn("Nothing valid from pwr=12", verdict)

    def test_levels_with_no_data_are_flagged(self):
        verdict = ws.format_verdict(run(good(c) for c in range(30)))
        self.assertIn("WARNING no data", verdict)

    def test_the_apps_frames_are_flagged(self):
        """The app sends everything at 15, which doesn't fit the schedule."""
        s = run(good(c, pwr=15) for c in range(30, 40))
        self.assertEqual(s.mismatches, 10)
        self.assertIn("is tx-sweep flashed", ws.format_verdict(s))

    def test_a_reboot_does_not_count_as_loss(self):
        s = run([good(c) for c in (20, 21)] + [good(c) for c in (0, 1)])
        self.assertEqual((s.reboots, s.levels[15].sent, s.levels[15].lost), (1, 4, 0))
        self.assertIn("1 reboots", ws.format_verdict(s))

    def test_a_repeated_frame_counts_once(self):
        s = run([good(5), good(5), good(6)])
        self.assertEqual((s.levels[15].sent, s.levels[15].ok, s.reboots), (2, 2, 0))

    def test_hat_errors_are_reported(self):
        verdict = ws.format_verdict(run([good(0), good(1, txerr=2)]))
        self.assertIn("2 TX errors", verdict)

    def test_offline_lines_parse(self):
        line = ("[  1] t=   0.218s dur=  22.9ms frame     sfd_err=0 pol=+ snr= 56.4dB cfo=  -0.0kHz len=128 fcs=ok "
                " dev=24.2kHz VA2AOX cnt=0 up=0.0s rst=0x10 pwr=15 txerr=0 lost=0 bad=0 bit_err=0/792")
        s = run([line, "bursts=1 frames=1 fcs_ok=1"])
        self.assertEqual((s.levels[15].ok, s.levels[15].snr, s.other), (1, [56.4], 0))

    def test_table_has_a_row_per_level(self):
        table = ws.format_table(run(good(c) for c in range(60)))
        self.assertEqual(len(table.splitlines()), 3)


if __name__ == "__main__":
    unittest.main()
