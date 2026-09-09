#!/usr/bin/env python3
"""Two twins send PCM at once; each receives the other's bytes (no floor)."""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time

import websockets

from demos.server._shared.registry import load_devices

N_FRAMES = 20
FRAME_SIZE = 640
TOTAL_BYTES = N_FRAMES * FRAME_SIZE


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def ws_url(base: str) -> str:
    base = base.rstrip("/")
    if base.startswith("https://"):
        return "wss://" + base.removeprefix("https://") + "/v1/ws"
    if base.startswith("http://"):
        return "ws://" + base.removeprefix("http://") + "/v1/ws"
    return base + "/v1/ws"


def frames_for(seed: int) -> list[bytes]:
    payload = bytes((seed + i) % 256 for i in range(TOTAL_BYTES))
    return [payload[i : i + FRAME_SIZE] for i in range(0, TOTAL_BYTES, FRAME_SIZE)]


async def recv_json(ws, wanted: str, timeout: float = 5.0) -> dict:
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"timed out waiting for {wanted}")
        msg = await asyncio.wait_for(ws.recv(), timeout=remaining)
        if isinstance(msg, bytes):
            continue
        body = json.loads(msg)
        if body.get("type") == wanted:
            return body


async def hello(ws, device_id: str, token: str, available: bool = False) -> dict:
    await ws.send(
        json.dumps(
            {
                "type": "hello",
                "device_id": device_id,
                "token": token,
                "available": available,
            }
        )
    )
    ok = await recv_json(ws, "hello_ok")
    if ok.get("device_id") != device_id:
        raise RuntimeError(f"hello_ok device_id={ok.get('device_id')!r}")
    return ok


async def collect_binary(ws, expected: bytes) -> bytearray:
    buf = bytearray()
    while len(buf) < len(expected):
        msg = await asyncio.wait_for(ws.recv(), timeout=5.0)
        if isinstance(msg, bytes):
            buf.extend(msg)
    return buf


async def run(base_url: str) -> int:
    devices = load_devices()
    mazi = devices["box-a"]
    arlo = devices["box-b"]
    url = ws_url(base_url)
    mazi_frames = frames_for(1)
    arlo_frames = frames_for(17)
    mazi_payload = b"".join(mazi_frames)
    arlo_payload = b"".join(arlo_frames)

    async with websockets.connect(url) as ws_a, websockets.connect(url) as ws_b:
        ok_a = await hello(ws_a, mazi.id, mazi.token, available=False)
        ok_b = await hello(ws_b, arlo.id, arlo.token, available=True)
        if ok_a.get("name") != mazi.name or ok_a.get("peer_name") != arlo.name:
            return fail(f"mazi hello names: {ok_a}")
        if ok_b.get("name") != arlo.name or ok_b.get("peer_name") != mazi.name:
            return fail(f"arlo hello names: {ok_b}")
        if ok_a.get("peer_online") is not False:
            return fail(f"mazi hello expected peer offline: {ok_a}")
        if ok_b.get("peer_online") is not True or ok_b.get("peer_available") is not False:
            return fail(f"arlo hello expected Mazi muted+online: {ok_b}")

        joined = await recv_json(ws_a, "peer_status")
        if joined.get("online") is not True or joined.get("available") is not True:
            return fail(f"mazi did not see Arlo open: {joined}")
        if joined.get("peer_name") != arlo.name:
            return fail(f"mazi peer_status name: {joined}")

        await ws_a.send(json.dumps({"type": "status", "available": True}))
        opened = await recv_json(ws_b, "peer_status")
        if opened.get("online") is not True or opened.get("available") is not True:
            return fail(f"arlo did not see Mazi open: {opened}")

        await ws_a.send(json.dumps({"type": "status", "available": False}))
        closed = await recv_json(ws_b, "peer_status")
        if closed.get("available") is not False:
            return fail(f"arlo did not see Mazi mute: {closed}")

        recv_b = asyncio.create_task(collect_binary(ws_b, mazi_payload))
        recv_a = asyncio.create_task(collect_binary(ws_a, arlo_payload))
        await asyncio.sleep(0)

        for fa, fb in zip(mazi_frames, arlo_frames, strict=True):
            await ws_a.send(fa)
            await ws_b.send(fb)

        try:
            got_b = await recv_b
            got_a = await recv_a
        except TimeoutError:
            return fail("did not receive both directions")

        if bytes(got_b) != mazi_payload:
            return fail(f"Arlo did not get Mazi's PCM ({len(got_b)} bytes)")
        if bytes(got_a) != arlo_payload:
            return fail(f"Mazi did not get Arlo's PCM ({len(got_a)} bytes)")

    print(f"-- PASS h21_talk {mazi.name}↔{arlo.name} duplex {N_FRAMES} frames")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    try:
        return asyncio.run(run(args.base_url))
    except Exception as exc:
        return fail(str(exc))


if __name__ == "__main__":
    sys.exit(main())
