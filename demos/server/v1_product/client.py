#!/usr/bin/env python3
"""Smoke the v1 product host: hangout, login, inbox, send, admin."""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import wave
from io import BytesIO

import httpx
import websockets

AUTH_LYNN = {
    "Authorization": "Bearer change-me-lynn",
}
AUTH_MAZI = {
    "Authorization": "Bearer change-me-mazi",
}


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def tiny_wav(seconds: float = 0.25) -> bytes:
    buf = BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(16000)
        n = int(16000 * seconds)
        wf.writeframes(b"\x00\x01" * n)
    return buf.getvalue()


async def ws_smoke(base: str, http: httpx.Client) -> None:
    ws_url = base.replace("http://", "ws://").replace("https://", "wss://") + "/v1/ws"
    wav = tiny_wav()
    async with websockets.connect(ws_url) as ws:
        await ws.send(json.dumps({"type": "hello", "token": "change-me-lynn"}))
        hello = json.loads(await asyncio.wait_for(ws.recv(), timeout=3.0))
        if hello.get("type") != "hello_ok":
            raise RuntimeError(f"ws hello_ok expected, got {hello}")
        sent = await asyncio.to_thread(
            lambda: http.post(
                f"{base}/v1/messages",
                headers={**AUTH_MAZI, "X-User-Id": "mazi"},
                data={"kind": "audio", "to_user_id": "lynn", "broadcast": "false"},
                files={"blob": ("ws.wav", wav, "audio/wav")},
            )
        )
        if sent.status_code != 200:
            raise RuntimeError(f"ws trigger send failed: {sent.status_code}")
        push = json.loads(await asyncio.wait_for(ws.recv(), timeout=3.0))
        if push.get("type") != "inbox":
            raise RuntimeError(f"ws inbox push expected, got {push}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    with httpx.Client(timeout=10.0) as http:
        r = http.get(f"{base}/v1/hangout", headers=AUTH_LYNN)
        if r.status_code != 200:
            return fail(f"GET /v1/hangout expected 200 got {r.status_code}")
        hangout = r.json()
        names = {u["id"] for u in hangout.get("users", [])}
        if names != {"lynn", "mazi", "arlo", "audrey"}:
            return fail(f"unexpected users: {names}")
        print("PASS hangout roster")

        r = http.get(f"{base}/v1/hangout")
        if r.status_code != 401:
            return fail("hangout without token should 401")
        print("PASS hangout auth")

        r = http.post(
            f"{base}/v1/session/login",
            headers=AUTH_MAZI,
            json={"user_id": "mazi", "pin": "wrong"},
        )
        if r.status_code != 401:
            return fail(f"bad pin expected 401 got {r.status_code}")
        print("PASS bad pin")

        r = http.post(
            f"{base}/v1/session/login",
            headers=AUTH_MAZI,
            json={"user_id": "mazi", "pin": "2345"},
        )
        if r.status_code != 200 or not r.json().get("ok"):
            return fail(f"login failed: {r.status_code} {r.text}")
        inbox = r.json()
        msgs = inbox.get("messages") or []
        if not msgs or msgs[0].get("seq") != 1:
            return fail(f"expected First Message seq=1: {msgs}")
        if not msgs[0].get("system"):
            return fail("First Message should be system")
        print("PASS login + First Message")

        headers = {**AUTH_MAZI, "X-User-Id": "mazi"}
        wav = tiny_wav()
        sent = http.post(
            f"{base}/v1/messages",
            headers=headers,
            data={"kind": "audio", "to_user_id": "lynn", "broadcast": "false"},
            files={"blob": ("clip.wav", wav, "audio/wav")},
        )
        if sent.status_code != 200:
            return fail(f"send expected 200 got {sent.status_code} {sent.text}")
        print("PASS mazi → lynn audio")

        lynn_headers = {**AUTH_LYNN, "X-User-Id": "lynn"}
        lin = http.post(
            f"{base}/v1/session/login",
            headers=AUTH_LYNN,
            json={"user_id": "lynn", "pin": "1234"},
        )
        lynn_inbox = lin.json().get("messages") or []
        lynn_seqs = [m["seq"] for m in lynn_inbox]
        if max(lynn_seqs) < 2:
            return fail(f"lynn missing inbound message: {lynn_seqs}")

        bc = http.post(
            f"{base}/v1/messages",
            headers=lynn_headers,
            data={"kind": "audio", "broadcast": "true"},
            files={"blob": ("all.wav", wav, "audio/wav")},
        )
        if bc.status_code != 200:
            return fail(f"broadcast expected 200 got {bc.status_code}")
        created = bc.json().get("messages") or []
        if len(created) != 3:
            return fail(f"broadcast should fan out to 3: {created}")
        print("PASS lynn broadcast")

        admin = http.post(
            f"{base}/v1/admin/login",
            json={"username": "lynn", "password": "change-me-lynn"},
        )
        if admin.status_code != 200:
            return fail(f"admin login {admin.status_code}")
        token = admin.json().get("token")
        if not token:
            return fail("admin token missing")
        reset = http.post(
            f"{base}/v1/admin/pin-reset",
            headers={"Authorization": f"Bearer {token}"},
            json={"user_id": "arlo", "pin": "9999"},
        )
        if reset.status_code != 200 or reset.json().get("pin") != "9999":
            return fail(f"pin reset failed: {reset.text}")
        print("PASS admin pin reset")

        arlo = http.post(
            f"{base}/v1/session/login",
            headers={"Authorization": "Bearer change-me-arlo"},
            json={"user_id": "arlo", "pin": "9999"},
        )
        if arlo.status_code != 200 or not arlo.json().get("pin_reset"):
            return fail(f"arlo pin_reset flag missing: {arlo.status_code} {arlo.text}")
        print("PASS pin_reset on first login")

        prof = http.put(
            f"{base}/v1/profile",
            headers=headers,
            json={"avatar_slot": 3, "accent_hex": "#FF6B6B"},
        )
        if prof.status_code != 200:
            return fail(f"profile put {prof.status_code} {prof.text}")
        got = prof.json().get("profile") or {}
        if got.get("avatar_slot") != 3 or got.get("accent_hex") != "#FF6B6B":
            return fail(f"profile mismatch: {got}")
        hang = http.get(f"{base}/v1/hangout", headers=AUTH_MAZI).json()
        mazi = next(u for u in hang["users"] if u["id"] == "mazi")
        if mazi.get("profile", {}).get("avatar_slot") != 3:
            return fail(f"hangout profile not synced: {mazi}")
        print("PASS profile sync")

        asyncio.run(ws_smoke(base, http))
        print("PASS websocket inbox")

    print("-- PASS v1_product")
    return 0


if __name__ == "__main__":
    sys.exit(main())
