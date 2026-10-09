"""Packed sketch clip: magic + timed points. Keep in sync with firmware h27."""

from __future__ import annotations

import struct

MAGIC = b"FLSK"
VER = 1
HDR_FMT = "<4sBBH"  # magic, ver, pad, n
PT_FMT = "<HBBHH"  # t_ms, phase, pad, x, y
HDR_LEN = struct.calcsize(HDR_FMT)
PT_LEN = struct.calcsize(PT_FMT)
MAX_POINTS = 2048
MAX_MS = 30000

PHASE_DOWN = 0
PHASE_MOVE = 1
PHASE_UP = 2
PHASE_NAME = {PHASE_DOWN: "down", PHASE_MOVE: "move", PHASE_UP: "up"}


class SketchError(ValueError):
    pass


def pack_sketch(points: list[tuple[int, int, int, int]]) -> bytes:
    """points are (t_ms, phase, x, y)."""
    if len(points) > MAX_POINTS:
        raise SketchError(f"too many points ({len(points)})")
    body = struct.pack(HDR_FMT, MAGIC, VER, 0, len(points))
    for t, phase, x, y in points:
        body += struct.pack(PT_FMT, int(t) & 0xFFFF, int(phase) & 0xFF, 0, int(x), int(y))
    return body


def unpack_sketch(blob: bytes) -> list[tuple[int, int, int, int]]:
    if len(blob) < HDR_LEN:
        raise SketchError("truncated header")
    magic, ver, _pad, n = struct.unpack_from(HDR_FMT, blob, 0)
    if magic != MAGIC:
        raise SketchError("bad magic")
    if ver != VER:
        raise SketchError(f"unsupported version {ver}")
    if n > MAX_POINTS:
        raise SketchError(f"too many points ({n})")
    need = HDR_LEN + n * PT_LEN
    if len(blob) < need:
        raise SketchError("truncated points")
    out: list[tuple[int, int, int, int]] = []
    off = HDR_LEN
    last_t = 0
    for _ in range(n):
        t, phase, _p, x, y = struct.unpack_from(PT_FMT, blob, off)
        off += PT_LEN
        if phase > PHASE_UP:
            raise SketchError(f"bad phase {phase}")
        if t < last_t:
            raise SketchError("time went backwards")
        last_t = t
        x = max(0, min(319, x))
        y = max(0, min(239, y))
        out.append((t, phase, x, y))
    return out
