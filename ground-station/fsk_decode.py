#!/usr/bin/env python3
"""
Offline decoder for the hat's MR-FSK frames, from a hackrf_transfer capture.

Capture (8 MS/s, tuned 500 kHz off the signal to stay clear of the DC spike):

    hackrf_transfer -r capture.iq -f 905500000 -s 8000000 -a 0 -l 16 -g 24 -n 80000000

Decode:

    python3 ground-station/fsk_decode.py capture.iq --expect tx-burst --fcs-search
    python3 ground-station/fsk_decode.py capture.iq --expect greenbear     # software/tests/usb-bridge

Preset 0: 2-FSK, 50 ksym/s, h = 1 (25 kHz deviation), uncoded, whitening off.

From the datasheet: preamble of 01010101 (6.10.3.1), SFD0 0x7209 sent LSB first,
PHR fields (figure 6-19), 0 as -fdev and 1 as +fdev (table 6-49).

Assumed, and to be confirmed on a real capture:
 - PSDU octets go LSB first, and L10-L0 goes MSB first.
 - The FCS is CRC-32 as Ethernet does it. --fcs-search tries the other variants.
 - The HackRF doesn't invert the spectrum. The SFD's sign shows this, and is reported.
"""
import argparse
import sys
import zlib

import numpy as np

import greenbear_packet as gbp

SYMBOL_RATE = 50e3
DEVIATION = 25e3
CHANNEL_RATE = 500e3        # exactly 10 samples per symbol
SPS = int(CHANNEL_RATE / SYMBOL_RATE)
WIDE_CUTOFF = 200e3         # room for the offset between the two radios' clocks
SEARCH_CUTOFF = 90e3        # first pass: a lopsided payload can skew the offset by up to fdev
NARROW_CUTOFF = 60e3

SFD_BITS = [1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0]  # 0x7209, LSB first
SYNC_BITS = [0, 1, 0, 1, 0, 1, 0, 1] + SFD_BITS              # end of the preamble, then SFD
SYNC_PREAMBLE = 8
SFD_MAX_ERRORS = 2
PHR_BITS = 16
FCS_LEN = 4
PREAMBLE_HISTORY = 40      # symbols kept before a sync match, to measure the offset
SNR_CAL_DB = -10.75         # estimate_snr to SNR in 100 kHz, live path, simulated 14-30 dB. It flattens above 30 dB

TIMING_GAIN = 0.03
BURST_THRESHOLD = 4.0       # power over the noise floor: 6 dB, in the wide filter's band
BURST_AVG_S = 0.5e-3
BURST_MERGE_S = 1e-3
BURST_MIN_S = 2e-3

PATTERNS = {"tx-burst": b"OCS-RF-1 TEST BURST 0123456789 "}

# The app's frame: software/app/src/main.c has the layout
APP_LEN = 128
APP_VERSION = 1
APP_CALL_SIGN = 1
APP_CALL_SIGN_LEN = 10
APP_COUNTER = 11
APP_UPTIME = 15
APP_RESET_CAUSE = 19
APP_TX_POWER = 20
APP_TX_ERRORS = 21
APP_PATTERN = 25
APP_PATTERN_SEED = 0x6C078965
APP_SEARCH = 64             # a damaged frame's counter is looked for this far past the last one
APP_MAX_BER = 0.25          # above this a damaged frame's pattern isn't the one tried. Random is 0.5

# Green Bear packets from software/tests/usb-bridge: the layout is in greenbear_packet.py
GB_LEN_FIELD = 14


def lowpass(cutoff, rate, n_taps):
    n = np.arange(n_taps) - (n_taps - 1) / 2
    taps = 2 * cutoff / rate * np.sinc(2 * cutoff / rate * n) * np.hamming(n_taps)
    return (taps / taps.sum()).astype(np.float32)


