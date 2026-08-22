#!/usr/bin/env python3
"""You on the Mac: drop a voicemail into the box inbox (not a live stream).

Uses the same HTTP as the phone will: POST /v1/messages as the peer of the box.
Flash h09 on the box (or wait, then flash) to download and play.

  python -m demos.server.03_messages.server --host 0.0.0.0 --port 8080
  python demos/parent/send_voicemail.py --base-url http://192.168.x.x:8080
  python demos/parent/send_voicemail.py --base-url … --wav ~/clip.wav --as box-b
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
import wave
from io import BytesIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import httpx

from demos.server._shared.registry import load_devices

RATE = 16000


def beep_wav() -> bytes:
    n = int(RATE * 0.35)
    buf = BytesIO()
    with wave.open(buf, "w") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        frames = b"".join(
            struct.pack("<h", int(11000 * math.sin(2 * math.pi * 660 * i / RATE)))
            for i in range(n)
        )
        wav.writeframes(frames)
    return buf.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--as", dest="as_id", default="box-b", help="sender registry id")
    parser.add_argument("--wav", help="WAV to upload (default: generated beep)")
    parser.add_argument("--text", help="also send a text message")
    args = parser.parse_args()

    devices = load_devices()
    me = devices[args.as_id]
    base = args.base_url.rstrip("/")
    headers = {"Authorization": f"Bearer {me.token}"}

    if args.text:
        r = httpx.post(
            f"{base}/v1/messages",
            headers=headers,
            data={"kind": "text", "text": args.text},
            timeout=20.0,
        )
        print(f"text {r.status_code} {r.text}")
        r.raise_for_status()

    if args.wav:
        blob = Path(args.wav).expanduser().read_bytes()
        name = Path(args.wav).name
    else:
        blob = beep_wav()
        name = "beep.wav"

    r = httpx.post(
        f"{base}/v1/messages",
        headers=headers,
        data={"kind": "audio"},
        files={"blob": (name, blob, "audio/wav")},
        timeout=30.0,
    )
    print(f"audio {r.status_code} {r.text}")
    r.raise_for_status()
    print(f"inbox of peer {me.peer} got {name} ({len(blob)} bytes)")
    print("flash h09 on the box (as the peer) to play it.")
    print("-- PASS send_voicemail")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"FAIL {exc}")
        sys.exit(1)
