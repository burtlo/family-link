#!/usr/bin/env python3
"""Hangout audio relay: server copies floor-holder binary WS frames to the peer."""

from __future__ import annotations

import argparse
import json
import time
from dataclasses import dataclass

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from demos.server._shared.registry import device_for_token, load_devices

DEVICES = load_devices()
app = FastAPI()

AUTH_FAIL = 4401


@dataclass
class Session:
    caller: str
    callee: str
    started: bool = False
    floor: str | None = None
    first_frame_t: float | None = None
    last_frame_t: float | None = None
    frames: int = 0


connections: dict[str, WebSocket] = {}
session: Session | None = None


def _log_relay_span(sess: Session) -> None:
    if sess.first_frame_t is None or sess.last_frame_t is None:
        return
    span_ms = (sess.last_frame_t - sess.first_frame_t) * 1000.0
    print(
        f"audio_relay first_frame t={sess.first_frame_t:.6f} "
        f"last_frame t={sess.last_frame_t:.6f} frames={sess.frames} "
        f"span_ms={span_ms:.2f}",
        flush=True,
    )


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


async def broadcast(ids: tuple[str, str], payload: dict) -> None:
    for device_id in ids:
        await send_json(device_id, payload)


def pair_of(sess: Session) -> tuple[str, str]:
    return (sess.caller, sess.callee)


def other_of(sess: Session, device_id: str) -> str:
    return sess.callee if device_id == sess.caller else sess.caller


async def end_session(*, send_hangup: bool) -> None:
    global session
    sess = session
    session = None
    if sess is None:
        return
    _log_relay_span(sess)
    if send_hangup:
        await broadcast(pair_of(sess), {"type": "hangup"})


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
    await websocket.send_json(
        {"type": "hello_ok", "device_id": device.id, "peer_id": device.peer}
    )
    return device.id


async def handle_text(sender: str, body: dict) -> None:
    global session
    kind = body.get("type")
    device = DEVICES.get(sender)
    if device is None:
        return

    if kind == "invite":
        if session is not None:
            return
        peer_id = device.peer
        if not peer_id:
            return
        session = Session(caller=sender, callee=peer_id)
        await send_json(peer_id, {"type": "ring", "from": sender})
        return

    if kind == "accept":
        sess = session
        if sess is None or sess.started or sender != sess.callee:
            return
        sess.started = True
        sess.floor = None
        pair = pair_of(sess)
        await broadcast(pair, {"type": "session_start"})
        await broadcast(pair, {"type": "floor", "holder": None})
        return

    if kind == "floor_request":
        sess = session
        if sess is None or not sess.started:
            return
        if sess.floor is None or sess.floor == sender:
            sess.floor = sender
            await broadcast(pair_of(sess), {"type": "floor", "holder": sender})
        return

    if kind == "floor_release":
        sess = session
        if sess is None or not sess.started or sess.floor != sender:
            return
        sess.floor = None
        await broadcast(pair_of(sess), {"type": "floor", "holder": None})
        return

    if kind == "hangup":
        await end_session(send_hangup=True)
        return


async def handle_binary(sender: str, data: bytes) -> None:
    sess = session
    if sess is None or not sess.started or sess.floor != sender:
        return
    now = time.time()
    if sess.frames == 0:
        sess.first_frame_t = now
        print(f"audio_relay first_frame t={now:.6f}", flush=True)
    sess.last_frame_t = now
    sess.frames += 1
    await send_bytes(other_of(sess, sender), data)


@app.websocket("/v1/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    device_id: str | None = None
    try:
        device_id = await handle_hello(websocket)
        if device_id is None:
            return
        while True:
            message = await websocket.receive()
            if message["type"] == "websocket.disconnect":
                break
            if message.get("bytes") is not None:
                await handle_binary(device_id, message["bytes"])
            elif message.get("text") is not None:
                try:
                    body = json.loads(message["text"])
                except json.JSONDecodeError:
                    continue
                if isinstance(body, dict):
                    await handle_text(device_id, body)
    except WebSocketDisconnect:
        pass
    finally:
        if device_id is not None and connections.get(device_id) is websocket:
            del connections[device_id]
            sess = session
            if sess is not None and device_id in pair_of(sess):
                await end_session(send_hangup=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
