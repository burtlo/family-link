#!/usr/bin/env python3
"""Audio-message fixture for firmware h18 (playback screen).

Not protocol demo 07. 03_messages stays the inbox/seq contract.
This host serves a catalog: a generated melody, then every saved voice
clip copied into assets/. Boot on the box walks that list.

  python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080
  make flash DEMO=h18
"""

from __future__ import annotations

import argparse
import math
import shutil
import struct
import wave
from io import BytesIO
from pathlib import Path

import uvicorn
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import Response

app = FastAPI()

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ASSETS = HERE / "assets"
INBOX_WAV = ROOT / "data" / "03_messages"

RATE = 16000
# Long enough that play/pause and the bar are obvious on the desk.
DURATION_S = 4.5
START_MS = 800


def _melody_wav() -> tuple[bytes, int]:
    n = int(RATE * DURATION_S)
    notes = (392.0, 494.0, 587.0, 784.0, 587.0, 494.0)
    hop = n // len(notes)
    samples = []
    for i in range(n):
        freq = notes[min(i // hop, len(notes) - 1)]
        env = 0.35 + 0.65 * min(1.0, (i % hop) / 400.0)
        if hop - (i % hop) < 200:
            env *= (hop - (i % hop)) / 200.0
        samples.append(
            int(24000 * env * math.sin(2 * math.pi * freq * i / RATE))
        )
    buf = BytesIO()
    with wave.open(buf, "w") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(b"".join(struct.pack("<h", s) for s in samples))
    blob = buf.getvalue()
    duration_ms = int(round(n * 1000 / RATE))
    return blob, duration_ms


MELODY, MELODY_MS = _melody_wav()

# Inbox copies 1–4 were empty / unusable; keep them out of the catalog.
SKIP_VOICE_STEMS = {f"voice-{n:02d}" for n in range(1, 5)}


def _wav_sort_key(path: Path) -> tuple:
    stem = path.stem
    if stem.isdigit():
        return (0, int(stem), str(path))
    return (1, stem.lower(), str(path))


def _wav_duration_ms(path: Path) -> int:
    with wave.open(str(path), "r") as wav:
        n, rate = wav.getnframes(), wav.getframerate()
        return int(round(n * 1000 / rate)) if rate else 0


def import_inbox_wavs() -> None:
    """Copy saved inbox recordings into demo assets as voice-NN.wav."""
    ASSETS.mkdir(parents=True, exist_ok=True)
    if not INBOX_WAV.is_dir():
        return
    sources = sorted(INBOX_WAV.rglob("*.wav"), key=_wav_sort_key)
    for n, src in enumerate(sources, start=1):
        dest = ASSETS / f"voice-{n:02d}.wav"
        if dest.stem in SKIP_VOICE_STEMS:
            if dest.exists():
                dest.unlink()
            continue
        if dest.exists() and dest.stat().st_mtime >= src.stat().st_mtime:
            continue
        shutil.copy2(src, dest)


def catalog() -> list[dict]:
    import_inbox_wavs()
    items: list[dict] = [
        {
            "id": "tone-1",
            "sender": "Dad",
            "sent_at": "Sun 23 Aug, 3:04 PM",
            "blob": MELODY,
            "duration_ms": MELODY_MS,
            "position_ms": START_MS,
        }
    ]
    for path in sorted(ASSETS.glob("voice-*.wav")):
        if path.stem in SKIP_VOICE_STEMS:
            continue
        n = path.stem.split("-")[-1]
        items.append(
            {
                "id": path.stem,
                "sender": "Dad",
                "sent_at": f"voice {n}",
                "path": path,
                "duration_ms": _wav_duration_ms(path),
                "position_ms": 0,
            }
        )
    return items


def _item_bytes(item: dict) -> bytes:
    if "blob" in item:
        return item["blob"]
    return Path(item["path"]).read_bytes()


def message_body(item: dict, base: str, index: int, count: int) -> dict:
    return {
        "id": item["id"],
        "sender": item["sender"],
        "sent_at": item["sent_at"],
        "url": f"{base.rstrip('/')}/demo/h18/audio/{item['id']}",
        "duration_ms": item["duration_ms"],
        "position_ms": item["position_ms"],
        "read": False,
        "index": index,
        "count": count,
    }


def _by_id(clip_id: str) -> dict | None:
    for item in catalog():
        if item["id"] == clip_id:
            return item
    return None


@app.get("/demo/h18/message")
def get_message(request: Request, i: int = 0):
    items = catalog()
    if not items:
        raise HTTPException(status_code=500, detail="empty catalog")
    idx = i % len(items)
    base = str(request.base_url).rstrip("/")
    return message_body(items[idx], base, idx, len(items))


@app.get("/demo/h18/audio.wav")
def get_audio_alias():
    return Response(content=MELODY, media_type="audio/wav")


@app.get("/demo/h18/audio/{clip_id}")
def get_audio(clip_id: str):
    item = _by_id(clip_id)
    if item is None:
        raise HTTPException(status_code=404, detail="unknown clip")
    return Response(content=_item_bytes(item), media_type="audio/wav")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    n = len(catalog())
    print(f"h18 catalog: {n} message(s) (melody + voice clips in {ASSETS})")
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
