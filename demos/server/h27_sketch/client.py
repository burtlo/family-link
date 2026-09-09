#!/usr/bin/env python3
"""Mazi posts a timed heart; Arlo fetches and reads it (no box)."""

from __future__ import annotations

import argparse
import math
import sys

import httpx

from demos.server._shared.registry import load_devices
from demos.server.h27_sketch.codec import PHASE_DOWN, PHASE_MOVE, PHASE_UP, pack_sketch, unpack_sketch


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def heart_points(n: int = 24, step_ms: int = 40) -> list[tuple[int, int, int, int]]:
    pts: list[tuple[int, int, int, int]] = []
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
        if i == 0:
            phase = PHASE_DOWN
        elif i == n - 1:
            phase = PHASE_UP
        else:
            phase = PHASE_MOVE
        pts.append((i * step_ms, phase, px, py))
    return pts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    devices = load_devices()
    mazi = devices["box-a"]
    arlo = devices["box-b"]
    heart = heart_points()
    blob = pack_sketch(heart)

    with httpx.Client(timeout=8.0) as client:
        empty = client.get(f"{base}/v1/sketches", headers=auth(arlo.token))
        if empty.status_code != 200:
            return fail(f"Arlo list {empty.status_code} {empty.text}")
        if empty.json().get("unread"):
            return fail(f"Arlo inbox not empty at start: {empty.json()}")

        posted = client.post(
            f"{base}/v1/sketches",
            headers={**auth(mazi.token), "Content-Type": "application/octet-stream"},
            content=blob,
        )
        if posted.status_code != 200:
            return fail(f"POST {posted.status_code} {posted.text}")
        body = posted.json()
        if body.get("ok") is not True or body.get("to") != arlo.id:
            return fail(f"POST body: {body}")
        sketch_id = body.get("id")
        if not sketch_id:
            return fail(f"POST missing id: {body}")
        if body.get("points") != len(heart):
            return fail(f"points {body.get('points')} vs {len(heart)}")
        if body.get("ms") != heart[-1][0]:
            return fail(f"ms {body.get('ms')} vs {heart[-1][0]}")

        mazi_box = client.get(f"{base}/v1/sketches", headers=auth(mazi.token))
        if mazi_box.status_code != 200:
            return fail(f"Mazi list {mazi_box.status_code}")
        if mazi_box.json().get("unread"):
            return fail("Mazi should not see the note in their own inbox")

        listed = client.get(f"{base}/v1/sketches", headers=auth(arlo.token))
        if listed.status_code != 200:
            return fail(f"Arlo list after post {listed.status_code} {listed.text}")
        unread = listed.json().get("unread") or []
        if len(unread) != 1 or unread[0].get("id") != sketch_id:
            return fail(f"Arlo unread: {unread}")
        if unread[0].get("from_name") != mazi.name:
            return fail(f"from_name: {unread[0]}")

        got = client.get(f"{base}/v1/sketches/{sketch_id}/blob", headers=auth(arlo.token))
        if got.status_code != 200:
            return fail(f"GET blob {got.status_code} {got.text}")
        try:
            replay = unpack_sketch(got.content)
        except Exception as exc:
            return fail(f"unpack: {exc}")
        if replay != heart:
            return fail(f"replay mismatch ({len(replay)} pts)")

        stolen = client.get(f"{base}/v1/sketches/{sketch_id}/blob", headers=auth(mazi.token))
        if stolen.status_code != 404:
            return fail(f"Mazi must not fetch Arlo's blob: {stolen.status_code}")

        marked = client.post(f"{base}/v1/sketches/{sketch_id}/read", headers=auth(arlo.token))
        if marked.status_code != 200 or marked.json().get("unread") != 0:
            return fail(f"read: {marked.status_code} {marked.text}")

        after = client.get(f"{base}/v1/sketches", headers=auth(arlo.token))
        if after.status_code != 200 or after.json().get("unread"):
            return fail(f"still unread after read: {after.json()}")

    print(f"-- PASS h27_sketch {mazi.name}→{arlo.name} heart {len(heart)} pts {heart[-1][0]} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
