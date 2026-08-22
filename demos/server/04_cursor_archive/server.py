#!/usr/bin/env python3
"""Playhead + archive: devices forget RAM; this process stores the cursor.

Demo-only TTL default is 2s so the client expiry check works without the
runner passing --ttl (production would be 1 hour). Override with --ttl or
FAMILY_TTL_S.
"""

from __future__ import annotations

import argparse
import os
import time
from collections import defaultdict

import uvicorn
from fastapi import FastAPI, Header
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer

DEVICES = load_devices()
# Demo default 2s (not 3600) so scripts/run_server_demo.py can prove TTL
# without extra flags. Production retention is 1 hour.
TTL_S = float(os.environ.get("FAMILY_TTL_S", "2"))

app = FastAPI()

# Per-recipient inbox. seq is monotonic from 1 and is never reused.
_inboxes: dict[str, list[dict]] = defaultdict(list)
_next_seq: dict[str, int] = defaultdict(lambda: 1)
_playheads: dict[str, int] = defaultdict(int)  # default 0


class TextMessage(BaseModel):
    kind: str
    text: str = ""


class PlayheadBody(BaseModel):
    seq: int = Field(..., ge=0)


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def require_device(authorization: str | None):
    token = parse_bearer(authorization)
    device = device_for_token(token, DEVICES) if token else None
    if device is None:
        return None
    return device


def _alive(msg: dict) -> bool:
    return (time.time() - msg["created_at"]) < TTL_S


def _public(msg: dict) -> dict:
    return {
        "seq": msg["seq"],
        "kind": msg["kind"],
        "text": msg["text"],
        "created_at": msg["created_at"],
        "from": msg["from"],
    }


@app.get("/v1/me")
def me(authorization: str | None = Header(default=None)):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    return {
        "device_id": device.id,
        "peer_id": device.peer,
        "playhead": _playheads[device.id],
    }


@app.post("/v1/messages")
def post_message(body: TextMessage, authorization: str | None = Header(default=None)):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    if body.kind != "text":
        return JSONResponse({"error": "kind must be text"}, status_code=400)
    if not device.peer:
        return JSONResponse({"error": "no peer"}, status_code=400)
    seq = _next_seq[device.peer]
    _next_seq[device.peer] = seq + 1
    msg = {
        "seq": seq,
        "kind": "text",
        "text": body.text,
        "created_at": time.time(),
        "from": device.id,
    }
    _inboxes[device.peer].append(msg)
    return _public(msg)


@app.get("/v1/messages")
def get_messages(
    authorization: str | None = Header(default=None),
    after: int | None = None,
    before: int | None = None,
    limit: int | None = None,
):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    inbox = [m for m in _inboxes[device.id] if _alive(m)]

    if before is not None:
        # Archive: seq <= before, still inside TTL, oldest-first (playback order).
        cap = 10 if limit is None else max(0, limit)
        picked = [m for m in inbox if m["seq"] <= before]
        picked.sort(key=lambda m: m["seq"])
        return {"messages": [_public(m) for m in picked[:cap]]}

    after_seq = _playheads[device.id] if after is None else after
    picked = [m for m in inbox if m["seq"] > after_seq]
    picked.sort(key=lambda m: m["seq"])
    return {"messages": [_public(m) for m in picked]}


@app.put("/v1/playhead")
def put_playhead(body: PlayheadBody, authorization: str | None = Header(default=None)):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    _playheads[device.id] = body.seq
    return {"playhead": body.seq}


def main() -> None:
    global TTL_S
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument(
        "--ttl",
        type=float,
        default=TTL_S,
        help="message TTL seconds (demo default 2; production 3600)",
    )
    args = parser.parse_args()
    TTL_S = args.ttl
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
