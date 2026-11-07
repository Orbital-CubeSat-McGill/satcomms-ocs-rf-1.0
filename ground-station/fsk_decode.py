#!/usr/bin/env python3
"""
Offline decoder for the hat's MR-FSK frames, from a hackrf_transfer capture.

Capture (8 MS/s, tuned 500 kHz off the signal to stay clear of the DC spike):

    hackrf_transfer -r capture.iq -f 915500000 -s 8000000 -a 0 -l 16 -g 24 -n 80000000

Decode:

    python3 ground-station/fsk_decode.py capture.iq --expect tx-burst --fcs-search

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

TIMING_GAIN = 0.03
BURST_THRESHOLD = 4.0       # power over the noise floor: 6 dB, in the wide filter's band
BURST_AVG_S = 0.5e-3
BURST_MERGE_S = 1e-3
BURST_MIN_S = 2e-3

PATTERNS = {"tx-burst": b"OCS-RF-1 TEST BURST 0123456789 "}


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
                })

        self.pending = waiting
        return results

    def _offset(self, start):
        """Mean frequency over whole 01 pairs of preamble: the offset still in the signal."""
        i = start + SYNC_PREAMBLE - self.base
        n = min(i, PREAMBLE_HISTORY + SYNC_PREAMBLE) // 2 * 2
        return float(np.mean(self.buf[i - n:i])) * DEVIATION


def decode_capture(path, rate=8e6, tune=915.5e6, freq=915e6):
    z = read_channel(path, rate, freq - tune)
    bursts = find_bursts(z)
    noise = noise_power(z, bursts)
    return [decode_burst(z, s, e, noise) for s, e in bursts]


def format_frame(i, r, pattern):
    line = f"[{i:3}] t={r['t']:8.3f}s dur={r['dur'] * 1e3:6.1f}ms {r['status']:9}"
    if r["status"] == "no-sync":
        return line

    snr = f"{r['snr']:5.1f}dB" if r["snr"] is not None else "   n/a"
    line += f" sfd_err={r['sfd_errors']} pol={'+' if r['polarity'] > 0 else '-'} snr={snr} cfo={r['cfo'] / 1e3:+6.1f}kHz"
    line += f" len={r['phr']['len']}"
    if "fcs_ok" in r:
        line += f" fcs={'ok ' if r['fcs_ok'] else 'BAD'} dev={r['deviation'] / 1e3:4.1f}kHz"
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
    ap.add_argument("--tune", type=float, default=915.5e6, help="capture centre frequency, Hz")
    ap.add_argument("--freq", type=float, default=915e6, help="signal frequency, Hz")
    ap.add_argument("--expect", choices=sorted(PATTERNS) + ["greenbear"], help="tx-burst bit-error check, or greenbear header parse")
    ap.add_argument("--fcs-search", action="store_true", help="on a bad FCS, try the CRC-32 variants")
    ap.add_argument("--hex", action="store_true", help="print the first 64 PSDU bytes of each frame")
    args = ap.parse_args()

    pattern = PATTERNS.get(args.expect) if args.expect != "greenbear" else None
    results = decode_capture(args.capture, args.rate, args.tune, args.freq)

    for i, r in enumerate(results, 1):
        print(format_frame(i, r, pattern))
        if args.expect == "greenbear" and "psdu" in r and r.get("fcs_ok"):
            logical = r["psdu"][:-FCS_LEN]
            try:
                pkt = gbp.parse(logical)
                print(f"      greenbear {gbp.format_header(pkt)}")
            except ValueError as e:
                print(f"      greenbear parse: {e}")
        if args.hex and "psdu" in r:
            print(f"      {r['psdu'][:64].hex(' ')}")
        if args.fcs_search and r.get("fcs_ok") is False:
            print(f"      fcs-search: {fcs_search(r['psdu']) or 'no variant matches'}")

    frames = sum("fcs_ok" in r for r in results)
    good = sum(r.get("fcs_ok", False) for r in results)
    print(f"bursts={len(results)} frames={frames} fcs_ok={good}")
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
