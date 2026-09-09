#!/usr/bin/env python3
"""Dated diary chunks for firmware h22.

While a box is unmuted it POSTs ~1 s WAV clips. This host stamps UTC
received_at and keeps the files. Muting is just the client stopping.

  python demos/server/h22_diary/server.py --host 0.0.0.0 --port 8080
  make flash DEMO=h22
"""

from __future__ import annotations

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path

import uvicorn
from fastapi import FastAPI, Header, Request
from fastapi.responses import JSONResponse, Response
from starlette.datastructures import UploadFile

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
DATA_DIR = ROOT / "data" / "h22_diary"

DEVICES = load_devices()
app = FastAPI()

# device_id -> list of chunk dicts (blob_path kept server-side)
CHUNKS: dict[str, list[dict]] = {}


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def current_device(authorization: str | None):
    token = parse_bearer(authorization)
    if not token:
        return None
    return device_for_token(token, DEVICES)


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def iso_z(dt: datetime) -> str:
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.") + f"{dt.microsecond:06d}"[:3] + "Z"


def file_stamp(dt: datetime) -> str:
    return dt.astimezone(timezone.utc).strftime("%Y%m%dT%H%M%S") + f"{dt.microsecond:06d}"[:3] + "Z"


def public_chunk(row: dict) -> dict:
    return {
        "device_id": row["device_id"],
        "name": row["name"],
        "session": row["session"],
        "seq": row["seq"],
        "received_at": row["received_at"],
        "bytes": row["bytes"],
        "id": row["id"],
    }


@app.post("/v1/diary", response_model=None)
async def post_chunk(request: Request, authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()

    form = await request.form()
    try:
        session = str(form.get("session") or "0")
        try:
            seq = int(form.get("seq") or 0)
        except (TypeError, ValueError):
            return JSONResponse({"error": "seq must be an int"}, status_code=400)
        blob_item = form.get("blob")
        blob_bytes: bytes | None = None
        if isinstance(blob_item, UploadFile):
            blob_bytes = await blob_item.read()
            if not blob_bytes:
                blob_bytes = None
        if blob_bytes is None:
            return JSONResponse({"error": "blob required"}, status_code=400)
    finally:
        await form.close()

    now = utc_now()
    received_at = iso_z(now)
    chunk_id = f"{file_stamp(now)}-{seq:04d}"
    dest = DATA_DIR / device.id / f"{chunk_id}.wav"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(blob_bytes)

    row = {
        "id": chunk_id,
        "device_id": device.id,
        "name": device.name,
        "session": session,
        "seq": seq,
        "received_at": received_at,
        "bytes": len(blob_bytes),
        "blob_path": dest,
    }
    box = CHUNKS.setdefault(device.id, [])
    box.append(row)

    log_path = DATA_DIR / device.id / "journal.jsonl"
    with log_path.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(public_chunk(row)) + "\n")

    print(
        f"diary {device.name} ({device.id}) session={session} seq={seq} "
        f"received_at={received_at} bytes={len(blob_bytes)}",
        flush=True,
    )
    out = public_chunk(row)
    out["ok"] = True
    return out


@app.get("/v1/diary", response_model=None)
def list_chunks(authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()
    rows = [public_chunk(row) for row in CHUNKS.get(device.id, [])]
    return {
        "device_id": device.id,
        "name": device.name,
        "chunks": rows,
    }


@app.get("/v1/diary/{chunk_id}/blob", response_model=None)
def get_blob(chunk_id: str, authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()
    for row in CHUNKS.get(device.id, []):
        if row["id"] == chunk_id:
            data = Path(row["blob_path"]).read_bytes()
            return Response(content=data, media_type="audio/wav")
    return JSONResponse({"error": "not found"}, status_code=404)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
