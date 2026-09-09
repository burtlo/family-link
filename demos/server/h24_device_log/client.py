#!/usr/bin/env python3
"""Heartbeat with log lines lands on disk and acks seq."""

from __future__ import annotations

import argparse
import sys

import httpx

from demos.server._shared.registry import load_devices


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def heartbeat(
    client: httpx.Client, base: str, token: str, *, available: bool, boot_id: int, logs: list[dict]
) -> dict:
    r = client.post(
        f"{base}/v1/heartbeat",
        headers=auth(token),
        json={"available": available, "boot_id": boot_id, "logs": logs},
    )
    if r.status_code != 200:
        raise SystemExit(fail(f"heartbeat {r.status_code} {r.text}"))
    body = r.json()
    if body.get("ok") is not True:
        raise SystemExit(fail(f"heartbeat not ok: {body}"))
    return body


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    devices = load_devices()
    mazi = devices["box-a"]
    arlo = devices["box-b"]

    sample_logs = [
        {"seq": 1, "ms": 120, "lvl": "I", "msg": "boot poweron"},
        {"seq": 2, "ms": 800, "lvl": "I", "msg": "wifi ok rssi=-42"},
    ]

    with httpx.Client(timeout=5.0) as client:
        first = heartbeat(client, base, mazi.token, available=False, boot_id=0xA001, logs=sample_logs)
        if first.get("logs_ack") != 2:
            raise SystemExit(fail(f"expected logs_ack=2 got {first.get('logs_ack')}"))
        if "peer" not in first or "self" not in first:
            raise SystemExit(fail(f"missing self/peer: {first}"))

        second = heartbeat(
            client,
            base,
            arlo.token,
            available=True,
            boot_id=0xB001,
            logs=[{"seq": 1, "ms": 50, "lvl": "I", "msg": "boot poweron"}],
        )
        if second.get("logs_ack") != 1:
            raise SystemExit(fail(f"arlo logs_ack expected 1 got {second.get('logs_ack')}"))
        if second["peer"].get("available") is not False:
            raise SystemExit(fail(f"arlo should see mazi away: {second['peer']}"))

        listed = client.get(f"{base}/v1/logs", headers=auth(mazi.token), params={"tail": 5})
        if listed.status_code != 200:
            raise SystemExit(fail(f"GET /v1/logs {listed.status_code}"))
        body = listed.json()
        if len(body.get("lines") or []) < 2:
            raise SystemExit(fail(f"expected >=2 log lines on disk got {body}"))

    print(f"-- PASS h24_device_log {mazi.name}+{arlo.name} lines={len(body['lines'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
