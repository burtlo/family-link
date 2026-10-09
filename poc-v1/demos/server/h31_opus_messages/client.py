#!/usr/bin/env python3
"""Automated h31 Opus chunk lifecycle tests."""

from __future__ import annotations

import argparse
import hashlib
import math
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import httpx

from demos.server._shared.registry import load_devices

ROOT = Path(__file__).resolve().parents[3]
FIXTURE_WAV = ROOT / "demos/server/h30_opus/fixtures/beep_1s_16k_mono.wav"


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_beep_wav(path: Path, seconds: float = 2.0) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    rate = 16000
    n = int(rate * seconds)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        frames = bytearray()
        for i in range(n):
            sample = int(0.2 * 32767 * math.sin(2 * math.pi * 440.0 * i / rate))
            frames += struct.pack("<h", sample)
        wf.writeframes(frames)


def wav_to_length_prefixed_opus(wav_path: Path, seconds: float) -> bytes:
    """Encode WAV segment to uint16-le length-prefixed Opus packets (20 ms frames)."""
    ff = shutil.which("ffmpeg")
    if not ff:
        # Synthetic packets for CI without ffmpeg (mux still builds Ogg pages).
        frames = int(seconds * 1000 / 20)
        out = bytearray()
        for i in range(frames):
            pkt = bytes([0xFC, 0xFF, 0xFE]) + bytes([i & 0xFF])
            out += struct.pack("<H", len(pkt))
            out += pkt
        return bytes(out)

    with tempfile.TemporaryDirectory() as td:
        ogg = Path(td) / "chunk.ogg"
        subprocess.check_call(
            [
                ff,
                "-y",
                "-i",
                str(wav_path),
                "-t",
                str(seconds),
                "-c:a",
                "libopus",
                "-b:a",
                "16k",
                "-frame_duration",
                "20",
                "-application",
                "voip",
                str(ogg),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return _ogg_to_length_prefixed(ogg.read_bytes())


def _ogg_to_length_prefixed(ogg: bytes) -> bytes:
    out = bytearray()
    pos = 0
    while pos + 27 <= len(ogg):
        if ogg[pos : pos + 4] != b"OggS":
            pos += 1
            continue
        seg_count = ogg[pos + 26]
        seg_table = ogg[pos + 27 : pos + 27 + seg_count]
        body_start = pos + 27 + seg_count
        body_len = sum(seg_table)
        body = ogg[body_start : body_start + body_len]
        pos = body_start + body_len
        if body.startswith(b"OpusHead") or body.startswith(b"OpusTags"):
            continue
        off = 0
        pkt = bytearray()
        for seg_len in seg_table:
            pkt += body[off : off + seg_len]
            off += seg_len
            if seg_len < 255:
                if pkt:
                    out += struct.pack("<H", len(pkt))
                    out += pkt
                pkt = bytearray()
    if not out:
        raise RuntimeError("no opus packets extracted from ogg")
    return bytes(out)


def make_chunk(seconds: float = 2.0) -> bytes:
    if not FIXTURE_WAV.is_file():
        write_beep_wav(FIXTURE_WAV, seconds=max(seconds, 1.0))
    return wav_to_length_prefixed_opus(FIXTURE_WAV, seconds)


def put_chunk(
    http: httpx.Client,
    base: str,
    token: str,
    message_id: str,
    seq: int,
    body: bytes,
    start_ms: int,
    duration_ms: int = 2000,
) -> httpx.Response:
    headers = {
        **auth(token),
        "Content-Type": "application/octet-stream",
        "X-Chunk-SHA256": sha256_hex(body),
        "X-Chunk-Start-Ms": str(start_ms),
        "X-Chunk-Duration-Ms": str(duration_ms),
        "X-Chunk-Bytes": str(len(body)),
    }
    return http.put(
        f"{base}/v1/messages/{message_id}/audio/{seq}",
        content=body,
        headers=headers,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    devices = load_devices()
    sender = devices["box-a"]

    chunk = make_chunk(2.0)
    chunk_b = make_chunk(2.0)
    if chunk == chunk_b:
        chunk_b = chunk + b"\x00"  # distinct body for conflict test

    with httpx.Client(timeout=15.0) as http:
        created = http.post(
            f"{base}/v1/messages",
            json={
                "protocol": "family-message/1",
                "to_user_id": devices["box-b"].id,
                "audio": {
                    "codec": "opus",
                    "sample_rate_hz": 16000,
                    "channels": 1,
                    "target_chunk_ms": 2000,
                },
            },
            headers=auth(sender.token),
        )
        if created.status_code != 201:
            return fail(f"create {created.status_code} {created.text}")
        message_id = created.json()["message_id"]

        # Out-of-order: seq 2 then 0 then 1
        r2 = put_chunk(http, base, sender.token, message_id, 2, chunk, 4000)
        if r2.status_code != 201:
            return fail(f"put seq2 {r2.status_code}")
        r0 = put_chunk(http, base, sender.token, message_id, 0, chunk, 0)
        if r0.status_code != 201:
            return fail(f"put seq0 {r0.status_code}")
        r1 = put_chunk(http, base, sender.token, message_id, 1, chunk, 2000)
        if r1.status_code != 201:
            return fail(f"put seq1 {r1.status_code}")

        dup = put_chunk(http, base, sender.token, message_id, 1, chunk, 2000)
        if dup.status_code != 200:
            return fail(f"duplicate chunk expected 200 got {dup.status_code}")

        conflict = put_chunk(http, base, sender.token, message_id, 1, chunk_b, 2000)
        if conflict.status_code != 409:
            return fail(f"conflict expected 409 got {conflict.status_code}")

        status = http.get(
            f"{base}/v1/messages/{message_id}/upload", headers=auth(sender.token)
        )
        if status.status_code != 200:
            return fail(f"upload status {status.status_code}")
        received = status.json()["audio"]["received"]
        if received != [0, 1, 2]:
            return fail(f"received sequences {received}")

        missing_block = http.post(
            f"{base}/v1/messages/{message_id}/complete",
            json={
                "audio_chunks": 4,
                "duration_ms": 8000,
                "closed_reason": "button",
            },
            headers=auth(sender.token),
        )
        if missing_block.status_code != 409:
            return fail(f"missing finalize expected 409 got {missing_block.status_code}")
        miss_body = missing_block.json()
        if "missing" not in miss_body or 3 not in miss_body["missing"]:
            return fail(f"missing list wrong: {miss_body}")

        done = http.post(
            f"{base}/v1/messages/{message_id}/complete",
            json={
                "audio_chunks": 3,
                "duration_ms": 6000,
                "closed_reason": "button",
            },
            headers=auth(sender.token),
        )
        if done.status_code not in (200, 201):
            return fail(f"complete {done.status_code} {done.text}")

        audio = http.get(
            f"{base}/v1/messages/{message_id}/audio", headers=auth(sender.token)
        )
        if audio.status_code != 200:
            return fail(f"GET audio {audio.status_code}")
        if not audio.headers.get("accept-ranges", "").lower().startswith("bytes"):
            return fail("missing Accept-Ranges")
        clen = int(audio.headers.get("content-length", "0"))
        if clen < 32 or audio.content[:4] != b"OggS":
            return fail("not ogg media")

        mid = clen // 2
        partial = http.get(
            f"{base}/v1/messages/{message_id}/audio",
            headers={**auth(sender.token), "Range": f"bytes={mid}-"}
        )
        if partial.status_code != 206:
            return fail(f"range GET expected 206 got {partial.status_code}")
        cr = partial.headers.get("content-range", "")
        if not cr.startswith(f"bytes {mid}-"):
            return fail(f"bad content-range {cr}")

        index = http.get(
            f"{base}/v1/messages/{message_id}/index.json", headers=auth(sender.token)
        )
        if index.status_code != 200:
            return fail(f"index.json {index.status_code}")
        idx = index.json()
        if idx.get("duration_ms", 0) < 1000:
            return fail(f"index duration {idx}")

        measured = len(chunk) * 3
        print(
            f"-- PASS h31_opus_messages message_id={message_id} "
            f"chunk_bytes={len(chunk)} total_upload={measured} ogg_bytes={clen}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
