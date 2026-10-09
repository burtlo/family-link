#!/usr/bin/env python3
"""You on the Mac: live PTT through the server to a box running h11/h12.

This is the parent-phone stand-in. Audio does not go box↔box on Wi-Fi.
The server copies 20 ms PCM frames to whoever does not hold the floor.

  python -m demos.server.06_audio_relay.server --host 0.0.0.0 --port 8080
  python demos/parent/live_ptt.py --base-url http://192.168.x.x:8080
  make flash DEMO=h11   # start this after the Python side is waiting

Default: wait for the box invite, send a short tone (to the box speaker),
then capture mute-held audio from the box into data/parent/from-box.wav.

  --send-wav path.wav   stream that file to the box instead of a tone
  --invite              you start the hangout (box must accept / also handles ring)
  --as box-b            which registry device you are (peer of the flashed box)
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import websockets

from demos.server._shared.registry import load_devices

FRAME = 640
RATE = 16000
if os.environ.get("FAMILY_LINK_ROOT"):
    ROOT = Path(os.environ["FAMILY_LINK_ROOT"])
OUT_DIR = ROOT / "data" / "parent"


def ws_url(base: str) -> str:
    base = base.rstrip("/")
    if base.startswith("https://"):
        return "wss://" + base.removeprefix("https://") + "/v1/ws"
    if base.startswith("http://"):
        return "ws://" + base.removeprefix("http://") + "/v1/ws"
    return base + "/v1/ws"


def pcm_from_wav(path: Path) -> bytes:
    with wave.open(str(path), "rb") as wav:
        if wav.getsampwidth() != 2:
            raise SystemExit(f"{path} must be 16-bit PCM")
        ch = wav.getnchannels()
        rate = wav.getframerate()
        raw = wav.readframes(wav.getnframes())
    samples = memoryview(raw).cast("h")
    if ch == 2:
        mono = bytearray()
        for i in range(0, len(samples) - 1, 2):
            v = (int(samples[i]) + int(samples[i + 1])) // 2
            mono += int(v).to_bytes(2, "little", signed=True)
        raw = bytes(mono)
    if rate != RATE:
        raise SystemExit(f"{path} must be {RATE} Hz (got {rate}). Convert with ffmpeg.")
    return bytes(raw)


def tone_pcm(seconds: float = 0.8) -> bytes:
    import math

    n = int(RATE * seconds)
    out = bytearray()
    for i in range(n):
        v = int(9000 * math.sin(2 * math.pi * 440 * i / RATE))
        out += v.to_bytes(2, "little", signed=True)
    return bytes(out)


def frames_of(pcm: bytes) -> list[bytes]:
    if len(pcm) % 2:
        pcm = pcm + b"\x00"
    pad = (-len(pcm)) % FRAME
    if pad:
        pcm = pcm + b"\x00" * pad
    return [pcm[i : i + FRAME] for i in range(0, len(pcm), FRAME)]


def write_wav(path: Path, pcm: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(pcm)


async def send_json(ws, body: dict) -> None:
    await ws.send(json.dumps(body))


async def run(args: argparse.Namespace) -> int:
    devices = load_devices()
    me = devices[args.as_id]
    url = ws_url(args.base_url)
    if args.send_wav:
        pcm = pcm_from_wav(Path(args.send_wav).expanduser())
        label = args.send_wav
    else:
        pcm = tone_pcm(0.8)
        label = "440 Hz tone (not your voice)"
    chunks = frames_of(pcm)
    capture = bytearray()
    dest = Path(args.capture).expanduser() if args.capture else OUT_DIR / "from-box.wav"

    print(f"parent live_ptt as {me.id} -> {url}", flush=True)
    print(f"will send {len(chunks)} frames ({label}) TO the box", flush=True)
    print("start this before flashing h11, or while the box is waiting.", flush=True)

    async with websockets.connect(url) as ws:
        await send_json(ws, {"type": "hello", "device_id": me.id, "token": me.token})
        hello = json.loads(await ws.recv())
        if hello.get("type") != "hello_ok":
            print(f"FAIL hello {hello}")
            return 1

        if args.invite:
            await send_json(ws, {"type": "invite"})
            print("invited peer; waiting session_start…", flush=True)
        else:
            print("waiting for ring from the box…", flush=True)

        session = False
        sent = False
        while True:
            msg = await asyncio.wait_for(ws.recv(), timeout=120.0)
            if isinstance(msg, bytes):
                capture.extend(msg)
                print(f"FROM box {len(capture)} bytes", flush=True)
                if sent and len(capture) >= FRAME * args.min_from_box:
                    break
                continue
            body = json.loads(msg)
            kind = body.get("type")
            print(f"ws {body}", flush=True)
            if kind == "ring":
                await send_json(ws, {"type": "accept"})
            elif kind == "session_start":
                session = True
            elif kind == "floor" and session and not sent and body.get("holder") in (None, me.id):
                if body.get("holder") is None:
                    await send_json(ws, {"type": "floor_request"})
                    continue
                print(f"floor held — streaming {len(chunks)} frames to the box", flush=True)
                for chunk in chunks:
                    await ws.send(chunk)
                    await asyncio.sleep(0.02)
                await send_json(ws, {"type": "floor_release"})
                sent = True
                print("sent. hold mute on the box to talk back.", flush=True)
                if args.min_from_box <= 0:
                    break

        write_wav(dest, bytes(capture))
        print(f"wrote {dest} ({len(capture)} bytes FROM the box)", flush=True)
        print("-- PASS live_ptt")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--as", dest="as_id", default="box-b", help="registry id (you)")
    parser.add_argument("--send-wav", help="16 kHz s16le mono WAV to stream TO the box")
    parser.add_argument("--capture", help="WAV path for audio FROM the box")
    parser.add_argument("--invite", action="store_true", help="you start; default waits for box invite")
    parser.add_argument("--min-from-box", type=int, default=10, help="PCM frames to wait for from the box (0=send only)")
    args = parser.parse_args()
    try:
        return asyncio.run(run(args))
    except Exception as exc:
        print(f"FAIL {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
