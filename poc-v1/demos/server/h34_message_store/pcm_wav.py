"""Stream PCM chunk files into a canonical WAV without loading the full message."""

from __future__ import annotations

import struct
import os
from pathlib import Path
from typing import Callable


def _wav_header(data_bytes: int, sample_rate: int, channels: int) -> bytes:
    byte_rate = sample_rate * channels * 2
    block_align = channels * 2
    riff_size = 36 + data_bytes
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        riff_size,
        b"WAVE",
        b"fmt ",
        16,
        1,
        channels,
        sample_rate,
        byte_rate,
        block_align,
        16,
        b"data",
        data_bytes,
    )


def finalize_pcm_chunks_to_wav(
    chunk_paths: list[Path],
    dest_wav: Path,
    *,
    sample_rate: int = 16000,
    channels: int = 1,
    on_first_block: Callable[[], None] | None = None,
) -> int:
    """Write chunk PCM bodies sequentially; return total PCM data bytes."""
    if sample_rate != 16000 or channels != 1:
        raise ValueError("PCM WAV requires 16 kHz mono")
    dest_wav.parent.mkdir(parents=True, exist_ok=True)
    pcm_bytes = 0
    first_block = True
    with dest_wav.open("wb") as out:
        out.write(_wav_header(0, sample_rate, channels))
        for path in chunk_paths:
            with path.open("rb") as src:
                while True:
                    block = src.read(65536)
                    if not block:
                        break
                    if len(block) % 2:
                        raise ValueError("pcm chunk has incomplete sample")
                    pcm_bytes += len(block)
                    out.write(block)
                    if first_block:
                        first_block = False
                        if on_first_block:
                            on_first_block()
        out.seek(0)
        out.write(_wav_header(pcm_bytes, sample_rate, channels))
        out.flush()
        os.fsync(out.fileno())
    return pcm_bytes
