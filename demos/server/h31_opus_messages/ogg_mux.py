"""Stream length-prefixed Opus packets into an Ogg Opus file (no full-RAM buffer)."""

from __future__ import annotations

import struct
from pathlib import Path
from typing import BinaryIO, Iterable, Iterator

OGG_CRC_POLY = 0x04C11DB7
FRAME_GRANULE_48K = 960  # 20 ms @ 48 kHz (Opus default)


def _crc32_ogg(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ OGG_CRC_POLY) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def _segments_for_payload(payload: bytes) -> bytes:
    segs: list[int] = []
    off = 0
    while off < len(payload):
        n = min(255, len(payload) - off)
        segs.append(n)
        off += n
    if not segs:
        segs = [0]
    return bytes(segs)


def _build_page(
    payload: bytes,
    header_type: int,
    granule: int,
    serial: int,
    seqno: int,
) -> bytes:
    segments = _segments_for_payload(payload)
    header = bytearray(b"OggS")
    header += struct.pack("<B", 0)
    header += struct.pack("<B", header_type)
    header += struct.pack("<Q", granule)
    header += struct.pack("<I", serial)
    header += struct.pack("<I", seqno)
    header += struct.pack("<I", 0)
    header += struct.pack("<B", len(segments))
    header += segments
    page = header + payload
    crc = _crc32_ogg(page)
    page = page[:22] + struct.pack("<I", crc) + page[26:]
    return page


def _opus_head(sample_rate: int = 16000, channels: int = 1) -> bytes:
    body = bytearray(b"OpusHead")
    body += struct.pack("<B", 1)
    body += struct.pack("<B", channels)
    body += struct.pack("<H", 312)  # pre-skip
    body += struct.pack("<I", sample_rate)
    body += struct.pack("<h", 0)
    body += struct.pack("<B", 0)
    return bytes(body)


def _opus_tags() -> bytes:
    vendor = b"family-link-h31"
    body = bytearray(b"OpusTags")
    body += struct.pack("<I", len(vendor))
    body += vendor
    body += struct.pack("<I", 0)
    return bytes(body)


def iter_length_prefixed_packets(stream: BinaryIO) -> Iterator[bytes]:
    while True:
        hdr = stream.read(2)
        if len(hdr) < 2:
            break
        plen = hdr[0] | (hdr[1] << 8)
        if plen <= 0:
            break
        pkt = stream.read(plen)
        if len(pkt) != plen:
            break
        yield pkt


def packets_from_chunk_files(paths: Iterable[Path]) -> Iterator[bytes]:
    for path in paths:
        with path.open("rb") as fh:
            yield from iter_length_prefixed_packets(fh)


def mux_packets_to_ogg(
    packets: Iterable[bytes],
    out_path: Path,
    *,
    sample_rate: int = 16000,
) -> tuple[int, int]:
    """Write Ogg Opus; returns (frame_count, duration_ms)."""
    serial = 0x6C31_0F31
    seqno = 0
    granule = 0
    frames = 0
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as out:
        out.write(_build_page(_opus_head(sample_rate), 0x02, 0, serial, seqno))
        seqno += 1
        out.write(_build_page(_opus_tags(), 0x00, 0, serial, seqno))
        seqno += 1
        last_pkt: bytes | None = None
        for pkt in packets:
            frames += 1
            granule += FRAME_GRANULE_48K
            htype = 0x00
            out.write(_build_page(pkt, htype, granule, serial, seqno))
            seqno += 1
            last_pkt = pkt
        if frames == 0:
            out.write(_build_page(b"", 0x04, 0, serial, seqno))
        else:
            # Re-write last page with EOS — append tiny EOS page instead
            out.write(_build_page(b"", 0x04, granule, serial, seqno))
    duration_ms = frames * 20
    return frames, duration_ms


def duration_ms_from_granule(granule: int) -> int:
    return int(granule * 1000 / 48000)
