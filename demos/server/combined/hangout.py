"""Hangout signaling + PCM copy-through. Same Session shape as 06_audio_relay."""

from __future__ import annotations

import json
import time
from dataclasses import dataclass

from fastapi import WebSocket

from demos.server._shared.registry import device_for_token, load_devices

DEVICES = load_devices()
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


async def attach(device_id: str, websocket: WebSocket) -> None:
    """Replace an existing socket for this device (like 06)."""
    old = connections.get(device_id)
    connections[device_id] = websocket
    if old is not None and old is not websocket:
        try:
            await old.close()
        except Exception:
            pass


async def drop(device_id: str | None, websocket: WebSocket) -> None:
    if device_id is None:
        return
    if connections.get(device_id) is websocket:
        del connections[device_id]
        sess = session
        if sess is not None and device_id in pair_of(sess):
            await end_session(send_hangup=True)


def parse_hello(raw: str) -> dict | None:
    try:
        body = json.loads(raw)
    except json.JSONDecodeError:
        return None
    if not isinstance(body, dict) or body.get("type") != "hello":
        return None
    return body


def device_from_hello(body: dict):
    token = str(body.get("token") or "")
    claimed = str(body.get("device_id") or "")
    device = device_for_token(token, DEVICES)
    if device is None or device.id != claimed:
        return None
    return device


async def reject_hello(websocket: WebSocket) -> None:
    await websocket.close(code=AUTH_FAIL)


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
