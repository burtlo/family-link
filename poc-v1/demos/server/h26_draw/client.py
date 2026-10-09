#!/usr/bin/env python3
"""Two twins exchange a heart polyline and a clear (no box)."""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import sys
import time

import websockets

from demos.server._shared.registry import load_devices


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


def heart_points(n: int = 24) -> list[tuple[int, int]]:
    pts: list[tuple[int, int]] = []
    for i in range(n):
        t = 2.0 * math.pi * i / (n - 1)
        x = 16.0 * (math.sin(t) ** 3)
        y = (
            13.0 * math.cos(t)
            - 5.0 * math.cos(2.0 * t)
            - 2.0 * math.cos(3.0 * t)
            - math.cos(4.0 * t)
        )
        px = int(160 + x * 6)
        py = int(118 - y * 5)
        pts.append((px, py))
    return pts


def stroke(phase: str, x: int, y: int) -> str:
    return json.dumps({"type": "stroke", "phase": phase, "x": x, "y": y})


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


async def collect_draw(ws, n: int, timeout: float = 5.0) -> list[dict]:
    out: list[dict] = []
    deadline = time.monotonic() + timeout
    while len(out) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"timed out waiting for {n} draw frames (got {len(out)})")
        msg = await asyncio.wait_for(ws.recv(), timeout=remaining)
        if isinstance(msg, bytes):
            continue
        body = json.loads(msg)
        if body.get("type") in ("stroke", "clear"):
            out.append(body)
    return out


async def run(base_url: str) -> int:
    devices = load_devices()
    mazi = devices["box-a"]
    arlo = devices["box-b"]
    url = ws_url(base_url)
    heart = heart_points()
    line = [(40, 200), (80, 180), (120, 200)]

    async with websockets.connect(url) as ws_a, websockets.connect(url) as ws_b:
        ok_a = await hello(ws_a, mazi.id, mazi.token)
        ok_b = await hello(ws_b, arlo.id, arlo.token)
        if ok_a.get("name") != mazi.name or ok_a.get("peer_name") != arlo.name:
            return fail(f"mazi hello names: {ok_a}")
        if ok_b.get("name") != arlo.name or ok_b.get("peer_name") != mazi.name:
            return fail(f"arlo hello names: {ok_b}")
        if ok_a.get("peer_online") is not False:
            return fail(f"mazi hello expected peer offline: {ok_a}")
        if ok_b.get("peer_online") is not True:
            return fail(f"arlo hello expected Mazi online: {ok_b}")

        joined = await recv_json(ws_a, "peer_status")
        if joined.get("online") is not True or joined.get("peer_name") != arlo.name:
            return fail(f"mazi did not see Arlo join: {joined}")

        recv_heart = asyncio.create_task(collect_draw(ws_b, len(heart) + 1))
        await asyncio.sleep(0)
        x0, y0 = heart[0]
        await ws_a.send(stroke("down", x0, y0))
        for x, y in heart[1:]:
            await ws_a.send(stroke("move", x, y))
        await ws_a.send(stroke("up", heart[-1][0], heart[-1][1]))

        try:
            got_heart = await recv_heart
        except TimeoutError:
            return fail("Arlo did not get Mazi's heart")

        if got_heart[0].get("phase") != "down" or (got_heart[0].get("x"), got_heart[0].get("y")) != heart[0]:
            return fail(f"heart start: {got_heart[0]}")
        if got_heart[-1].get("phase") != "up":
            return fail(f"heart end: {got_heart[-1]}")
        moves = [(b.get("x"), b.get("y")) for b in got_heart[1:-1]]
        if moves != heart[1:]:
            return fail(f"heart moves mismatch ({len(moves)} vs {len(heart) - 1})")

        recv_line = asyncio.create_task(collect_draw(ws_a, len(line)))
        await asyncio.sleep(0)
        await ws_b.send(stroke("down", line[0][0], line[0][1]))
        await ws_b.send(stroke("move", line[1][0], line[1][1]))
        await ws_b.send(stroke("up", line[2][0], line[2][1]))

        try:
            got_line = await recv_line
        except TimeoutError:
            return fail("Mazi did not get Arlo's line")
        if [(b.get("x"), b.get("y")) for b in got_line] != line:
            return fail(f"line mismatch: {got_line}")

        recv_clear = asyncio.create_task(collect_draw(ws_b, 1))
        await asyncio.sleep(0)
        await ws_a.send(json.dumps({"type": "clear"}))
        try:
            got_clear = await recv_clear
        except TimeoutError:
            return fail("Arlo did not get clear")
        if got_clear[0].get("type") != "clear":
            return fail(f"clear: {got_clear[0]}")

    print(f"-- PASS h26_draw {mazi.name}↔{arlo.name} heart {len(heart)} pts")
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
