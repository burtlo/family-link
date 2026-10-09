#!/usr/bin/env python3
"""Upload two diary chunks; GET lists them with distinct datetimes."""

from __future__ import annotations

import argparse
import io
import math
import struct
import sys
import time
import wave

import httpx

from demos.server._shared.registry import load_devices

RATE = 16000


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def tone_wav(ms: int, freq: float) -> bytes:
    n = RATE * ms // 1000
    buf = io.BytesIO()
    with wave.open(buf, "w") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        frames = b"".join(
            struct.pack("<h", int(12000 * math.sin(2 * math.pi * freq * i / RATE)))
            for i in range(n)
        )
        wav.writeframes(frames)
    return buf.getvalue()


def post_chunk(
    client: httpx.Client, base: str, token: str, *, session: str, seq: int, blob: bytes
) -> dict:
    r = client.post(
        f"{base}/v1/diary",
        headers=auth(token),
        data={"session": session, "seq": str(seq)},
        files={"blob": ("chunk.wav", blob, "audio/wav")},
    )
    if r.status_code != 200:
        raise SystemExit(fail(f"POST /v1/diary {r.status_code} {r.text}"))
    body = r.json()
    if body.get("ok") is not True:
        raise SystemExit(fail(f"POST not ok: {body}"))
    for key in ("id", "received_at", "seq", "session", "bytes", "name"):
        if key not in body:
            raise SystemExit(fail(f"chunk missing {key}: {body}"))
    if body.get("seq") != seq:
        raise SystemExit(fail(f"seq mismatch: {body}"))
    if body.get("session") != session:
        raise SystemExit(fail(f"session mismatch: {body}"))
    if "T" not in str(body["received_at"]):
        raise SystemExit(fail(f"received_at is not a datetime: {body['received_at']}"))
    return body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    devices = load_devices()
    mazi = devices["box-a"]
    arlo = devices["box-b"]
    wav_a = tone_wav(200, 440.0)
    wav_b = tone_wav(200, 660.0)

    with httpx.Client(timeout=8.0) as client:
        first = post_chunk(client, base, mazi.token, session="1", seq=0, blob=wav_a)
        time.sleep(0.05)
        second = post_chunk(client, base, mazi.token, session="1", seq=1, blob=wav_b)
        if first["received_at"] == second["received_at"]:
            return fail(f"datetimes not distinct: {first['received_at']}")
        if first["id"] == second["id"]:
            return fail("chunk ids collided")

        listed = client.get(f"{base}/v1/diary", headers=auth(mazi.token))
        if listed.status_code != 200:
            return fail(f"GET /v1/diary {listed.status_code}")
        body = listed.json()
        if body.get("device_id") != mazi.id or body.get("name") != mazi.name:
            return fail(f"list identity: {body}")
        chunks = body.get("chunks") or []
        if len(chunks) != 2:
            return fail(f"expected 2 chunks, got {chunks}")
        if chunks[0]["id"] != first["id"] or chunks[1]["id"] != second["id"]:
            return fail(f"list order: {chunks}")

        blob = client.get(
            f"{base}/v1/diary/{second['id']}/blob", headers=auth(mazi.token)
        )
        if blob.status_code != 200:
            return fail(f"GET blob {blob.status_code}")
        if blob.content != wav_b:
            return fail("blob bytes mismatch")

        # Arlo has a separate diary; Mazi's chunks must not leak.
        other = client.get(f"{base}/v1/diary", headers=auth(arlo.token))
        if other.status_code != 200:
            return fail(f"Arlo GET {other.status_code}")
        if (other.json().get("chunks") or []):
            return fail(f"Arlo should have an empty diary: {other.json()}")

        # Mute is client-side: stopping POSTs means no third chunk.
        again = client.get(f"{base}/v1/diary", headers=auth(mazi.token))
        if len(again.json().get("chunks") or []) != 2:
            return fail("diary grew without a POST")

    print(f"-- PASS h22_diary {mazi.name} chunks=2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