def read_channel(path, rate, offset):
    """Reads signed 8-bit IQ, moves the signal to 0 Hz and decimates to CHANNEL_RATE."""
    decim = int(round(rate / CHANNEL_RATE))
    if decim * CHANNEL_RATE != rate:
        raise ValueError(f"rate must be a multiple of {CHANNEL_RATE:.0f}")

    raw = np.memmap(path, dtype=np.int8, mode="r")
    taps = lowpass(WIDE_CUTOFF, rate, 12 * decim + 1)[::-1]
    hist = np.zeros(len(taps) - 1, dtype=np.complex64)
    chunk = decim << 18
    out = []

    for pos in range(0, len(raw) // 2, chunk):
        iq = np.asarray(raw[2 * pos:2 * (pos + chunk)], dtype=np.float32) / 128
        n = len(iq) // 2
        t = np.arange(pos, pos + n) / rate
        x = (iq[0:2 * n:2] + 1j * iq[1:2 * n:2]) * np.exp(-2j * np.pi * offset * t).astype(np.complex64)
        buf = np.concatenate((hist, x))
        hist = buf[-(len(taps) - 1):]
        windows = np.lib.stride_tricks.sliding_window_view(buf, len(taps))[::decim]
        out.append((windows @ taps).astype(np.complex64))

    return np.concatenate(out) if out else np.zeros(0, dtype=np.complex64)


def find_bursts(z):
    """Returns (start, end) sample pairs where the power stands above the noise floor."""
    n_avg = int(BURST_AVG_S * CHANNEL_RATE)
    if len(z) <= n_avg:
        return []

    power = np.convolve(np.abs(z) ** 2, np.ones(n_avg) / n_avg, "same")
    floor = np.percentile(power, 2)  # low enough to stay out of the bursts at a high duty cycle
    above = np.concatenate(([False], power > BURST_THRESHOLD * floor, [False]))
    edges = np.flatnonzero(above[1:] != above[:-1])

    merged = []
    for start, end in zip(edges[0::2], edges[1::2]):
        if merged and start - merged[-1][1] < BURST_MERGE_S * CHANNEL_RATE:
            merged[-1][1] = end
        else:
            merged.append([start, end])

    return [(s, e) for s, e in merged if e - s >= BURST_MIN_S * CHANNEL_RATE]


def noise_power(z, bursts):
    """Mean power in the narrow filter, from the longest gap between bursts."""
    edges = [0] + [v for b in bursts for v in b] + [len(z)]
    gaps = list(zip(edges[0::2], edges[1::2]))
    start, end = max(gaps, key=lambda g: g[1] - g[0])
    margin = int(BURST_AVG_S * CHANNEL_RATE)
    gap = z[start + margin:end - margin]
    if len(gap) < 10 * SPS:
        return None
    return float(np.mean(np.abs(channel_filter(gap, NARROW_CUTOFF)) ** 2))


def channel_filter(x, cutoff):
    return np.convolve(x, lowpass(cutoff, CHANNEL_RATE, 8 * SPS + 1), "same")


def discriminate(x):
    """Instantaneous frequency in Hz, smoothed over one symbol."""
    freq = np.angle(x[1:] * np.conj(x[:-1])) * CHANNEL_RATE / (2 * np.pi)
    return np.convolve(freq, np.ones(SPS) / SPS, "same")


def slice_symbols(y, first, count):
    """Samples at symbol centres, with Gardner timing to follow the clock difference."""
    values = np.zeros(count)
    pos = float(first)
    prev = np.interp(pos, np.arange(len(y)), y)
    values[0] = prev
    n = 1

    while n < count and pos + SPS < len(y) - 1:
        mid = np.interp(pos + SPS / 2, np.arange(len(y)), y)
        cur = np.interp(pos + SPS, np.arange(len(y)), y)
        pos += SPS + TIMING_GAIN * SPS * (prev - cur) * mid / DEVIATION ** 2
        values[n] = cur = np.interp(pos, np.arange(len(y)), y)
        prev = cur
        n += 1

    return values[:n]


def bits_to_bytes(bits):
    """Packs bits LSB first, the PSDU's assumed order on air."""
    return np.packbits(bits, bitorder="little").tobytes()


def parse_phr(bits):
    phr = {
        "ms": int(bits[0]),
        "rb1": int(bits[1]),
        "rb2": int(bits[2]),
        "fcs16": int(bits[3]),
        "dw": int(bits[4]),
        "len": int("".join(str(int(b)) for b in bits[5:16]), 2),
    }
    phr["valid"] = phr["ms"] == 0 and phr["dw"] == 0 and phr["fcs16"] == 0 and phr["len"] > FCS_LEN
    return phr


def fcs_ok(psdu):
    return zlib.crc32(psdu[:-FCS_LEN]) == int.from_bytes(psdu[-FCS_LEN:], "little")


def _crc32_serial(bits, init):
    reg = init
    for b in bits:
        top = (reg >> 31) ^ int(b)
        reg = (reg << 1) & 0xFFFFFFFF
        if top:
            reg ^= 0x04C11DB7
    return reg


def fcs_search(psdu):
    """Tries the CRC-32 variants on a frame, and names the ones that match its FCS."""
    data = np.frombuffer(psdu[:-FCS_LEN], dtype=np.uint8)
    wanted = psdu[-FCS_LEN:]
    matches = []

    for order in ("little", "big"):
        bits = np.unpackbits(data, bitorder=order)
        for init in (0, 0xFFFFFFFF):
            reg = _crc32_serial(bits, init)
            for xorout in (0, 0xFFFFFFFF):
                out = reg ^ xorout
                rev = int(f"{out:032b}"[::-1], 2)
                for name, value, endian in (
                    ("msb", out, "big"), ("msb", out, "little"),
                    ("rev", rev, "big"), ("rev", rev, "little"),
                ):
                    if value.to_bytes(4, endian) == wanted:
                        matches.append(f"in={order} init={init:#x} xorout={xorout:#x} out={name}-{endian}")
    return matches


def count_bit_errors(payload, pattern):
    reps = len(payload) // len(pattern) + 1
    expected = np.frombuffer((pattern * reps)[:len(payload)], dtype=np.uint8)
    got = np.frombuffer(payload, dtype=np.uint8)
    return int(np.unpackbits(got ^ expected).sum()), 8 * len(payload)


def measure_deviation(soft):
    """Mean level of symbols matching both neighbours: the smoothing shrinks the others."""
    signs = soft > 0
    settled = (signs[1:-1] == signs[:-2]) & (signs[1:-1] == signs[2:])
    return float(np.mean(np.abs(soft[1:-1][settled]))) if settled.any() else float("nan")


def estimate_snr(soft):
    """
    SNR in dB, from how far settled symbols scatter around their two levels, so it needs
    no power reading and ignores any offset. See SNR_CAL_DB for what it's tied to.
    """
    signs = soft > 0
    settled = (signs[1:-1] == signs[:-2]) & (signs[1:-1] == signs[2:])
    high, low = soft[1:-1][settled & signs[1:-1]], soft[1:-1][settled & ~signs[1:-1]]
    if len(high) < 2 or len(low) < 2:
        return None
    level = (high.mean() - low.mean()) / 2
    spread = (np.sum((high - high.mean()) ** 2) + np.sum((low - low.mean()) ** 2)) / (len(high) + len(low))
    return float(10 * np.log10(level ** 2 / spread) + SNR_CAL_DB) if spread > 0 else None


def app_pattern(counter):
    """The app's fill_pattern: xorshift32, low byte of each step."""
    x = (counter ^ APP_PATTERN_SEED) & 0xFFFFFFFF
    out = bytearray(APP_LEN - FCS_LEN - APP_PATTERN)
    for i in range(len(out)):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[i] = x & 0xFF
    return bytes(out)


def parse_app_frame(psdu):
    """The fields of the app's frame. Only worth trusting if the FCS passed."""
    return {
        "version": psdu[0],
        "call": psdu[APP_CALL_SIGN:APP_CALL_SIGN + APP_CALL_SIGN_LEN].decode("ascii", "replace").strip(),
        "counter": int.from_bytes(psdu[APP_COUNTER:APP_COUNTER + 4], "little"),
        "uptime_ms": int.from_bytes(psdu[APP_UPTIME:APP_UPTIME + 4], "little"),
        "reset_cause": psdu[APP_RESET_CAUSE],
        "tx_power": psdu[APP_TX_POWER],
        "tx_errors": int.from_bytes(psdu[APP_TX_ERRORS:APP_TX_ERRORS + 4], "little"),
    }


class AppLink:
    """
    Counts lost frames and bit errors from the app's frames, in the order they arrive.

    lost: counter values never heard. bad: heard, with a failed FCS. A valid FCS means no
    bit errors, so they only come from bad frames. A bad frame's own counter can't be
    trusted, so its counter is whichever nearby value's pattern fits. If none fits, the
    frame counts as unidentified, and the next good frame counts it as lost.
    """

    def __init__(self):
        self.last = None
        self.lost = self.bad = self.unidentified = self.reboots = 0
        self.frames = self.bit_errors = self.bits = 0

    def _identify(self, psdu):
        payload = psdu[APP_PATTERN:-FCS_LEN]
        candidates = [parse_app_frame(psdu)["counter"]]
        if self.last is not None:
            candidates += [(self.last + i) & 0xFFFFFFFF for i in range(1, APP_SEARCH + 1)]
        errors, counter = min((count_bit_errors(payload, app_pattern(c))[0], c) for c in candidates)
        return (counter, errors) if errors <= APP_MAX_BER * 8 * len(payload) else (None, None)

    def update(self, psdu, good):
        """Returns a dict for an app frame, or None for any other frame."""
        if len(psdu) != APP_LEN or (good and psdu[0] != APP_VERSION):
            return None

        if good:
            fields = parse_app_frame(psdu)
            counter, errors = fields["counter"], count_bit_errors(psdu[APP_PATTERN:-FCS_LEN], app_pattern(fields["counter"]))[0]
        else:
            fields = None
            counter, errors = self._identify(psdu)
            if counter is None:
                self.unidentified += 1
                return {"good": False, "counter": None, "fields": None, "link": self}

        if self.last is not None and counter <= self.last:
            self.reboots += 1       # the counter only restarts with the firmware
        elif self.last is not None:
            self.lost += counter - self.last - 1
        self.last = counter
        self.frames += 1
        self.bad += not good
        self.bit_errors += errors
        self.bits += 8 * (APP_LEN - FCS_LEN - APP_PATTERN)
        return {"good": good, "counter": counter, "errors": errors, "fields": fields, "link": self}

    def summary(self):
        ber = f"{self.bit_errors / self.bits:.2e}" if self.bits else "n/a"
        return (f"frames={self.frames + self.lost} lost={self.lost} bad={self.bad} unidentified={self.unidentified}"
                f" reboots={self.reboots} bit_err={self.bit_errors}/{self.bits} ber={ber}")


def format_app(app):
    link = app["link"]
    totals = f"lost={link.lost} bad={link.bad}"
    if app["counter"] is None:
        return f"unidentified {totals}"
    line = f"cnt={app['counter']}"
    f = app["fields"]
    if f:
        line = f"{f['call']} {line} up={f['uptime_ms'] / 1e3:.1f}s rst=0x{f['reset_cause']:02x} pwr={f['tx_power']} txerr={f['tx_errors']}"
    return f"{line} {totals} bit_err={app['errors']}/{8 * (APP_LEN - FCS_LEN - APP_PATTERN)}"


class GreenBearLink:
    """
    Parses Green Bear packets from frames with a valid FCS, and counts sequence numbers never heard.
    A sequence number that repeats or goes back (by more than half of the 16-bit range, so a wrap still
    counts as a gap) is a restart of the sender, or test packets sent in a loop, and isn't loss.

    The packet is the PSDU before its FCS, so the bridge has to send the packet plus 4 spare bytes.
    If it sends only the packet, the chip's FCS overwrites the packet's last 4 bytes: its CRC-16 and
    the last 2 bytes of content. That is reported as tail_lost, with the content that is left.
    """

    def __init__(self):
        self.last_seq = None
        self.packets = []
        self.lost = self.bad = self.unparsed = self.tail_lost = self.restarts = 0

    def _overwritten(self, psdu):
        """The packet's header, if the PSDU is exactly the packet with its tail overwritten."""
        if len(psdu) < gbp.HEADER_SIZE + FCS_LEN:
            return None         # the length field itself was overwritten
        content_len = int.from_bytes(psdu[GB_LEN_FIELD:GB_LEN_FIELD + 2], "little")
        if psdu[:7] != gbp.CALLSIGN or psdu[7] != gbp.VERSION or len(psdu) != gbp.HEADER_SIZE + content_len + gbp.CRC_SIZE:
            return None
        seq, time = int.from_bytes(psdu[8:10], "little"), int.from_bytes(psdu[10:14], "little")
        return gbp.Packet(psdu[:7], psdu[7], seq, time, psdu[gbp.HEADER_SIZE:-FCS_LEN])

    def update(self, psdu, good):
        """Returns a dict for a frame with a valid FCS, or None for a bad one."""
        if not good:
            self.bad += 1
            return None
        tail_lost = False
        try:
            pkt = gbp.parse(psdu[:-FCS_LEN])
        except ValueError as e:
            pkt = self._overwritten(psdu)
            if pkt is None:
                self.unparsed += 1
                return {"pkt": None, "error": str(e), "data": psdu[:-FCS_LEN], "link": self}
            tail_lost = True
            self.tail_lost += 1

        step = None if self.last_seq is None else (pkt.seq - self.last_seq) % 65536
        restart = step is not None and (step == 0 or step > 32768)
        missed = step - 1 if step is not None and not restart else 0
        self.restarts += restart
        self.lost += missed
        self.last_seq = pkt.seq
        self.packets.append(pkt)
        return {"pkt": pkt, "tail_lost": tail_lost, "missed": missed, "restart": restart, "link": self}

    def summary(self):
        return (f"packets={len(self.packets)} lost={self.lost} bad={self.bad} unparsed={self.unparsed}"
                f" tail_lost={self.tail_lost} restarts={self.restarts}")


def format_message(data):
    """The whole message on one line, as text where it is text."""
    return repr(data.decode("utf-8", "backslashreplace"))


def format_greenbear(gb):
    """Two lines: the packet's header and counts, then its whole content."""
    link = gb["link"]
    if gb["pkt"] is None:
        return [f"      greenbear parse: {gb['error']} unparsed={link.unparsed}",
                f"      msg: {format_message(gb['data'])}"]
    pkt = gb["pkt"]
    crc = "lost (tail overwritten by the FCS: the bridge must send 4 more bytes)" if gb["tail_lost"] else "ok"
    line = (f"      greenbear callsign={pkt.callsign.decode('ascii', 'replace')!r} seq={pkt.seq} time={pkt.time}"
            f" len={len(pkt.content) + 2 * gb['tail_lost']} lost={link.lost} bad={link.bad} crc16={crc}")
    if gb["missed"]:
        line += f" GAP missed={gb['missed']}"
    if gb["restart"]:
        line += f" RESTART restarts={link.restarts}"
    return [line, f"      msg: {format_message(pkt.content)}{' [last 2 bytes lost]' if gb['tail_lost'] else ''}"]


def find_sync(x, offset, cutoff):
    """Removes offset and filters. Returns the signal, frequency, SFD start, polarity and residual offset."""
    t = np.arange(len(x)) / CHANNEL_RATE
    x = channel_filter(x * np.exp(-2j * np.pi * offset * t), cutoff)
    y = discriminate(x)

    template = np.repeat(2 * np.array(SYNC_BITS) - 1.0, SPS)
    template -= template.mean()  # so the offset left in y can't bias the peak
    if len(y) <= len(template):
        return None
    corr = np.correlate(y, template, "valid")
    i_sync = int(np.argmax(np.abs(corr)))
    polarity = 1 if corr[i_sync] > 0 else -1

    sfd_start = i_sync + SYNC_PREAMBLE * SPS
    pre_start = max(0, sfd_start - 48 * SPS)
    span = (sfd_start - pre_start) // (2 * SPS) * (2 * SPS)  # whole 01 pairs, so the mean is the offset
    dc = float(np.mean(y[sfd_start - span:sfd_start])) if span else 0.0
    return x, y, sfd_start, polarity, dc


def decode_burst(z, start, end, noise):
    """Returns a dict for the burst. "status" is "frame" only if a PHR was read."""
    pad = 2 * SPS
    x = z[max(0, start - pad):min(len(z), end + pad)]
    result = {"t": start / CHANNEL_RATE, "dur": (end - start) / CHANNEL_RATE, "status": "no-sync"}

    coarse = np.angle(np.sum(x[1:] * np.conj(x[:-1]))) * CHANNEL_RATE / (2 * np.pi)
    first = find_sync(x, coarse, SEARCH_CUTOFF)
    if first is None:
        return result
    offset = coarse + first[4]
    x, y, sfd_start, polarity, dc = find_sync(x, offset, NARROW_CUTOFF)

    y = (y - dc) * polarity
    centre = sfd_start + (SPS - 1) / 2
    max_syms = int((len(y) - 1 - centre) // SPS)
    if max_syms < len(SFD_BITS) + PHR_BITS:
        return result
    soft = slice_symbols(y, centre, max_syms)
    bits = (soft > 0).astype(np.uint8)

    sfd_errors = int(np.sum(bits[:len(SFD_BITS)] != np.array(SFD_BITS)))
    if sfd_errors > SFD_MAX_ERRORS:
        return result

    body = bits[len(SFD_BITS):]
    phr = parse_phr(body[:PHR_BITS])
    power = float(np.mean(np.abs(x[pad:-pad]) ** 2))
    result.update(
        status="frame",
        sfd_errors=sfd_errors,
        polarity=polarity,
        cfo=offset + dc,
        phr=phr,
        head_bits="".join(str(b) for b in body[:64]),
        snr=10 * np.log10(max(power - noise, 1e-12) / noise) if noise else None,
    )
    if not phr["valid"]:
        result["status"] = "bad-phr"
        return result

    wanted = PHR_BITS + 8 * phr["len"]
    if len(body) < wanted:
        result["status"] = "truncated"
        result["psdu"] = bits_to_bytes(body[PHR_BITS:len(body) - (len(body) - PHR_BITS) % 8])
        return result

    data_soft = soft[len(SFD_BITS) + PHR_BITS:len(SFD_BITS) + wanted]
    result["deviation"] = measure_deviation(data_soft)
    result["psdu"] = bits_to_bytes(body[PHR_BITS:wanted])
    result["fcs_ok"] = fcs_ok(result["psdu"])
    return result


class FrameSync:
    """
    Finds frames in a live stream of soft symbols, in units of fdev, for the flowgraph.

    Every sync match is followed on its own, so a false match in the noise between
    bursts can't swallow a real frame. Either polarity is accepted. Bad FCS frames are
    only reported when the sync matched exactly: most other failures are just noise.
    """
    SYNC_MAX_ERRORS = 2

    def __init__(self):
        self.template = 2 * np.array(SYNC_BITS, dtype=np.int32) - 1
        self.buf = np.zeros(0, dtype=np.float32)
        self.base = 0           # stream position of buf[0]
        self.searched = 0       # stream position of the next sync start to test
        self.pending = []       # (sync start, errors, polarity)
        self.good_end = 0       # candidates starting before this sat inside a good frame

    def feed(self, soft):
        self.buf = np.concatenate((self.buf, soft))
        self._search()
        results = self._finish_candidates()

        oldest = min([c[0] for c in self.pending] + [self.searched])
        keep_from = max(oldest - PREAMBLE_HISTORY, self.base)
        self.buf = self.buf[keep_from - self.base:]
        self.base = keep_from
        return results

    def _search(self):
        window = self.buf[self.searched - self.base:]
        if len(window) < len(self.template):
            return
        corr = np.correlate(np.where(window > 0, 1, -1), self.template, "valid")
        errors = (len(self.template) - np.abs(corr)) // 2
        for i in np.flatnonzero(errors <= self.SYNC_MAX_ERRORS):
            self.pending.append((self.searched + int(i), int(errors[i]), 1 if corr[i] > 0 else -1))
        self.searched += len(corr)

    def _finish_candidates(self):
        results = []
        waiting = []
        end = self.base + len(self.buf)

        for cand in self.pending:
            start, errors, polarity = cand
            if start < self.good_end:
                continue
            phr_start = start + len(SYNC_BITS)
            if end < phr_start + PHR_BITS:
                waiting.append(cand)
                continue

            soft = self.buf[phr_start - self.base:] * polarity
            phr = parse_phr((soft[:PHR_BITS] > 0).astype(np.uint8))
            if not phr["valid"]:
                continue
            wanted = PHR_BITS + 8 * phr["len"]
            if len(soft) < wanted:
                waiting.append(cand)
                continue

            psdu = bits_to_bytes((soft[PHR_BITS:wanted] > 0).astype(np.uint8))
            good = fcs_ok(psdu)
            if good:
                self.good_end = phr_start + wanted
            if good or errors == 0:
                results.append({
                    "len": phr["len"],
                    "psdu": psdu,
                    "fcs_ok": good,
                    "polarity": polarity,
                    "sync_errors": errors,
                    "offset": self._offset(start),
                    "deviation": measure_deviation(soft[PHR_BITS:wanted] * DEVIATION),
                    "snr": estimate_snr(soft[PHR_BITS:wanted]),
                })

        self.pending = waiting
        return results

    def _offset(self, start):
        """Mean frequency over whole 01 pairs of preamble: the offset still in the signal."""
        i = start + SYNC_PREAMBLE - self.base
        n = min(i, PREAMBLE_HISTORY + SYNC_PREAMBLE) // 2 * 2
        return float(np.mean(self.buf[i - n:i])) * DEVIATION


def decode_capture(path, rate=8e6, tune=905.5e6, freq=905e6):
    z = read_channel(path, rate, freq - tune)
    bursts = find_bursts(z)
    noise = noise_power(z, bursts)
    return [decode_burst(z, s, e, noise) for s, e in bursts]


def format_frame(i, r, pattern, app=None):
    line = f"[{i:3}] t={r['t']:8.3f}s dur={r['dur'] * 1e3:6.1f}ms {r['status']:9}"
    if r["status"] == "no-sync":
        return line

    snr = f"{r['snr']:5.1f}dB" if r["snr"] is not None else "   n/a"
    line += f" sfd_err={r['sfd_errors']} pol={'+' if r['polarity'] > 0 else '-'} snr={snr} cfo={r['cfo'] / 1e3:+6.1f}kHz"
    line += f" len={r['phr']['len']}"
    if "fcs_ok" in r:
        line += f" fcs={'ok ' if r['fcs_ok'] else 'BAD'} dev={r['deviation'] / 1e3:4.1f}kHz"
        if app:
            return f"{line} {format_app(app)}"
        if pattern:
            errors, total = count_bit_errors(r["psdu"][:-FCS_LEN], pattern)
            line += f" bit_err={errors}/{total}"
        line += f" {r['psdu'][:32]!r}"
    else:
        line += f" bits={r['head_bits']}"
    return line


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", help="signed 8-bit IQ file from hackrf_transfer -r")
    ap.add_argument("--rate", type=float, default=8e6, help="capture sample rate, Hz")
    ap.add_argument("--tune", type=float, default=905.5e6, help="capture centre frequency, Hz")
    ap.add_argument("--freq", type=float, default=905e6, help="signal frequency, Hz")
    ap.add_argument("--expect", choices=sorted(PATTERNS) + ["greenbear"],
                    help="count bit errors against a known payload, or parse usb-bridge's Green Bear packets")
    ap.add_argument("--fcs-search", action="store_true", help="on a bad FCS, try the CRC-32 variants")
    ap.add_argument("--hex", action="store_true", help="print the first 64 PSDU bytes of each frame")
    args = ap.parse_args()

    pattern = PATTERNS.get(args.expect)
    results = decode_capture(args.capture, args.rate, args.tune, args.freq)
    link = AppLink()
    gb_link = GreenBearLink() if args.expect == "greenbear" else None

    for i, r in enumerate(results, 1):
        if gb_link:
            gb = gb_link.update(r["psdu"], r["fcs_ok"]) if "fcs_ok" in r else None
            print(format_frame(i, r, pattern))
            for line in format_greenbear(gb) if gb else []:
                print(line)
        else:
            app = link.update(r["psdu"], r["fcs_ok"]) if "fcs_ok" in r else None
            print(format_frame(i, r, pattern, app))
        if args.hex and "psdu" in r:
            print(f"      {r['psdu'][:64].hex(' ')}")
        if args.fcs_search and r.get("fcs_ok") is False:
            print(f"      fcs-search: {fcs_search(r['psdu']) or 'no variant matches'}")

    frames = sum("fcs_ok" in r for r in results)
    good = sum(r.get("fcs_ok", False) for r in results)
    print(f"bursts={len(results)} frames={frames} fcs_ok={good}")
    if link.frames or link.unidentified:
        print(link.summary())
    if gb_link:
        print(gb_link.summary())
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
