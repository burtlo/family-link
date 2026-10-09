#!/usr/bin/env python3
"""Mazi sends a WAV to Arlo; Arlo's twin WS gets inbox and fetches the blob."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import wave
from pathlib import Path

import httpx
from websockets.sync.client import connect

from demos.server._shared.registry import load_devices

BEEP_PATH = Path(__file__).resolve().parent / "beep.wav"


def _write_beep(path: Path) -> None:
    rate = 16000
    nframes = int(rate * 0.25)
    freq = 880.0
    amp = 12000
    with wave.open(str(path), "w") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        frames = b"".join(
            struct.pack("<h", int(amp * math.sin(2 * math.pi * freq * i / rate)))
            for i in range(nframes)
        )
        wav.writeframes(frames)


_write_beep(BEEP_PATH)


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    ws_url = base.replace("http://", "ws://", 1).replace("https://", "wss://", 1) + "/v1/ws"
    devices = load_devices()
    mazi = devices["box-a"]
    arlo = devices["box-b"]
    beep_bytes = BEEP_PATH.read_bytes()

    with httpx.Client(timeout=8.0) as http:
        with connect(ws_url, open_timeout=8) as ws:
            ws.send(
                json.dumps(
                    {"type": "hello", "device_id": arlo.id, "token": arlo.token}
                )
            )
            hello = json.loads(ws.recv(timeout=8))
            if hello.get("type") != "hello_ok" or hello.get("peer_id") != mazi.id:
                return fail(f"hello_ok mismatch: {hello}")

            posted = http.post(
                f"{base}/v1/messages",
                data={"kind": "audio"},
                files={"blob": ("clip.wav", beep_bytes, "audio/wav")},
                headers=auth(mazi.token),
            )
            if posted.status_code != 200:
                return fail(f"POST {posted.status_code} {posted.text}")
            body = posted.json()
            if (
                body.get("seq") != 1
                or body.get("kind") != "audio"
                or body.get("from") != mazi.id
                or body.get("to") != arlo.id
            ):
                return fail(f"POST body mismatch: {body}")

            event = json.loads(ws.recv(timeout=8))
            if (
                event.get("type") != "inbox"
                or event.get("seq") != 1
                or event.get("kind") != "audio"
                or event.get("from") != mazi.id
            ):
                return fail(f"inbox event mismatch: {event}")

            blob = http.get(f"{base}/v1/messages/1/blob", headers=auth(arlo.token))
            if blob.status_code != 200:
                return fail(f"GET blob {blob.status_code}")
            if blob.content != beep_bytes:
                return fail("blob bytes mismatch")

            reply = http.post(
                f"{base}/v1/messages",
                data={"kind": "audio"},
                files={"blob": ("clip.wav", beep_bytes, "audio/wav")},
                headers=auth(arlo.token),
            )
            if reply.status_code != 200:
                return fail(f"reply POST {reply.status_code} {reply.text}")
            reply_body = reply.json()
            if reply_body.get("to") != mazi.id or reply_body.get("seq") != 1:
                return fail(f"reply body mismatch: {reply_body}")

    print("-- PASS h29_pingpong")
    return 0


if __name__ == "__main__":
    sys.exit(main())
