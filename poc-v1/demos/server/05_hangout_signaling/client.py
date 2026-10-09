#!/usr/bin/env python3
"""Exercise hangout signaling: invite/ring/accept, floor, hangup, timeout."""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from typing import Any
from urllib.parse import urlparse

import websockets
from websockets.asyncio.client import ClientConnection


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def ws_url(base_url: str) -> str:
    parsed = urlparse(base_url)
    scheme = "wss" if parsed.scheme == "https" else "ws"
    return f"{scheme}://{parsed.netloc}/v1/ws"


async def send(ws: ClientConnection, typ: str, **fields: Any) -> None:
    await ws.send(json.dumps({"type": typ, **fields}))


async def recv(ws: ClientConnection, timeout: float = 5.0) -> dict[str, Any]:
    raw = await asyncio.wait_for(ws.recv(), timeout=timeout)
    if not isinstance(raw, str):
        raise AssertionError(f"expected text frame, got {type(raw)!r}")
    msg = json.loads(raw)
    if not isinstance(msg, dict):
        raise AssertionError(f"expected object, got {msg!r}")
    return msg


async def expect(
    ws: ClientConnection, typ: str, timeout: float = 5.0, **fields: Any
) -> dict[str, Any]:
    msg = await recv(ws, timeout=timeout)
    if msg.get("type") != typ:
        raise AssertionError(f"expected type={typ!r} got {msg}")
    for key, value in fields.items():
        if msg.get(key) != value:
            raise AssertionError(f"expected {key}={value!r} got {msg}")
    return msg


async def hello(ws: ClientConnection, device_id: str, token: str, peer_id: str) -> None:
    await send(ws, "hello", device_id=device_id, token=token)
    await expect(ws, "hello_ok", device_id=device_id, peer_id=peer_id)


async def expect_session_start(a: ClientConnection, b: ClientConnection) -> None:
    await asyncio.gather(expect(a, "session_start"), expect(b, "session_start"))
    await asyncio.gather(
        expect(a, "floor", holder=None),
        expect(b, "floor", holder=None),
    )


async def scenario(url: str) -> None:
    async with websockets.connect(url) as box_a, websockets.connect(url) as box_b:
        await hello(box_a, "box-a", "change-me-a", "box-b")
        await hello(box_b, "box-b", "change-me-b", "box-a")

        await send(box_a, "invite")
        await expect(box_b, "ring")
        await send(box_b, "accept")
        await expect_session_start(box_a, box_b)

        await send(box_a, "floor_request")
        await asyncio.gather(
            expect(box_a, "floor", holder="box-a"),
            expect(box_b, "floor", holder="box-a"),
        )
        await send(box_b, "floor_request")
        await expect(box_b, "floor_denied")
        await send(box_a, "floor_release")
        await asyncio.gather(
            expect(box_a, "floor", holder=None),
            expect(box_b, "floor", holder=None),
        )

        await send(box_a, "hangup")
        await asyncio.gather(expect(box_a, "hangup"), expect(box_b, "hangup"))

        await send(box_a, "invite")
        await expect(box_b, "ring")
        await send(box_b, "accept")
        await expect_session_start(box_a, box_b)

        await send(box_a, "hangup")
        await asyncio.gather(expect(box_a, "hangup"), expect(box_b, "hangup"))

        await send(box_a, "invite")
        await expect(box_b, "ring")
        await asyncio.gather(
            expect(box_a, "timeout", timeout=8.0),
            expect(box_b, "timeout", timeout=8.0),
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    url = ws_url(args.base_url)
    try:
        asyncio.run(scenario(url))
    except Exception as exc:
        return fail(str(exc))
    print("-- PASS 05_hangout_signaling")
    return 0


if __name__ == "__main__":
    sys.exit(main())
