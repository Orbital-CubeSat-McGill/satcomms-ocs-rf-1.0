#!/usr/bin/env python3
"""
Shows where the link fails, from a log of software/tests/tx-sweep's frames.

    python3 -u ground-station/GNUradio/fsk_demod.py | tee ~/weak-1.log
    python3 ground-station/weak_signal.py ~/weak-1.log

Stop the flowgraph only after frames come back at pwr=15. Frames lost at the end of a
log can't be counted, because nothing after them shows they were ever sent. Output from
fsk_decode.py works too, and its SNR is a power measurement that doesn't flatten at 30 dB.

 sent: counter values in the log's range at this level, heard or not
 ok:   valid FCS
 bad:  heard with a failed FCS, and identified by its pattern
 lost: sent, never decoded or not identified
 FER:  (bad + lost) / sent

BER only covers frames that were heard, so it reads low close to the point of failure.
"""
import argparse
import re
import statistics
import sys

STEP_FRAMES = 30    # tx-sweep's STEP_FRAMES
TX_POWER_TOP = 15   # tx-sweep's TX_POWER_TOP
PATTERN_BITS = 792

FRAME = re.compile(r"len=(\d+) fcs=(ok|BAD)")
COUNTER = re.compile(r" cnt=(\d+)")
SNR = re.compile(r" snr=\s*(-?[\d.]+)dB")
POWER = re.compile(r" pwr=(\d+)")
TX_ERRORS = re.compile(r" txerr=(\d+)")
BIT_ERRORS = re.compile(r" bit_err=(\d+)/")


def level_for(counter, step=STEP_FRAMES):
    """tx-sweep's level_for: the level a counter value was sent at."""
    return TX_POWER_TOP - (counter // step) % (TX_POWER_TOP + 1)


class Level:
    def __init__(self):
        self.sent = self.ok = self.bad = self.bit_errors = 0
        self.snr = []

    @property
    def fer(self):
        return (self.sent - self.ok) / self.sent if self.sent else None

    @property
    def lost(self):
        return self.sent - self.ok - self.bad


class Sweep:
    def __init__(self, step=STEP_FRAMES):
        self.step = step
        self.levels = {}
        self.last = None
        self.reboots = self.mismatches = self.unidentified = self.other = 0
        self.tx_errors = 0

    def _level(self, counter):
        return self.levels.setdefault(level_for(counter, self.step), Level())

    def add_line(self, line):
        if not line.startswith("["):
            return
        frame = FRAME.search(line)
        if not frame or frame[1] != "128":
            self.other += 1
            return
        if "unidentified" in line:
            self.unidentified += 1
            return
        counter = COUNTER.search(line)
        if not counter:
            self.other += 1
            return
        self._add_frame(int(counter[1]), frame[2] == "ok", line)

    def _add_frame(self, counter, good, line):
        if self.last is not None and counter == self.last:
            return
        if self.last is not None and counter < self.last:
            self.reboots += 1       # the counter only restarts with the firmware
            self.last = None

        first = counter if self.last is None else self.last + 1
        for c in range(first, counter + 1):
            self._level(c).sent += 1
        self.last = counter

        level = self._level(counter)
        level.ok += good
        level.bad += not good
        bit_errors = BIT_ERRORS.search(line)
        if bit_errors:
            level.bit_errors += int(bit_errors[1])
        snr = SNR.search(line)
        if snr:
            level.snr.append(float(snr[1]))
        power = POWER.search(line)
        if power and int(power[1]) != level_for(counter, self.step):
            self.mismatches += 1
        tx_errors = TX_ERRORS.search(line)
        if tx_errors:
            self.tx_errors = max(self.tx_errors, int(tx_errors[1]))

    def missing_levels(self):
        return [p for p in range(TX_POWER_TOP, -1, -1) if p not in self.levels]

    def first_with(self, test):
        """The highest level that passes test, going down from the top."""
        return next((p for p in sorted(self.levels, reverse=True) if test(self.levels[p])), None)


def format_table(sweep):
    lines = ["pwr  sent    ok   bad  lost     FER     bit_err       BER  snr_med  snr_min"]
    for p in sorted(sweep.levels, reverse=True):
        v = sweep.levels[p]
        bits = (v.ok + v.bad) * PATTERN_BITS
        ber = f"{v.bit_errors / bits:.1e}" if bits else "n/a"
        med = f"{statistics.median(v.snr):.1f}" if v.snr else "n/a"
        low = f"{min(v.snr):.1f}" if v.snr else "n/a"
        lines.append(f"{p:3}  {v.sent:4}  {v.ok:4}  {v.bad:4}  {v.lost:4}  {100 * v.fer:5.1f}%  "
                     f"{v.bit_errors:10}  {ber:>8}  {med:>7}  {low:>7}")
    return "\n".join(lines)


def format_verdict(sweep):
    out = []
    first = sweep.first_with(lambda v: v.fer > 0)
    half = sweep.first_with(lambda v: v.fer >= 0.5)
    dead = sweep.first_with(lambda v: v.ok == 0)
    if first is None:
        out.append("No frame failed at any level seen. Weaken the link and run it again.")
    else:
        out.append(f"First failure at pwr={first}.")
        if half is not None:
            out.append(f"Half or more failed from pwr={half}.")
        if dead is not None:
            out.append(f"Nothing valid from pwr={dead}.")

    missing = sweep.missing_levels()
    if missing:
        out.append(f"WARNING no data for pwr={missing}. Run until frames come back at pwr={TX_POWER_TOP}.")
    if sweep.mismatches:
        out.append(f"WARNING {sweep.mismatches} frames report a pwr the schedule doesn't expect. "
                   "This table is wrong: is tx-sweep flashed, with the same step?")
    if sweep.tx_errors:
        out.append(f"WARNING the hat reports {sweep.tx_errors} TX errors, so some loss isn't the link.")
    if sweep.reboots:
        out.append(f"WARNING {sweep.reboots} reboots during the log.")
    if sweep.unidentified or sweep.other:
        out.append(f"Damaged beyond identifying, counted as lost: {sweep.unidentified}. Other lines: {sweep.other}.")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", help="output of fsk_demod.py or fsk_decode.py")
    ap.add_argument("--step", type=int, default=STEP_FRAMES, help="frames at each level")
    args = ap.parse_args()

    sweep = Sweep(args.step)
    with open(args.log, errors="replace") as f:
        for line in f:
            sweep.add_line(line)

    if not sweep.levels:
        print("no app frames in this log")
        return 1
    print(format_table(sweep))
    print()
    print(format_verdict(sweep))
    return 1 if sweep.mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
