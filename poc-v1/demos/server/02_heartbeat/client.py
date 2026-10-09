#!/usr/bin/env python3
"""Prove presence: both online, stale after 10s, recover with no client state."""

from __future__ import annotations

import argparse
import sys
import time

import httpx

TOKEN_A = "change-me-a"
TOKEN_B = "change-me-b"


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def heartbeat(client: httpx.Client, base: str, token: str, uptime_s: float) -> httpx.Response:
    return client.post(
        f"{base}/v1/heartbeat",
        headers=auth(token),
        json={"uptime_s": uptime_s, "rssi": -50},
    )


def me(client: httpx.Client, base: str, token: str) -> httpx.Response:
    return client.get(f"{base}/v1/me", headers=auth(token))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    with httpx.Client(timeout=5.0) as client:
        # 1. Both twins heartbeat a few times, 2s apart. Then both see the peer.
        for i in range(3):
            if i:
                time.sleep(2)
            ra = heartbeat(client, base, TOKEN_A, uptime_s=float(i * 2))
            rb = heartbeat(client, base, TOKEN_B, uptime_s=float(i * 2))
            if ra.status_code != 200 or ra.json().get("ok") is not True:
                return fail(f"box-a heartbeat {i} failed: {ra.status_code} {ra.text}")
            if rb.status_code != 200 or rb.json().get("ok") is not True:
                return fail(f"box-b heartbeat {i} failed: {rb.status_code} {rb.text}")

        for token, name in ((TOKEN_A, "box-a"), (TOKEN_B, "box-b")):
            r = me(client, base, token)
            if r.status_code != 200:
                return fail(f"{name} GET /v1/me expected 200 got {r.status_code}")
            body = r.json()
            if body.get("peer_online") is not True:
                return fail(f"{name} expected peer_online true while both live: {body}")
            if body.get("last_seen") is None:
                return fail(f"{name} last_seen should be set after heartbeat: {body}")

        # 2. Stop box-b. After the 10s stale window, box-a sees the peer offline.
        time.sleep(11)
        r = me(client, base, TOKEN_A)
        if r.status_code != 200:
            return fail(f"stale GET /v1/me expected 200 got {r.status_code}")
        body = r.json()
        if body.get("peer_online") is not False:
            return fail(f"box-a expected peer_online false after 11s silence: {body}")

        # 3. box-b heartbeats again (power-loss recovery; no client-stored state).
        rb = heartbeat(client, base, TOKEN_B, uptime_s=0.0)
        if rb.status_code != 200 or rb.json().get("ok") is not True:
            return fail(f"recovery heartbeat failed: {rb.status_code} {rb.text}")
        r = me(client, base, TOKEN_A)
        if r.status_code != 200:
            return fail(f"recovery GET /v1/me expected 200 got {r.status_code}")
        body = r.json()
        if body.get("peer_online") is not True:
            return fail(f"box-a expected peer_online true after box-b recovered: {body}")

    print("-- PASS 02_heartbeat")
    return 0


if __name__ == "__main__":
    sys.exit(main())
