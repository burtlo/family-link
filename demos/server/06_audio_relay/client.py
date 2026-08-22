#!/usr/bin/env python3
"""Two twins: A holds the floor, server relays 20 PCM frames, B captures bytes."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
import time
from pathlib import Path

import websockets

from demos.server._shared.registry import load_devices

N_FRAMES = 20
FRAME_SIZE = 640
TOTAL_BYTES = N_FRAMES * FRAME_SIZE
ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
CAPTURE = ROOT / "data" / "06_audio_relay" / "relay-capture.bin"


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


def build_payload() -> bytes:
    return bytes(i % 256 for i in range(TOTAL_BYTES))


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


async def hello(ws, device_id: str, token: str) -> dict:
    await ws.send(json.dumps({"type": "hello", "device_id": device_id, "token": token}))
    ok = await recv_json(ws, "hello_ok")
    if ok.get("device_id") != device_id:
        raise RuntimeError(f"hello_ok device_id={ok.get('device_id')!r}")
    return ok


async def collect_binary(ws, n_bytes: int, recv_times: list[float]) -> bytearray:
    buf = bytearray()
    while len(buf) < n_bytes:
        msg = await asyncio.wait_for(ws.recv(), timeout=5.0)
        if isinstance(msg, bytes):
            recv_times.append(time.perf_counter())
            buf.extend(msg)
    return buf


async def run(base_url: str) -> int:
    devices = load_devices()
    a = devices["box-a"]
    b = devices["box-b"]
    url = ws_url(base_url)
    payload = build_payload()
    frames = [payload[i : i + FRAME_SIZE] for i in range(0, TOTAL_BYTES, FRAME_SIZE)]

    async with websockets.connect(url) as ws_a, websockets.connect(url) as ws_b:
        await hello(ws_a, a.id, a.token)
        await hello(ws_b, b.id, b.token)

        await ws_a.send(json.dumps({"type": "invite"}))
        ring = await recv_json(ws_b, "ring")
        if ring.get("from") != a.id:
            return fail(f"ring from={ring.get('from')!r}")

        await ws_b.send(json.dumps({"type": "accept"}))
        start_a, start_b = await asyncio.gather(
            recv_json(ws_a, "session_start"),
            recv_json(ws_b, "session_start"),
        )
        if start_a.get("type") != "session_start" or start_b.get("type") != "session_start":
            return fail("missing session_start")

        floor_a, floor_b = await asyncio.gather(
            recv_json(ws_a, "floor"),
            recv_json(ws_b, "floor"),
        )
        if floor_a.get("holder") is not None or floor_b.get("holder") is not None:
            return fail(f"expected empty floor, got {floor_a} {floor_b}")

        await ws_a.send(json.dumps({"type": "floor_request"}))
        held_a, held_b = await asyncio.gather(
            recv_json(ws_a, "floor"),
            recv_json(ws_b, "floor"),
        )
        if held_a.get("holder") != a.id or held_b.get("holder") != a.id:
            return fail(f"expected floor {a.id}, got {held_a} {held_b}")

        recv_times: list[float] = []
        collector = asyncio.create_task(collect_binary(ws_b, TOTAL_BYTES, recv_times))
        await asyncio.sleep(0)

        send_times: list[float] = []
        for frame in frames:
            send_times.append(time.perf_counter())
            await ws_a.send(frame)

        try:
            captured = await collector
        except TimeoutError:
            return fail("B did not receive 20 binary frames")

        if bytes(captured) != payload:
            return fail(
                f"relay bytes mismatch: got {len(captured)} expected {TOTAL_BYTES}"
            )
        if len(recv_times) != N_FRAMES:
            return fail(f"expected {N_FRAMES} binary frames, got {len(recv_times)}")

        hop_ms = (recv_times[0] - send_times[0]) * 1000.0
        mean_ms = (
            sum((r - s) * 1000.0 for s, r in zip(send_times, recv_times, strict=True))
            / N_FRAMES
        )
        print(f"one-way hop {hop_ms:.1f} ms (first frame); mean {mean_ms:.1f} ms")

        CAPTURE.parent.mkdir(parents=True, exist_ok=True)
        CAPTURE.write_bytes(bytes(captured))
        print(f"wrote {CAPTURE} ({len(captured)} bytes)")

        await ws_a.send(json.dumps({"type": "hangup"}))
        await asyncio.gather(recv_json(ws_a, "hangup"), recv_json(ws_b, "hangup"))

    print("-- PASS 06_audio_relay")
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
