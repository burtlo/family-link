#!/usr/bin/env python3
"""Peer twin for firmware h11/h12: connect as box-b, accept, send reverse PCM.

Start the relay server first, then this twin, then boot the box:

    python -m demos.server.06_audio_relay.server --host 0.0.0.0 --port 8080
    python demos/server/06_audio_relay/twin_peer.py --base-url http://192.168.x.x:8080
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
from pathlib import Path

import websockets

from demos.server._shared.registry import load_devices

FRAME_SIZE = 640
N_REVERSE = 20
ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
CAPTURE = ROOT / "data" / "06_audio_relay" / "box-capture.bin"


def ws_url(base: str) -> str:
    base = base.rstrip("/")
    if base.startswith("https://"):
        return "wss://" + base.removeprefix("https://") + "/v1/ws"
    if base.startswith("http://"):
        return "ws://" + base.removeprefix("http://") + "/v1/ws"
    return base + "/v1/ws"


def tone_frame(n: int) -> bytes:
    out = bytearray(FRAME_SIZE)
    for i in range(0, FRAME_SIZE, 2):
        amp = 8000 if ((i // 2 + n * 16) // 18) % 2 == 0 else -8000
        out[i] = amp & 0xFF
        out[i + 1] = (amp >> 8) & 0xFF
    return bytes(out)


async def recv_until(ws, predicate, timeout: float = 60.0):
    captured = bytearray()
    deadline = asyncio.get_event_loop().time() + timeout
    while True:
        remaining = deadline - asyncio.get_event_loop().time()
        if remaining <= 0:
            raise TimeoutError("timeout")
        msg = await asyncio.wait_for(ws.recv(), timeout=remaining)
        if isinstance(msg, bytes):
            captured.extend(msg)
            continue
        body = json.loads(msg)
        print(f"ws {body}", flush=True)
        if predicate(body):
            return body, captured


async def run(base_url: str) -> int:
    devices = load_devices()
    peer = devices["box-b"]
    url = ws_url(base_url)
    print(f"twin_peer connecting as {peer.id} {url}", flush=True)

    async with websockets.connect(url) as ws:
        await ws.send(
            json.dumps({"type": "hello", "device_id": peer.id, "token": peer.token})
        )
        hello = json.loads(await ws.recv())
        if hello.get("type") != "hello_ok":
            print(f"FAIL hello {hello}")
            return 1

        print("waiting for ring from the box…", flush=True)
        await recv_until(ws, lambda b: b.get("type") == "ring")
        await ws.send(json.dumps({"type": "accept"}))
        await recv_until(ws, lambda b: b.get("type") == "session_start")

        await ws.send(json.dumps({"type": "floor_request"}))
        await recv_until(
            ws, lambda b: b.get("type") == "floor" and b.get("holder") == peer.id
        )
        for i in range(N_REVERSE):
            await ws.send(tone_frame(i))
            await asyncio.sleep(0.02)
        await ws.send(json.dumps({"type": "floor_release"}))
        print(f"sent {N_REVERSE} reverse frames; hold mute on the box", flush=True)

        captured = bytearray()
        deadline = asyncio.get_event_loop().time() + 90.0
        while len(captured) < FRAME_SIZE * 10:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                print("FAIL box did not send 10 PCM frames (hold mute)")
                return 1
            msg = await asyncio.wait_for(ws.recv(), timeout=remaining)
            if isinstance(msg, bytes):
                captured.extend(msg)
                print(f"from box {len(captured)} bytes", flush=True)

        CAPTURE.parent.mkdir(parents=True, exist_ok=True)
        CAPTURE.write_bytes(bytes(captured))
        print(f"wrote {CAPTURE} ({len(captured)} bytes)", flush=True)
        print("-- PASS twin_peer")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    try:
        return asyncio.run(run(args.base_url))
    except Exception as exc:
        print(f"FAIL {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
