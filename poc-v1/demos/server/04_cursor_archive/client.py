#!/usr/bin/env python3
"""Prove server-owned playhead, default-after fetch, archive, and TTL expiry."""

from __future__ import annotations

import argparse
import os
import sys
import time

import httpx

BOX_A = {"Authorization": "Bearer change-me-a"}
BOX_B = {"Authorization": "Bearer change-me-b"}


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def seqs(payload: dict) -> list[int]:
    return [int(m["seq"]) for m in payload.get("messages") or []]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    # Match server demo default (2s). Runner does not set FAMILY_TTL_S.
    ttl_s = float(os.environ.get("FAMILY_TTL_S", "2"))

    with httpx.Client(timeout=5.0) as client:
        for i, text in enumerate(("one", "two", "three"), start=1):
            r = client.post(
                f"{base}/v1/messages",
                headers=BOX_A,
                json={"kind": "text", "text": text},
            )
            if r.status_code != 200:
                return fail(f"POST message {i} expected 200 got {r.status_code}")
            if r.json().get("seq") != i:
                return fail(f"POST message {i} seq mismatch: {r.json()}")

        r = client.put(f"{base}/v1/playhead", headers=BOX_B, json={"seq": 2})
        if r.status_code != 200:
            return fail(f"PUT playhead expected 200 got {r.status_code}")

        # Reboot: do not send a remembered seq. Server playhead is the source.
        r = client.get(f"{base}/v1/me", headers=BOX_B)
        if r.status_code != 200:
            return fail(f"GET /v1/me expected 200 got {r.status_code}")
        me = r.json()
        if me.get("device_id") != "box-b" or me.get("playhead") != 2:
            return fail(f"after reboot expected playhead 2, got {me}")

        r = client.get(f"{base}/v1/messages", headers=BOX_B)
        if r.status_code != 200:
            return fail(f"GET /v1/messages expected 200 got {r.status_code}")
        got = seqs(r.json())
        if got != [3]:
            return fail(f"default fetch expected [3], got {got}")

        r = client.get(f"{base}/v1/messages", headers=BOX_B, params={"before": 2, "limit": 10})
        if r.status_code != 200:
            return fail(f"GET archive expected 200 got {r.status_code}")
        got = sorted(seqs(r.json()))
        if got != [1, 2]:
            return fail(f"archive before=2 expected [1, 2], got {got}")

        time.sleep(ttl_s + 0.5)
        r = client.get(f"{base}/v1/messages", headers=BOX_B, params={"before": 2, "limit": 10})
        if r.status_code != 200:
            return fail(f"GET expired archive expected 200 got {r.status_code}")
        got = seqs(r.json())
        if got != []:
            return fail(f"archive after TTL expected [], got {got}")

    print("-- PASS 04_cursor_archive")
    return 0


if __name__ == "__main__":
    sys.exit(main())
