#!/usr/bin/env python3
"""One FastAPI process a real box and a parent page can share.

Island demos 01–06 stay as proofs. This is the glue: REST inbox + one
WebSocket for mail alerts, presence, and hangout relay.
"""

from __future__ import annotations

import argparse
import json
import os
import time
from collections import defaultdict
from io import BytesIO
from pathlib import Path

import uvicorn
from fastapi import FastAPI, Header, Request, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
from starlette.datastructures import UploadFile

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer
from demos.server.combined import hangout

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
DATA_DIR = ROOT / "data" / "combined"
WEB_DIR = ROOT / "demos" / "parent" / "web"

STALE_S = 10.0
TTL_S = float(os.environ.get("FAMILY_TTL_S", "3600"))
PREVIEW_W, PREVIEW_H = 320, 240
PREVIEW_BYTES = PREVIEW_W * PREVIEW_H * 2

DEVICES = load_devices()
LAST_SEEN: dict[str, float] = {}
INBOXES: dict[str, list[dict]] = defaultdict(list)
NEXT_SEQ: dict[str, int] = defaultdict(lambda: 1)
PLAYHEADS: dict[str, int] = defaultdict(int)

app = FastAPI()
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class PlayheadBody(BaseModel):
    seq: int = Field(..., ge=0)


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def require_device(authorization: str | None):
    token = parse_bearer(authorization)
    return device_for_token(token, DEVICES) if token else None


def peer_online(peer_id: str, now: float) -> bool:
    ts = LAST_SEEN.get(peer_id)
    if ts is None:
        return False
    return (now - ts) < STALE_S


def alive(msg: dict) -> bool:
    return (time.time() - msg["created_at"]) < TTL_S


def unread_count(device_id: str) -> int:
    playhead = PLAYHEADS[device_id]
    return sum(1 for m in INBOXES[device_id] if alive(m) and m["seq"] > playhead)


def public_message(msg: dict) -> dict:
    item = {"seq": msg["seq"], "kind": msg["kind"], "from": msg["from"]}
    if msg.get("text") is not None:
        item["text"] = msg["text"]
    return item


def blob_path(recipient: str, seq: int) -> Path:
    return DATA_DIR / recipient / str(seq)


def jpeg_to_rgb565(data: bytes) -> bytes:
    from PIL import Image, ImageOps

    img = Image.open(BytesIO(data)).convert("RGB")
    fitted = ImageOps.fit(img, (PREVIEW_W, PREVIEW_H), method=Image.Resampling.LANCZOS)
    pixels = fitted.tobytes()
    out = bytearray(PREVIEW_BYTES)
    for i in range(PREVIEW_W * PREVIEW_H):
        r = pixels[i * 3]
        g = pixels[i * 3 + 1]
        b = pixels[i * 3 + 2]
        val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[i * 2] = val & 0xFF
        out[i * 2 + 1] = val >> 8
    return bytes(out)


async def touch_seen(device_id: str) -> None:
    now = time.time()
    was = peer_online(device_id, now)
    LAST_SEEN[device_id] = now
    if not was:
        device = DEVICES.get(device_id)
        if device is not None and device.peer:
            await hangout.send_json(
                device.peer,
                {"type": "presence", "peer_id": device_id, "online": True},
            )


def ensure_parent_web() -> Path | None:
    if WEB_DIR.exists() and not WEB_DIR.is_dir():
        return None
    WEB_DIR.mkdir(parents=True, exist_ok=True)
    index = WEB_DIR / "index.html"
    if not index.exists():
        index.write_text("parent page coming\n", encoding="utf-8")
    return WEB_DIR


@app.get("/v1/me", response_model=None)
def me(authorization: str | None = Header(default=None)):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    now = time.time()
    return {
        "device_id": device.id,
        "peer_id": device.peer,
        "role": device.role,
        "playhead": PLAYHEADS[device.id],
        "unread": unread_count(device.id),
        "peer_online": peer_online(device.peer, now),
    }


@app.post("/v1/heartbeat", response_model=None)
async def heartbeat(
    request: Request, authorization: str | None = Header(default=None)
):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    if request.headers.get("content-length") not in (None, "0"):
        try:
            await request.json()
        except Exception:
            pass
    now = time.time()
    await touch_seen(device.id)
    return {
        "ok": True,
        "peer_online": peer_online(device.peer, now),
        "server_time": now,
    }


@app.post("/v1/messages", response_model=None)
async def post_message(
    request: Request, authorization: str | None = Header(default=None)
):
    sender = require_device(authorization)
    if sender is None:
        return unauthorized()
    if not sender.peer:
        return JSONResponse({"error": "no peer"}, status_code=400)

    form = await request.form()
    try:
        kind = str(form.get("kind") or "")
        if kind not in {"text", "audio", "image"}:
            return JSONResponse(
                {"error": "kind must be text, audio, or image"}, status_code=400
            )
        raw_text = form.get("text")
        text = None if raw_text in (None, "") else str(raw_text)
        blob_item = form.get("blob")
        blob_bytes: bytes | None = None
        if isinstance(blob_item, UploadFile):
            blob_bytes = await blob_item.read()
            if not blob_bytes:
                blob_bytes = None
        if kind == "text" and text is None:
            return JSONResponse({"error": "text required"}, status_code=400)
        if kind in {"audio", "image"} and blob_bytes is None:
            return JSONResponse({"error": "blob required"}, status_code=400)
    finally:
        await form.close()

    recipient = sender.peer
    seq = NEXT_SEQ[recipient]
    NEXT_SEQ[recipient] = seq + 1
    stored = {
        "seq": seq,
        "kind": kind,
        "from": sender.id,
        "to": recipient,
        "text": text,
        "created_at": time.time(),
        "has_blob": blob_bytes is not None,
    }
    if blob_bytes is not None:
        dest = blob_path(recipient, seq)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob_bytes)
    INBOXES[recipient].append(stored)
    await hangout.send_json(
        recipient,
        {"type": "inbox", "seq": seq, "kind": kind, "from": sender.id},
    )
    return {"seq": seq, "kind": kind, "from": sender.id, "to": recipient}


