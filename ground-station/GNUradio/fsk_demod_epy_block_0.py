"""
Decodes the hat's MR-FSK frames from the symbol sync's soft symbols, and prints them.
The frame logic is FrameSync in ground-station/fsk_decode.py.

expect='tx-burst'  -> bit-error count against the tx-burst pattern
expect='greenbear' -> parse the Green Bear header (ground-station/greenbear_packet.py)
                      and flag seq gaps, like `fsk_decode.py --expect greenbear`
"""
import os
import sys

import numpy as np
from gnuradio import gr


class blk(gr.sync_block):
    """OCS-RF frame decoder"""

    def __init__(self, expect='greenbear'):
        gr.sync_block.__init__(self, name='OCS-RF frame decoder', in_sig=[np.float32], out_sig=None)
        self.expect = expect
        self.fd = None
        self.gbp = None
        self.frames = []
        self.packets = []
        self.last_seq = None

    def _load(self):
        # Not at import time: GRC also runs this file, from somewhere else, to read the ports
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
        import fsk_decode
        self.fd = fsk_decode
        self.sync = fsk_decode.FrameSync()
        self.pattern = fsk_decode.PATTERNS.get(self.expect)
        if self.expect == 'greenbear':
            import greenbear_packet
            self.gbp = greenbear_packet

    def work(self, input_items, output_items):
        if self.fd is None:
            self._load()
        for r in self.sync.feed(np.array(input_items[0])):
            self.frames.append(r)
            print(self._format(r), flush=True)
            if self.gbp is not None and r['fcs_ok']:
                print(self._greenbear(r), flush=True)
        return len(input_items[0])

    def _format(self, r):
        good = sum(f['fcs_ok'] for f in self.frames)
        line = (f"[{len(self.frames):4}] len={r['len']} fcs={'ok ' if r['fcs_ok'] else 'BAD'} ok={good}"
                f" pol={'+' if r['polarity'] > 0 else '-'} sync_err={r['sync_errors']}"
                f" offset={r['offset'] / 1e3:+5.1f}kHz dev={r['deviation'] / 1e3:4.1f}kHz")
        if self.pattern:
            errors, total = self.fd.count_bit_errors(r['psdu'][:-self.fd.FCS_LEN], self.pattern)
            line += f" bit_err={errors}/{total}"
        return line + f" {r['psdu'][:32]!r}"

    def _greenbear(self, r):
        logical = r['psdu'][:-self.fd.FCS_LEN]
        try:
            pkt = self.gbp.parse(logical)
        except ValueError as e:
            return f"      greenbear parse: {e}"
        self.packets.append(pkt)
        line = f"      greenbear {self.gbp.format_header(pkt)}"
        if self.last_seq is not None:
            missed = (pkt.seq - self.last_seq - 1) % 65536
            if missed:
                line += f" GAP missed={missed}"
        self.last_seq = pkt.seq
        return line
