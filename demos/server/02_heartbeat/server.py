#!/usr/bin/env python3
"""In-memory presence: last_seen from POST /v1/heartbeat, stale after 10s."""

from __future__ import annotations

import argparse
import time

import uvicorn
from fastapi import Body, FastAPI, Header, Request
from fastapi.responses import JSONResponse

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer

STALE_S = 10.0
DEVICES = load_devices()
# device_id -> unix timestamp of last successful heartbeat
LAST_SEEN: dict[str, float] = {}
# device_id -> client IP from the last heartbeat (proves which network hit us)
LAST_CLIENT: dict[str, str] = {}
app = FastAPI()


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def require_device(authorization: str | None):
    token = parse_bearer(authorization)
    device = device_for_token(token, DEVICES) if token else None
    if device is None:
        return None
    return device


def peer_online(peer_id: str, now: float) -> bool:
    ts = LAST_SEEN.get(peer_id)
    if ts is None:
        return False
    return (now - ts) < STALE_S


def client_ip(request: Request) -> str:
    forwarded = request.headers.get("x-forwarded-for")
    if forwarded:
        return forwarded.split(",")[0].strip()
    if request.client:
        return request.client.host
    return "?"


@app.post("/v1/heartbeat", response_model=None)
def heartbeat(
    request: Request,
    authorization: str | None = Header(default=None),
    body: dict | None = Body(default=None),
) -> dict | JSONResponse:
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    _ = body  # optional {uptime_s, rssi}; presence does not persist them
    now = time.time()
    LAST_SEEN[device.id] = now
    ip = client_ip(request)
    LAST_CLIENT[device.id] = ip
    online = peer_online(device.peer, now)
    print(
        f"-- heartbeat {device.id} from {ip} peer_online={str(online).lower()}",
        flush=True,
    )
    return {
        "ok": True,
        "peer_online": online,
        "server_time": now,
    }


@app.get("/v1/me", response_model=None)
def me(authorization: str | None = Header(default=None)) -> dict | JSONResponse:
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    now = time.time()
    return {
        "device_id": device.id,
        "peer_id": device.peer,
        "peer_online": peer_online(device.peer, now),
        "last_seen": LAST_SEEN.get(device.id),
    }


@app.get("/v1/status", response_model=None)
def status() -> dict:
    now = time.time()
    devices = []
    for dev in DEVICES.values():
        ts = LAST_SEEN.get(dev.id)
        devices.append(
            {
                "id": dev.id,
                "online": ts is not None and (now - ts) < STALE_S,
                "last_seen": ts,
                "age_s": None if ts is None else round(now - ts, 2),
                "client": LAST_CLIENT.get(dev.id),
            }
        )
    return {"ok": True, "now": now, "stale_s": STALE_S, "devices": devices}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
