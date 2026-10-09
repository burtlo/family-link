#!/usr/bin/env python3
"""Exercise async text + audio messages against a running 03_messages server."""

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

BEEP_PATH = Path(__file__).resolve().parent / "beep.wav"
AUTH_A = {"Authorization": "Bearer change-me-a"}
AUTH_B = {"Authorization": "Bearer change-me-b"}


def _write_beep(path: Path) -> None:
    rate = 16000
    nframes = int(rate * 0.2)
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    ws_url = base.replace("http://", "ws://", 1).replace("https://", "wss://", 1) + "/v1/ws"
    beep_bytes = BEEP_PATH.read_bytes()

    with httpx.Client(timeout=5.0) as http:
        with connect(ws_url, open_timeout=5) as ws:
            ws.send(
                json.dumps(
                    {"type": "hello", "device_id": "box-b", "token": "change-me-b"}
                )
            )
            hello = json.loads(ws.recv(timeout=5))
            if hello.get("type") != "hello_ok":
                return fail(f"expected hello_ok got {hello}")

            posted = http.post(
                f"{base}/v1/messages",
                data={"kind": "text", "text": "hello from a"},
                headers=AUTH_A,
            )
            if posted.status_code != 200:
                return fail(f"text POST expected 200 got {posted.status_code}")
            body = posted.json()
            if (
                body.get("seq") != 1
                or body.get("kind") != "text"
                or body.get("from") != "box-a"
                or body.get("to") != "box-b"
            ):
                return fail(f"text POST body mismatch: {body}")

            event = json.loads(ws.recv(timeout=5))
            if (
                event.get("type") != "inbox"
                or event.get("seq") != 1
                or event.get("kind") != "text"
                or event.get("from") != "box-a"
            ):
                return fail(f"inbox event mismatch: {event}")

            listed = http.get(f"{base}/v1/messages", headers=AUTH_B)
            if listed.status_code != 200:
                return fail(f"GET messages expected 200 got {listed.status_code}")
            messages = listed.json()
            if messages != [
                {"seq": 1, "kind": "text", "from": "box-a", "text": "hello from a"}
            ]:
                return fail(f"inbox list mismatch: {messages}")

            audio = http.post(
                f"{base}/v1/messages",
                data={"kind": "audio"},
                files={"blob": ("beep.wav", beep_bytes, "audio/wav")},
                headers=AUTH_A,
            )
            if audio.status_code != 200:
                return fail(f"audio POST expected 200 got {audio.status_code}")
            audio_body = audio.json()
            if (
                audio_body.get("seq") != 2
                or audio_body.get("kind") != "audio"
                or audio_body.get("from") != "box-a"
                or audio_body.get("to") != "box-b"
            ):
                return fail(f"audio POST body mismatch: {audio_body}")

            event = json.loads(ws.recv(timeout=5))
            if (
                event.get("type") != "inbox"
                or event.get("seq") != 2
                or event.get("kind") != "audio"
                or event.get("from") != "box-a"
            ):
                return fail(f"audio inbox event mismatch: {event}")

            blob = http.get(f"{base}/v1/messages/2/blob", headers=AUTH_B)
            if blob.status_code != 200:
                return fail(f"GET blob expected 200 got {blob.status_code}")
            if blob.content != beep_bytes:
                return fail(
                    f"blob bytes mismatch: got {len(blob.content)} expected {len(beep_bytes)}"
                )

            unauth = http.post(
                f"{base}/v1/messages",
                data={"kind": "text", "text": "nope"},
            )
            if unauth.status_code != 401:
                return fail(f"POST without auth expected 401 got {unauth.status_code}")

    print("-- PASS 03_messages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
