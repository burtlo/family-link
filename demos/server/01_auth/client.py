#!/usr/bin/env python3
"""Exercise GET /v1/me bearer auth against a running 01_auth server."""

from __future__ import annotations

import argparse
import sys

import httpx


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    with httpx.Client(timeout=5.0) as client:
        r = client.get(f"{base}/v1/me", headers={"Authorization": "Bearer change-me-a"})
        if r.status_code != 200:
            return fail(f"box-a expected 200 got {r.status_code}")
        body = r.json()
        if body.get("device_id") != "box-a" or body.get("peer_id") != "box-b":
            return fail(f"box-a identity mismatch: {body}")
        print("PASS")

        r = client.get(f"{base}/v1/me")
        if r.status_code != 401:
            return fail(f"missing auth expected 401 got {r.status_code}")
        print("PASS")

        r = client.get(f"{base}/v1/me", headers={"Authorization": "Bearer wrong"})
        if r.status_code != 401:
            return fail(f"wrong token expected 401 got {r.status_code}")
        print("PASS")

        r = client.get(f"{base}/v1/me", headers={"Authorization": "Bearer change-me-b"})
        if r.status_code != 200:
            return fail(f"box-b expected 200 got {r.status_code}")
        body = r.json()
        if body.get("device_id") != "box-b":
            return fail(f"box-b token returned device_id={body.get('device_id')!r}")
        print("PASS")

    print("-- PASS 01_auth")
    return 0


if __name__ == "__main__":
    sys.exit(main())
