#!/usr/bin/env python3
"""Touch-coordinate copy-through for firmware h26 (Mazi ↔ Arlo draw).

No invite, no floor, no audio. Each connected device's JSON `stroke` /
`clear` frames are copied to its peer. Draw on one glass; the other
paints the same 320×240 points.

  python -m demos.server.h26_draw.server --host 0.0.0.0 --port 8080
  make flash DEMO=h26 WHO=mazi PORT=/dev/cu.usbmodem…
  make flash DEMO=h26 WHO=arlo PORT=/dev/cu.usbmodem…
"""

from __future__ import annotations

import argparse
import json

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from demos.server._shared.registry import device_for_token, load_devices

DEVICES = load_devices()
app = FastAPI()

AUTH_FAIL = 4401
connections: dict[str, WebSocket] = {}


async def send_json(device_id: str, payload: dict) -> None:
    ws = connections.get(device_id)
    if ws is None:
        return
    try:
        await ws.send_json(payload)
    except Exception:
        return


async def notify_peer_status(device_id: str, *, online: bool) -> None:
    device = DEVICES.get(device_id)
    if device is None or not device.peer:
        return
    await send_json(
        device.peer,
        {
            "type": "peer_status",
            "peer_id": device_id,
            "peer_name": device.name,
            "online": online,
        },
    )


def hello_ok_body(device_id: str) -> dict:
    device = DEVICES[device_id]
    peer = DEVICES.get(device.peer)
    peer_online = device.peer in connections
    return {
        "type": "hello_ok",
        "device_id": device.id,
        "name": device.name,
        "peer_id": device.peer,
        "peer_name": peer.name if peer else device.peer,
        "peer_online": peer_online,
    }


async def handle_hello(websocket: WebSocket) -> str | None:
    try:
        raw = await websocket.receive_text()
        body = json.loads(raw)
    except (WebSocketDisconnect, json.JSONDecodeError, KeyError):
        await websocket.close(code=AUTH_FAIL)
        return None
    if not isinstance(body, dict) or body.get("type") != "hello":
        await websocket.close(code=AUTH_FAIL)
        return None
    token = str(body.get("token") or "")
    claimed = str(body.get("device_id") or "")
    device = device_for_token(token, DEVICES)
    if device is None or device.id != claimed:
        print(
            f"draw hello rejected claimed={claimed!r} token={token[:8]}…",
            flush=True,
        )
        await websocket.close(code=AUTH_FAIL)
        return None
    old = connections.get(device.id)
    connections[device.id] = websocket
    if old is not None and old is not websocket:
        try:
            await old.close()
        except Exception:
            pass
    peer_online = device.peer in connections
    await websocket.send_json(hello_ok_body(device.id))
    if peer_online:
        await notify_peer_status(device.id, online=True)
    print(
        f"draw hello {device.name} ({device.id}) peer_online={peer_online}",
        flush=True,
    )
    return device.id


async def handle_draw(device_id: str, raw: str) -> None:
    try:
        body = json.loads(raw)
    except json.JSONDecodeError:
        return
    if not isinstance(body, dict):
        return
    kind = body.get("type")
    if kind not in ("stroke", "clear"):
        return
    device = DEVICES[device_id]
    if kind == "clear":
        print(f"draw clear from {device.name} ({device_id})", flush=True)
    elif body.get("phase") == "down":
        print(
            f"draw down from {device.name} ({device_id}) "
            f"x={body.get('x')} y={body.get('y')}",
            flush=True,
        )
    await send_json(device.peer, body)


@app.get("/")
async def root() -> dict:
    return {
        "ok": True,
        "demo": "h26_draw",
        "connected": sorted(connections),
    }


@app.websocket("/v1/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    device_id: str | None = None
    strokes = 0
    clears = 0
    try:
        device_id = await handle_hello(websocket)
        if device_id is None:
            return
        while True:
            message = await websocket.receive()
            if message["type"] == "websocket.disconnect":
                break
            if message.get("text") is None:
                continue
            raw = message["text"]
            try:
                body = json.loads(raw)
            except json.JSONDecodeError:
                continue
            kind = body.get("type") if isinstance(body, dict) else None
            if kind == "stroke":
                strokes += 1
            elif kind == "clear":
                clears += 1
            await handle_draw(device_id, raw)
    except WebSocketDisconnect:
        pass
    finally:
        if device_id is not None and connections.get(device_id) is websocket:
            del connections[device_id]
            await notify_peer_status(device_id, online=False)
            if strokes or clears:
                print(
                    f"draw {device_id} strokes={strokes} clears={clears}",
                    flush=True,
                )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
