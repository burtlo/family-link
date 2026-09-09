#!/usr/bin/env python3
"""Full-duplex PCM copy-through for firmware h21 (Mazi ↔ Arlo talk).

No invite, no floor. Each connected device's binary frames are copied to
its peer. Kits start muted and only send while the mute latch is up.

JSON `status` / hello `available` is the friend's mute line: the other
kit should show muted/talking, not an empty waiting card.

  python -m demos.server.h21_talk.server --host 0.0.0.0 --port 8080
  make flash DEMO=h21 WHO=mazi PORT=/dev/cu.usbmodem…
  make flash DEMO=h21 WHO=arlo PORT=/dev/cu.usbmodem…
"""

from __future__ import annotations

import argparse
import json
import time

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from demos.server._shared.registry import device_for_token, load_devices

DEVICES = load_devices()
app = FastAPI()

AUTH_FAIL = 4401
connections: dict[str, WebSocket] = {}
last_available: dict[str, bool] = {}


async def send_json(device_id: str, payload: dict) -> None:
    ws = connections.get(device_id)
    if ws is None:
        return
    try:
        await ws.send_json(payload)
    except Exception:
        return


async def send_bytes(device_id: str, data: bytes) -> None:
    ws = connections.get(device_id)
    if ws is None:
        return
    try:
        await ws.send_bytes(data)
    except Exception:
        return


def record_available(device_id: str, raw) -> bool:
    available = bool(raw)
    prev = last_available.get(device_id)
    last_available[device_id] = available
    if prev is not available:
        device = DEVICES.get(device_id)
        name = device.name if device else device_id
        print(f"talk status {name} ({device_id}) available={available}", flush=True)
    return available


async def notify_peer_status(device_id: str, *, online: bool) -> None:
    device = DEVICES.get(device_id)
    if device is None or not device.peer:
        return
    available = bool(last_available.get(device_id)) if online else False
    await send_json(
        device.peer,
        {
            "type": "peer_status",
            "peer_id": device_id,
            "peer_name": device.name,
            "online": online,
            "available": available,
        },
    )


def hello_ok_body(device_id: str) -> dict:
    device = DEVICES[device_id]
    peer = DEVICES.get(device.peer)
    peer_online = device.peer in connections
    peer_available = last_available.get(device.peer) if peer_online else None
    return {
        "type": "hello_ok",
        "device_id": device.id,
        "name": device.name,
        "peer_id": device.peer,
        "peer_name": peer.name if peer else device.peer,
        "peer_online": peer_online,
        "peer_available": peer_available,
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
        await websocket.close(code=AUTH_FAIL)
        return None
    old = connections.get(device.id)
    connections[device.id] = websocket
    if old is not None and old is not websocket:
        try:
            await old.close()
        except Exception:
            pass
    if "available" in body:
        record_available(device.id, body.get("available"))
    peer_online = device.peer in connections
    await websocket.send_json(hello_ok_body(device.id))
    if peer_online:
        await notify_peer_status(device.id, online=True)
    print(
        f"talk hello {device.name} ({device.id}) peer_online={peer_online} "
        f"available={last_available.get(device.id)}",
        flush=True,
    )
    return device.id


async def handle_status(device_id: str, raw: str) -> None:
    try:
        body = json.loads(raw)
    except json.JSONDecodeError:
        return
    if not isinstance(body, dict) or body.get("type") != "status":
        return
    record_available(device_id, body.get("available"))
    await notify_peer_status(device_id, online=True)


@app.websocket("/v1/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    device_id: str | None = None
    frames = 0
    first_t: float | None = None
    try:
        device_id = await handle_hello(websocket)
        if device_id is None:
            return
        device = DEVICES[device_id]
        while True:
            message = await websocket.receive()
            if message["type"] == "websocket.disconnect":
                break
            if message.get("bytes") is not None:
                data = message["bytes"]
                now = time.time()
                if frames == 0:
                    first_t = now
                    print(
                        f"talk first_frame from {device.name} ({device.id}) t={now:.6f}",
                        flush=True,
                    )
                frames += 1
                await send_bytes(device.peer, data)
            elif message.get("text") is not None:
                await handle_status(device_id, message["text"])
    except WebSocketDisconnect:
        pass
    finally:
        if device_id is not None and connections.get(device_id) is websocket:
            del connections[device_id]
            await notify_peer_status(device_id, online=False)
            if frames and first_t is not None:
                span_ms = (time.time() - first_t) * 1000.0
                print(
                    f"talk {device_id} frames={frames} span_ms={span_ms:.1f}",
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
