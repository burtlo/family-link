#!/usr/bin/env python3
"""Known-device bearer auth: GET /v1/me returns the device that owns the token."""

from __future__ import annotations

import argparse

import uvicorn
from fastapi import FastAPI, Header
from fastapi.responses import JSONResponse

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer

DEVICES = load_devices()
app = FastAPI()


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


@app.get("/v1/me")
def me(authorization: str | None = Header(default=None)):
    token = parse_bearer(authorization)
    device = device_for_token(token, DEVICES) if token else None
    if device is None:
        return unauthorized()
    return {"device_id": device.id, "peer_id": device.peer, "role": device.role}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--ssl-certfile", default=None)
    parser.add_argument("--ssl-keyfile", default=None)
    args = parser.parse_args()
    ssl = {}
    if args.ssl_certfile or args.ssl_keyfile:
        if not args.ssl_certfile or not args.ssl_keyfile:
            parser.error("both --ssl-certfile and --ssl-keyfile are required")
        ssl["ssl_certfile"] = args.ssl_certfile
        ssl["ssl_keyfile"] = args.ssl_keyfile
    uvicorn.run(app, host=args.host, port=args.port, **ssl)


if __name__ == "__main__":
    main()
