"""WebSocket inbox alerts for v1 product endpoints."""

from __future__ import annotations

import json
from typing import Any

from fastapi import WebSocket

from demos.server._shared.hangout_registry import endpoint_for_token, load_registry

AUTH_FAIL = 4401
REGISTRY = load_registry()
connections: dict[str, WebSocket] = {}


def parse_hello(raw: str) -> dict[str, Any] | None:
    try:
        body = json.loads(raw)
    except json.JSONDecodeError:
        return None
    if not isinstance(body, dict) or body.get("type") != "hello":
        return None
    token = body.get("token")
    if not isinstance(token, str) or not token:
        return None
    return body


def endpoint_from_hello(body: dict[str, Any]):
    token = str(body.get("token"))
    return endpoint_for_token(token, REGISTRY)


async def reject_hello(websocket: WebSocket) -> None:
    await websocket.close(code=AUTH_FAIL)


async def attach(endpoint_id: str, websocket: WebSocket) -> None:
    old = connections.get(endpoint_id)
    connections[endpoint_id] = websocket
    if old is not None and old is not websocket:
        try:
            await old.close()
        except Exception:
            pass


async def drop(endpoint_id: str | None, websocket: WebSocket) -> None:
    if endpoint_id and connections.get(endpoint_id) is websocket:
        connections.pop(endpoint_id, None)


async def send_json(endpoint_id: str, payload: dict) -> None:
    ws = connections.get(endpoint_id)
    if ws is None:
        return
    try:
        await ws.send_json(payload)
    except Exception:
        return


async def notify_inbox(user_id: str, seq: int, kind: str, from_user: str) -> None:
    payload = {
        "type": "inbox",
        "user_id": user_id,
        "seq": seq,
        "kind": kind,
        "from": from_user,
    }
    for endpoint_id in REGISTRY.endpoints:
        await send_json(endpoint_id, payload)
