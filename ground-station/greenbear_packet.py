"""Green Bear logical packet codec for the ground station.

Copied from Ion/src/ion/greenbear/packet.py (+ config constants inlined).
KEEP IN SYNC with the Ion package and docs/greenbear-protocol.md.

This copy is fully implemented so the ground decoder works before the
BBG HITL slots (cobs/packet bodies) are filled.
"""

from __future__ import annotations

import binascii
import struct
from dataclasses import dataclass

CALLSIGN: bytes = b"VE3 JKB"
VERSION: int = 1
HEADER_SIZE: int = 16
CRC_SIZE: int = 2
MAX_CONTENT: int = 1024
CRC_INIT: int = 0xFFFF
CRC_CHECK: int = 0x29B1


@dataclass(frozen=True)
class Packet:
    callsign: bytes
    version: int
    seq: int
    time: int
    content: bytes


def crc16(data: bytes) -> int:
    return binascii.crc_hqx(data, CRC_INIT) & 0xFFFF


def build(seq: int, time: int, content: bytes) -> bytes:
    if not (0 <= seq <= 0xFFFF):
        raise ValueError(f"seq out of range: {seq}")
    if not (0 <= time <= 0xFFFFFFFF):
        raise ValueError(f"time out of range: {time}")
    if len(content) > MAX_CONTENT:
        raise ValueError(f"content too long: {len(content)} > {MAX_CONTENT}")

    body = (
        CALLSIGN
        + bytes([VERSION])
        + struct.pack("<H", seq)
        + struct.pack("<I", time)
        + struct.pack("<H", len(content))
        + content
    )
    return body + struct.pack("<H", crc16(body))


def parse(frame: bytes) -> Packet:
    if len(frame) < HEADER_SIZE + CRC_SIZE:
        raise ValueError("frame truncated")

    callsign = frame[:7]
    version = frame[7]
    seq, time, length = struct.unpack_from("<HIH", frame, 8)

    if callsign != CALLSIGN:
        raise ValueError(f"bad callsign: {callsign!r}")
    if version != VERSION:
        raise ValueError(f"bad version: {version}")
    if length > MAX_CONTENT:
        raise ValueError(f"length > MAX_CONTENT: {length}")

    expect = HEADER_SIZE + length + CRC_SIZE
    if len(frame) != expect:
        raise ValueError(f"length mismatch: got {len(frame)}, expect {expect}")

    content = frame[HEADER_SIZE : HEADER_SIZE + length]
    want = struct.unpack_from("<H", frame, HEADER_SIZE + length)[0]
    got = crc16(frame[: HEADER_SIZE + length])
    if got != want:
        raise ValueError(f"CRC mismatch: got 0x{got:04x} want 0x{want:04x}")

    return Packet(
        callsign=callsign,
        version=version,
        seq=seq,
        time=time,
        content=content,
    )


def format_header(pkt: Packet, *, crc_ok: bool = True) -> str:
    return (
        f"callsign={pkt.callsign!r} seq={pkt.seq} time={pkt.time} "
        f"len={len(pkt.content)} crc16={'ok' if crc_ok else 'BAD'}"
    )