@app.get("/v1/messages", response_model=None)
def list_messages(
    authorization: str | None = Header(default=None),
    after: int | None = None,
    before: int | None = None,
    limit: int | None = None,
):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    inbox = [m for m in INBOXES[device.id] if alive(m)]

    if before is not None:
        cap = 10 if limit is None else max(0, limit)
        picked = [m for m in inbox if m["seq"] <= before]
        picked.sort(key=lambda m: m["seq"])
        return [public_message(m) for m in picked[:cap]]

    after_seq = PLAYHEADS[device.id] if after is None else after
    picked = [m for m in inbox if m["seq"] > after_seq]
    picked.sort(key=lambda m: m["seq"])
    return [public_message(m) for m in picked]


@app.get("/v1/messages/{seq}/blob", response_model=None)
def get_blob(seq: int, authorization: str | None = Header(default=None)):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    for msg in INBOXES[device.id]:
        if msg["seq"] == seq:
            path = blob_path(device.id, seq)
            if not msg.get("has_blob") or not path.is_file():
                return JSONResponse({"error": "no blob"}, status_code=404)
            return Response(path.read_bytes(), media_type="application/octet-stream")
    return JSONResponse({"error": "not found"}, status_code=404)


@app.get("/v1/messages/{seq}/preview", response_model=None)
def get_preview(seq: int, authorization: str | None = Header(default=None)):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    for msg in INBOXES[device.id]:
        if msg["seq"] != seq:
            continue
        if msg["kind"] != "image":
            return JSONResponse({"error": "not an image"}, status_code=404)
        path = blob_path(device.id, seq)
        if not path.is_file():
            return JSONResponse({"error": "no blob"}, status_code=404)
        try:
            preview = jpeg_to_rgb565(path.read_bytes())
        except ImportError:
            return JSONResponse({"error": "Pillow missing"}, status_code=404)
        except Exception:
            return JSONResponse({"error": "preview failed"}, status_code=404)
        return Response(preview, media_type="application/octet-stream")
    return JSONResponse({"error": "not found"}, status_code=404)


@app.put("/v1/playhead", response_model=None)
def put_playhead(
    body: PlayheadBody, authorization: str | None = Header(default=None)
):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    PLAYHEADS[device.id] = body.seq
    return {"playhead": body.seq}


@app.delete("/v1/messages/{seq}", response_model=None)
def delete_message(seq: int, authorization: str | None = Header(default=None)):
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    box = INBOXES[device.id]
    for i, msg in enumerate(box):
        if msg["seq"] == seq:
            box.pop(i)
            path = blob_path(device.id, seq)
            if path.is_file():
                path.unlink()
            return Response(status_code=204)
    return JSONResponse({"error": "not found"}, status_code=404)


@app.websocket("/v1/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    device_id: str | None = None
    try:
        try:
            raw = await websocket.receive_text()
        except WebSocketDisconnect:
            return
        body = hangout.parse_hello(raw)
        if body is None:
            await hangout.reject_hello(websocket)
            return
        device = hangout.device_from_hello(body)
        if device is None:
            await hangout.reject_hello(websocket)
            return
        device_id = device.id
        await hangout.attach(device_id, websocket)
        await websocket.send_json(
            {
                "type": "hello_ok",
                "device_id": device.id,
                "peer_id": device.peer,
                "playhead": PLAYHEADS[device.id],
            }
        )
        await touch_seen(device_id)
        while True:
            message = await websocket.receive()
            if message["type"] == "websocket.disconnect":
                break
            await touch_seen(device_id)
            if message.get("bytes") is not None:
                await hangout.handle_binary(device_id, message["bytes"])
            elif message.get("text") is not None:
                try:
                    payload = json.loads(message["text"])
                except json.JSONDecodeError:
                    continue
                if isinstance(payload, dict):
                    await hangout.handle_text(device_id, payload)
    except WebSocketDisconnect:
        pass
    finally:
        await hangout.drop(device_id, websocket)


def _mount_parent_page() -> None:
    web = ensure_parent_web()
    if web is not None and web.is_dir():
        app.mount("/app", StaticFiles(directory=str(web), html=True), name="parent")


_mount_parent_page()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--ssl-certfile", default=None)
    parser.add_argument("--ssl-keyfile", default=None)
    args = parser.parse_args()
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    kwargs: dict = {}
    if args.ssl_certfile or args.ssl_keyfile:
        if not args.ssl_certfile or not args.ssl_keyfile:
            parser.error("both --ssl-certfile and --ssl-keyfile are required")
        kwargs["ssl_certfile"] = args.ssl_certfile
        kwargs["ssl_keyfile"] = args.ssl_keyfile
    uvicorn.run(app, host=args.host, port=args.port, **kwargs)


if __name__ == "__main__":
    main()
