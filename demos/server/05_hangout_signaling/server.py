#!/usr/bin/env python3
"""Hangout signaling over WebSocket: invite/ring/accept, floor, hangup. No audio."""

from __future__ import annotations

import argparse
import asyncio
import json
from dataclasses import dataclass, field
from typing import Any

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from demos.server._shared.registry import device_for_token, load_devices

DEVICES = load_devices()
app = FastAPI()
INVITE_TIMEOUT_S = 2.0


@dataclass
class Session:
    inviter: str
    callee: str
    phase: str  # "ringing" | "active"
    floor_holder: str | None = None
    timeout_task: asyncio.Task[None] | None = None
    token: int = 0


@dataclass
class Hub:
    """One in-memory hangout at a time. Hello-auth is local to this demo."""

    invite_timeout_s: float
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    connections: dict[str, WebSocket] = field(default_factory=dict)
    session: Session | None = None
    next_token: int = 1

    async def send(self, device_id: str, payload: dict[str, Any]) -> None:
        ws = self.connections.get(device_id)
        if ws is None:
            return
        try:
            await ws.send_text(json.dumps(payload))
        except Exception:
            return

    async def send_both(self, payload: dict[str, Any], a: str, b: str) -> None:
        await self.send(a, payload)
        await self.send(b, payload)

    async def hello(self, websocket: WebSocket, msg: dict[str, Any]) -> str | None:
        if msg.get("type") != "hello":
            await websocket.close(code=4401)
            return None
        device_id = msg.get("device_id")
        token = msg.get("token")
        if not isinstance(device_id, str) or not isinstance(token, str):
            await websocket.close(code=4401)
            return None
        device = device_for_token(token, DEVICES)
        if device is None or device.id != device_id:
            await websocket.close(code=4401)
            return None
        async with self.lock:
            self.connections[device.id] = websocket
        await self.send(
            device.id,
            {"type": "hello_ok", "device_id": device.id, "peer_id": device.peer},
        )
        return device.id

    async def clear_session(self, *, cancel_timeout: bool = True) -> None:
        sess = self.session
        self.session = None
        if sess is None:
            return
        task = sess.timeout_task
        sess.timeout_task = None
        if cancel_timeout and task is not None and not task.done():
            task.cancel()

    async def on_timeout(self, token: int) -> None:
        async with self.lock:
            sess = self.session
            if sess is None or sess.token != token or sess.phase != "ringing":
                return
            inviter, callee = sess.inviter, sess.callee
            await self.clear_session(cancel_timeout=False)
        await self.send_both({"type": "timeout"}, inviter, callee)

    async def handle(self, device_id: str, msg: dict[str, Any]) -> None:
        typ = msg.get("type")
        if typ == "invite":
            await self._invite(device_id)
        elif typ == "accept":
            await self._accept(device_id)
        elif typ == "floor_request":
            await self._floor_request(device_id)
        elif typ == "floor_release":
            await self._floor_release(device_id)
        elif typ == "hangup":
            await self._hangup()

    async def _invite(self, device_id: str) -> None:
        device = DEVICES.get(device_id)
        if device is None or not device.peer:
            return
        async with self.lock:
            if self.session is not None:
                return
            token = self.next_token
            self.next_token += 1
            sess = Session(
                inviter=device_id,
                callee=device.peer,
                phase="ringing",
                token=token,
            )
            self.session = sess
            sess.timeout_task = asyncio.create_task(self._watch_invite(token))
            callee = sess.callee
        await self.send(callee, {"type": "ring"})

    async def _watch_invite(self, token: int) -> None:
        try:
            await asyncio.sleep(self.invite_timeout_s)
        except asyncio.CancelledError:
            return
        await self.on_timeout(token)

    async def _accept(self, device_id: str) -> None:
        async with self.lock:
            sess = self.session
            if sess is None or sess.phase != "ringing" or device_id != sess.callee:
                return
            task = sess.timeout_task
            sess.timeout_task = None
            sess.phase = "active"
            sess.floor_holder = None
            inviter, callee = sess.inviter, sess.callee
        if task is not None and not task.done():
            task.cancel()
        await self.send_both({"type": "session_start"}, inviter, callee)
        await self.send_both({"type": "floor", "holder": None}, inviter, callee)

    async def _floor_request(self, device_id: str) -> None:
        async with self.lock:
            sess = self.session
            if sess is None or sess.phase != "active":
                return
            if device_id not in (sess.inviter, sess.callee):
                return
            if sess.floor_holder is None:
                sess.floor_holder = device_id
                inviter, callee, holder = sess.inviter, sess.callee, sess.floor_holder
                await self.send_both({"type": "floor", "holder": holder}, inviter, callee)
                return
            if sess.floor_holder != device_id:
                await self.send(device_id, {"type": "floor_denied"})

    async def _floor_release(self, device_id: str) -> None:
        async with self.lock:
            sess = self.session
            if sess is None or sess.phase != "active":
                return
            if sess.floor_holder != device_id:
                return
            sess.floor_holder = None
            inviter, callee = sess.inviter, sess.callee
        await self.send_both({"type": "floor", "holder": None}, inviter, callee)

    async def _hangup(self) -> None:
        async with self.lock:
            sess = self.session
            if sess is None:
                return
            inviter, callee = sess.inviter, sess.callee
            await self.clear_session()
        await self.send_both({"type": "hangup"}, inviter, callee)

    async def drop(self, device_id: str | None, websocket: WebSocket) -> None:
        if device_id is None:
            return
        async with self.lock:
            if self.connections.get(device_id) is websocket:
                self.connections.pop(device_id, None)
            sess = self.session
            if sess is None or device_id not in (sess.inviter, sess.callee):
                return
            inviter, callee = sess.inviter, sess.callee
            await self.clear_session()
        await self.send_both({"type": "hangup"}, inviter, callee)


hub = Hub(invite_timeout_s=INVITE_TIMEOUT_S)


@app.websocket("/v1/ws")
async def ws_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    device_id: str | None = None
    try:
        first = await websocket.receive_text()
        try:
            msg = json.loads(first)
        except json.JSONDecodeError:
            await websocket.close(code=4401)
            return
        if not isinstance(msg, dict):
            await websocket.close(code=4401)
            return
        device_id = await hub.hello(websocket, msg)
        if device_id is None:
            return
        while True:
            raw = await websocket.receive_text()
            try:
                body = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if isinstance(body, dict):
                await hub.handle(device_id, body)
    except WebSocketDisconnect:
        pass
    finally:
        await hub.drop(device_id, websocket)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--invite-timeout", type=float, default=2.0)
    args = parser.parse_args()
    hub.invite_timeout_s = args.invite_timeout
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
