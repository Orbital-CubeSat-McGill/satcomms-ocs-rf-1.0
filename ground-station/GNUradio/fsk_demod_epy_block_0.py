"""
Decodes the hat's MR-FSK frames from the symbol sync's soft symbols, prints them, and writes them
to the run's text log with a UTC time. The frame logic is FrameSync in ground-station/fsk_decode.py.
The log also records each receiver setting when it changes, and when IQ recording starts and stops.

expect='greenbear' -> Green Bear packets from software/tests/usb-bridge (greenbear_packet.py): the header,
                      sequence gaps, and the whole message on the lines under each frame
expect='tx-burst'  -> bit errors against the tx-burst pattern. The old app's 128-byte frames parse in this mode
"""
import os
import sys
import time

import numpy as np
from gnuradio import gr

# Flowgraph variables passed in so their changes are logged. GRC calls the setters from the sliders
SETTINGS = ('freq', 'rf_gain', 'vga_gain', 'amp', 'offset_hz', 'sq_thresh', 'iq_file')


def utc():
    t = time.time()
    return time.strftime('%Y-%m-%dT%H:%M:%S', time.gmtime(t)) + f'.{int(t % 1 * 1000):03d}Z'


def _setting(name):
    def get(self):
        return self.settings.get(name)

    def put(self, value):
        if self.settings.get(name) != value:
            self.settings[name] = value
            if name == 'iq_file':
                self._write('# iq ' + ('off' if value == os.devnull else f'recording to {value}'))
            else:
                self._write(f'# {name}={value}')
    return property(get, put)


class blk(gr.sync_block):
    """OCS-RF frame decoder"""

    def __init__(self, expect='greenbear', log_file='', samp_rate=0.0, freq=0.0, rf_gain=0.0, vga_gain=0.0,
                 amp=False, offset_hz=0.0, sq_thresh=0.0, iq_file=''):
        gr.sync_block.__init__(self, name='OCS-RF frame decoder', in_sig=[np.float32], out_sig=None)
        self.expect = expect
        self.fd = None
        self.frames = []
        self.settings = {}
        self.log = None
        # Empty when GRC builds the block only to read its ports
        if log_file:
            os.makedirs(os.path.dirname(os.path.abspath(log_file)), exist_ok=True)
            self.log = open(log_file, 'a', buffering=1)
            self._write(f'# fsk_demod start, samp_rate={samp_rate:g}, log {log_file}')
        self.freq, self.rf_gain, self.vga_gain, self.amp = freq, rf_gain, vga_gain, amp
        self.offset_hz, self.sq_thresh, self.iq_file = offset_hz, sq_thresh, iq_file

    def _load(self):
        # Not at import time: GRC also runs this file, from somewhere else, to read the ports
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
        import fsk_decode
        self.fd = fsk_decode
        self.sync = fsk_decode.FrameSync()
        self.link = fsk_decode.AppLink()
        self.gb = fsk_decode.GreenBearLink() if self.expect == 'greenbear' else None
        self.pattern = fsk_decode.PATTERNS.get(self.expect)

    def _write(self, text):
        if self.log:
            self.log.write(f'{utc()} {text}\n')

    def work(self, input_items, output_items):
        if self.fd is None:
            self._load()
        for r in self.sync.feed(np.array(input_items[0])):
            self.frames.append(r)
            for line in self._format(r):
                print(line, flush=True)
                self._write(line)
        return len(input_items[0])

    def stop(self):
        self._write(f'# fsk_demod stop, {len(self.frames)} frames')
        return True

    def _format(self, r):
        good = sum(f['fcs_ok'] for f in self.frames)
        line = (f"[{len(self.frames):4}] len={r['len']} fcs={'ok ' if r['fcs_ok'] else 'BAD'} ok={good}"
                f" pol={'+' if r['polarity'] > 0 else '-'} sync_err={r['sync_errors']}"
                f" offset={r['offset'] / 1e3:+5.1f}kHz dev={r['deviation'] / 1e3:4.1f}kHz")
        if r['snr'] is not None:
            line += f" snr={r['snr']:4.1f}dB"
        if self.gb:
            gb = self.gb.update(r['psdu'], r['fcs_ok'])
            return [line + f" {r['psdu'][:32]!r}"] + (self.fd.format_greenbear(gb) if gb else [])
        app = self.link.update(r['psdu'], r['fcs_ok'])
        if app:
            return [f"{line} {self.fd.format_app(app)}"]
        if self.pattern:
            errors, total = self.fd.count_bit_errors(r['psdu'][:-self.fd.FCS_LEN], self.pattern)
            line += f" bit_err={errors}/{total}"
        return [line + f" {r['psdu'][:32]!r}"]


for _name in SETTINGS:
    setattr(blk, _name, _setting(_name))
