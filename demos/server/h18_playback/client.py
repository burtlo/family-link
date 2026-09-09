#!/usr/bin/env python3
"""Smoke the h18 playback fixture: message JSON + WAV bytes."""

from __future__ import annotations

import argparse
import sys
from urllib.parse import urlparse

import httpx


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def check_message(http: httpx.Client, url: str, *, require_mid_start: bool) -> dict:
    r = http.get(url)
    if r.status_code != 200:
        raise SystemExit(fail(f"GET {url} expected 200 got {r.status_code}"))
    msg = r.json()
    for key in ("id", "sender", "sent_at", "url", "duration_ms", "position_ms", "read",
                "index", "count"):
        if key not in msg:
            raise SystemExit(fail(f"message missing {key}: {msg}"))
    if not isinstance(msg["duration_ms"], int) or msg["duration_ms"] < 200:
        raise SystemExit(fail(f"duration_ms implausible: {msg['duration_ms']}"))
    if not isinstance(msg["position_ms"], int) or msg["position_ms"] < 0:
        raise SystemExit(fail(f"position_ms implausible: {msg['position_ms']}"))
    if msg["position_ms"] >= msg["duration_ms"]:
        raise SystemExit(fail(f"position_ms past end: {msg}"))
    if require_mid_start and msg["position_ms"] < 1:
        raise SystemExit(fail(f"tone should start mid-clip: {msg}"))
    parsed = urlparse(str(msg["url"]))
    if parsed.scheme not in {"http", "https"} or not parsed.path:
        raise SystemExit(fail(f"url is not http(s): {msg['url']}"))

    wav = http.get(str(msg["url"]))
    if wav.status_code != 200:
        raise SystemExit(fail(f"GET audio expected 200 got {wav.status_code}"))
    if wav.content[:4] != b"RIFF" or wav.content[8:12] != b"WAVE":
        raise SystemExit(fail("audio is not a WAV"))
    min_bytes = 44 + (msg["duration_ms"] * 32) // 2
    if len(wav.content) < min_bytes:
        raise SystemExit(
            fail(f"wav too short: {len(wav.content)} bytes for {msg['duration_ms']} ms")
        )
    return msg


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    with httpx.Client(timeout=8.0) as http:
        first = check_message(http, f"{base}/demo/h18/message", require_mid_start=True)
        count = int(first["count"])
        if count < 1:
            return fail(f"count implausible: {count}")
        wrap = check_message(
            http, f"{base}/demo/h18/message?i={count}", require_mid_start=True
        )
        if wrap["index"] != 0 or wrap["id"] != first["id"]:
            return fail(f"catalog did not wrap: {wrap}")
        if count >= 2:
            voice = check_message(
                http, f"{base}/demo/h18/message?i=1", require_mid_start=False
            )
            if voice["index"] != 1:
                return fail(f"second message index: {voice}")
            if voice["position_ms"] != 0:
                return fail(f"voice clip should start at 0: {voice}")

        page = http.get(f"{base}/box/")
        if page.status_code != 200:
            return fail(f"GET /box/ expected 200 got {page.status_code}")
        if 'id="lcd"' not in page.text:
            return fail("box page missing 320x240 lcd")
        js = http.get(f"{base}/box/box.js")
        if js.status_code != 200:
            return fail(f"GET /box/box.js expected 200 got {js.status_code}")

    print(f"-- PASS h18_playback count={count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
