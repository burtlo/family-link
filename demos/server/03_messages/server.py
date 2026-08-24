#!/usr/bin/env python3
"""Async messages: text + audio blobs, per-inbox seq, WebSocket inbox alerts."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import shutil
from pathlib import Path

import uvicorn
from fastapi import FastAPI, Header, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse, Response
from starlette.datastructures import UploadFile

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
DATA_DIR = ROOT / "data" / "03_messages"

DEVICES = load_devices()
app = FastAPI()

# recipient_id -> list of message dicts
INBOXES: dict[str, list[dict]] = {}
# device_id -> waiting WS queues
SUBSCRIBERS: dict[str, list[asyncio.Queue]] = {}


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def current_device(authorization: str | None):
    token = parse_bearer(authorization)
    if not token:
        return None
    return device_for_token(token, DEVICES)


def inbox_for(device_id: str) -> list[dict]:
    return INBOXES.setdefault(device_id, [])


async def push_inbox(device_id: str, event: dict) -> None:
    for queue in list(SUBSCRIBERS.get(device_id, [])):
        await queue.put(event)


@app.post("/v1/messages", response_model=None)
async def post_message(
    request: Request, authorization: str | None = Header(default=None)
):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    if not sender.peer:
        return JSONResponse({"error": "no peer"}, status_code=400)

    form = await request.form()
    try:
        kind = str(form.get("kind") or "")
        if kind not in {"text", "audio"}:
            return JSONResponse({"error": "kind must be text or audio"}, status_code=400)
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
        if kind == "audio" and blob_bytes is None:
            return JSONResponse({"error": "blob required"}, status_code=400)
    finally:
        await form.close()

    recipient = sender.peer
    box = inbox_for(recipient)
    seq = len(box) + 1
    stored = {
        "seq": seq,
        "kind": kind,
        "from": sender.id,
        "to": recipient,
        "text": text,
        "blob_path": None,
    }
    if blob_bytes is not None:
        name = f"{seq}.wav" if kind == "audio" else str(seq)
        dest = DATA_DIR / recipient / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob_bytes)
        stored["blob_path"] = dest
    box.append(stored)
    await push_inbox(
        recipient,
        {"type": "inbox", "seq": seq, "kind": kind, "from": sender.id},
    )
    return {"seq": seq, "kind": kind, "from": sender.id, "to": recipient}


@app.get("/v1/messages", response_model=None)
def list_messages(authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()
    out = []
    for msg in inbox_for(device.id):
        item = {"seq": msg["seq"], "kind": msg["kind"], "from": msg["from"]}
        if msg.get("text") is not None:
            item["text"] = msg["text"]
        out.append(item)
    return out


@app.get("/v1/messages/{seq}/blob", response_model=None)
def get_blob(seq: int, authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()
    for msg in inbox_for(device.id):
        if msg["seq"] == seq:
            path = msg.get("blob_path")
            if path is None or not Path(path).is_file():
                return JSONResponse({"error": "no blob"}, status_code=404)
            return Response(Path(path).read_bytes(), media_type="application/octet-stream")
    return JSONResponse({"error": "not found"}, status_code=404)


@app.websocket("/v1/ws")
async def ws_inbox(websocket: WebSocket) -> None:
    await websocket.accept()
    try:
        raw = await websocket.receive_text()
    except WebSocketDisconnect:
        return
    try:
        hello = json.loads(raw)
    except Exception:
        await websocket.close(code=1008)
        return
    if hello.get("type") != "hello":
        await websocket.close(code=1008)
        return
    token = str(hello.get("token") or "")
    device = device_for_token(token, DEVICES) if token else None
    if device is None or device.id != str(hello.get("device_id") or ""):
        await websocket.close(code=1008)
        return
    queue: asyncio.Queue = asyncio.Queue()
    SUBSCRIBERS.setdefault(device.id, []).append(queue)
    await websocket.send_json(
        {"type": "hello_ok", "device_id": device.id, "peer_id": device.peer}
    )
    try:
        async def pump() -> None:
            while True:
                event = await queue.get()
                await websocket.send_json(event)

        async def watch() -> None:
            while True:
                await websocket.receive_text()

        await asyncio.gather(pump(), watch())
    except WebSocketDisconnect:
        pass
    finally:
        bucket = SUBSCRIBERS.get(device.id) or []
        if queue in bucket:
            bucket.remove(queue)


def main() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    for child in DATA_DIR.iterdir():
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
