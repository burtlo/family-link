#!/usr/bin/env python3
"""Mute-as-open presence for firmware h20 (Mazi ↔ Arlo).

POST /v1/heartbeat {available: bool} records this device's open/away flag.
The JSON response always includes self + the peer's last known state.
A status change is just a new recorded value; the friend sees it on their
next heartbeat. Away is not offline — both sides keep heartbeating.

  python demos/server/h20_presence/server.py --host 0.0.0.0 --port 8080
  make flash DEMO=h20   # each kit has its own secrets.h (box-a / box-b)
"""

from __future__ import annotations

import argparse
import time
from dataclasses import dataclass

import uvicorn
from fastapi import Body, FastAPI, Header
from fastapi.responses import JSONResponse

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer

STALE_S = 10.0
DEVICES = load_devices()
app = FastAPI()


@dataclass
class Presence:
    available: bool
    updated_at: float


# device_id -> last heartbeat
STATE: dict[str, Presence] = {}


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def require_device(authorization: str | None):
    token = parse_bearer(authorization)
    return device_for_token(token, DEVICES) if token else None


def person(device_id: str) -> dict:
    dev = DEVICES.get(device_id)
    return {
        "id": device_id,
        "name": dev.name if dev else device_id,
    }


def peer_view(peer_id: str, now: float) -> dict:
    body = person(peer_id)
    st = STATE.get(peer_id)
    if st is None or (now - st.updated_at) >= STALE_S:
        body.update({"online": False, "available": None, "updated_at": None})
        return body
    body.update(
        {
            "online": True,
            "available": st.available,
            "updated_at": st.updated_at,
        }
    )
    return body


def self_view(device_id: str, now: float) -> dict:
    body = person(device_id)
    st = STATE.get(device_id)
    if st is None:
        body.update({"online": False, "available": None, "updated_at": None})
        return body
    body.update(
        {
            "online": True,
            "available": st.available,
            "updated_at": st.updated_at,
        }
    )
    return body


@app.post("/v1/heartbeat", response_model=None)
def heartbeat(
    authorization: str | None = Header(default=None),
    body: dict | None = Body(default=None),
) -> dict | JSONResponse:
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    payload = body if isinstance(body, dict) else {}
    available = bool(payload.get("available")) if "available" in payload else False
    now = time.time()
    prev = STATE.get(device.id)
    STATE[device.id] = Presence(available=available, updated_at=now)
    if prev is None or prev.available != available:
        print(
            f"presence {device.name} ({device.id}) available={available}",
            flush=True,
        )
    return {
        "ok": True,
        "server_time": now,
        "self": self_view(device.id, now),
        "peer": peer_view(device.peer, now),
    }


@app.get("/v1/me", response_model=None)
def me(authorization: str | None = Header(default=None)) -> dict | JSONResponse:
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    now = time.time()
    return {
        "device_id": device.id,
        "name": device.name,
        "peer_id": device.peer,
        "self": self_view(device.id, now),
        "peer": peer_view(device.peer, now),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
