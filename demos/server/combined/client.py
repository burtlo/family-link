#!/usr/bin/env python3
"""Smoke the combined host: auth, text, image preview, playhead."""

from __future__ import annotations

import argparse
import json
import sys
from io import BytesIO

import httpx
from websockets.exceptions import ConnectionClosed
from websockets.sync.client import connect

AUTH_A = {"Authorization": "Bearer change-me-a"}
AUTH_B = {"Authorization": "Bearer change-me-b"}
PREVIEW_LEN = 320 * 240 * 2


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def tiny_jpeg() -> bytes:
    from PIL import Image

    buf = BytesIO()
    Image.new("RGB", (32, 24), (200, 40, 40)).save(buf, format="JPEG")
    return buf.getvalue()


def as_list(payload) -> list:
    if isinstance(payload, list):
        return payload
    if isinstance(payload, dict) and isinstance(payload.get("messages"), list):
        return payload["messages"]
    return []


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    with httpx.Client(timeout=10.0) as http:
        r = http.get(f"{base}/app/")
        if r.status_code != 200:
            return fail(f"GET /app/ expected 200 got {r.status_code}")
        page = r.text
        if "parent page coming" in page and "Desk link" not in page:
            return fail("GET /app/ is still the combined placeholder")
        if "<html" not in page.lower():
            return fail("GET /app/ did not look like HTML")
        print("PASS parent /app")

        r = http.get(f"{base}/v1/me", headers=AUTH_A)
        if r.status_code != 200:
            return fail(f"good token expected 200 got {r.status_code}")
        me = r.json()
        if me.get("device_id") != "box-a" or me.get("peer_id") != "box-b":
            return fail(f"box-a identity mismatch: {me}")
        if me.get("role") is None or "playhead" not in me or "unread" not in me:
            return fail(f"box-a /v1/me missing fields: {me}")
        print("PASS me good token")

        r = http.get(f"{base}/v1/me")
        if r.status_code != 401:
            return fail(f"missing auth expected 401 got {r.status_code}")
        r = http.get(f"{base}/v1/me", headers={"Authorization": "Bearer wrong"})
        if r.status_code != 401:
            return fail(f"bad token expected 401 got {r.status_code}")
        print("PASS me bad token")

        ws_url = (
            base.replace("http://", "ws://", 1).replace("https://", "wss://", 1)
            + "/v1/ws"
        )
        with connect(ws_url, open_timeout=5) as ws:
            ws.send(
                json.dumps({"type": "hello", "device_id": "box-a", "token": "wrong"})
            )
            try:
                ws.recv(timeout=5)
                return fail("bad WS token should close, got a frame")
            except ConnectionClosed as exc:
                code = exc.rcvd.code if exc.rcvd is not None else None
                if code != 4401:
                    return fail(f"bad WS token expected close 4401 got {code}")
        with connect(ws_url, open_timeout=5) as ws:
            ws.send(
                json.dumps(
                    {"type": "hello", "device_id": "box-a", "token": "change-me-a"}
                )
            )
            hello = json.loads(ws.recv(timeout=5))
            if hello.get("type") != "hello_ok" or hello.get("device_id") != "box-a":
                return fail(f"expected hello_ok got {hello}")
            if "playhead" not in hello:
                return fail(f"hello_ok missing playhead: {hello}")
        print("PASS ws hello")

        posted = http.post(
            f"{base}/v1/messages",
            data={"kind": "text", "text": "hello from b"},
            headers=AUTH_B,
        )
        if posted.status_code != 200:
            return fail(f"text POST expected 200 got {posted.status_code} {posted.text}")
        text_body = posted.json()
        if (
            text_body.get("kind") != "text"
            or text_body.get("from") != "box-b"
            or text_body.get("to") != "box-a"
        ):
            return fail(f"text POST body mismatch: {text_body}")
        text_seq = int(text_body["seq"])

        listed = http.get(f"{base}/v1/messages", headers=AUTH_A)
        if listed.status_code != 200:
            return fail(f"GET messages expected 200 got {listed.status_code}")
        messages = as_list(listed.json())
        found = next((m for m in messages if m.get("seq") == text_seq), None)
        if found is None or found.get("text") != "hello from b" or found.get("from") != "box-b":
            return fail(f"box-a inbox missing text seq {text_seq}: {messages}")
        print("PASS text box-b → box-a")

        jpeg = tiny_jpeg()
        image = http.post(
            f"{base}/v1/messages",
            data={"kind": "image"},
            files={"blob": ("tiny.jpg", jpeg, "image/jpeg")},
            headers=AUTH_B,
        )
        if image.status_code != 200:
            return fail(f"image POST expected 200 got {image.status_code} {image.text}")
        image_seq = int(image.json()["seq"])
        preview = http.get(f"{base}/v1/messages/{image_seq}/preview", headers=AUTH_A)
        if preview.status_code != 200:
            return fail(f"GET preview expected 200 got {preview.status_code} {preview.text}")
        if len(preview.content) != PREVIEW_LEN:
            return fail(
                f"preview length {len(preview.content)} expected {PREVIEW_LEN}"
            )
        blob = http.get(f"{base}/v1/messages/{image_seq}/blob", headers=AUTH_A)
        if blob.status_code != 200 or blob.content != jpeg:
            return fail(
                f"GET blob mismatch status={blob.status_code} len={len(blob.content)}"
            )
        print("PASS image preview")

        put = http.put(f"{base}/v1/playhead", headers=AUTH_A, json={"seq": image_seq})
        if put.status_code != 200:
            return fail(f"PUT playhead expected 200 got {put.status_code}")
        if put.json().get("playhead") != image_seq:
            return fail(f"PUT playhead body mismatch: {put.json()}")
        me = http.get(f"{base}/v1/me", headers=AUTH_A).json()
        if me.get("playhead") != image_seq:
            return fail(f"GET /v1/me playhead expected {image_seq} got {me}")
        after = as_list(http.get(f"{base}/v1/messages", headers=AUTH_A).json())
        leftover = [m for m in after if int(m["seq"]) <= image_seq]
        if leftover:
            return fail(f"default GET after playhead still has {leftover}")
        print("PASS playhead")

    print("-- PASS combined")
    return 0


if __name__ == "__main__":
    sys.exit(main())
