#!/usr/bin/env python3
"""Store-and-forward drawing notes for firmware h27 (Mazi ↔ Arlo).

POST a timed stroke clip; the peer opens it later. Not live copy-through
(that is h26). The friend does not need to be at the glass when you send.

  python -m demos.server.h27_sketch.server --host 0.0.0.0 --port 8080
  make flash DEMO=h27 WHO=mazi
  make flash DEMO=h27 WHO=arlo
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

import uvicorn
from fastapi import FastAPI, Header, Request
from fastapi.responses import JSONResponse, Response

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer
from demos.server.h27_sketch.codec import MAX_POINTS, PT_LEN, HDR_LEN, unpack_sketch, SketchError

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
DATA_DIR = ROOT / "data" / "h27_sketch"

DEVICES = load_devices()
app = FastAPI()

# recipient id -> list of sketch dicts
INBOX: dict[str, list[dict]] = {}
SEQ: dict[str, int] = {}


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def current_device(authorization: str | None):
    token = parse_bearer(authorization)
    if not token:
        return None
    return device_for_token(token, DEVICES)


def public_row(row: dict) -> dict:
    return {
        "id": row["id"],
        "from": row["from_id"],
        "from_name": row["from_name"],
        "points": row["points"],
        "ms": row["ms"],
        "bytes": row["bytes"],
    }


def inbox_unread(device_id: str) -> list[dict]:
    return [row for row in INBOX.get(device_id, []) if not row["read"]]


@app.get("/")
async def root() -> dict:
    return {
        "ok": True,
        "demo": "h27_sketch",
        "unread": {did: len(inbox_unread(did)) for did in sorted(INBOX)},
    }


@app.post("/v1/sketches", response_model=None)
async def post_sketch(request: Request, authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()
    if not device.peer or device.peer not in DEVICES:
        return JSONResponse({"error": "no peer"}, status_code=400)
    blob = await request.body()
    if len(blob) > HDR_LEN + MAX_POINTS * PT_LEN:
        return JSONResponse({"error": "too large"}, status_code=400)
    try:
        points = unpack_sketch(blob)
    except SketchError as exc:
        return JSONResponse({"error": str(exc)}, status_code=400)
    if not points:
        return JSONResponse({"error": "empty"}, status_code=400)

    to_id = device.peer
    seq = SEQ.get(to_id, 0) + 1
    SEQ[to_id] = seq
    sketch_id = str(seq)
    dest = DATA_DIR / to_id / f"{sketch_id}.flsk"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(blob)

    row = {
        "id": sketch_id,
        "from_id": device.id,
        "from_name": device.name,
        "to_id": to_id,
        "points": len(points),
        "ms": points[-1][0],
        "bytes": len(blob),
        "blob_path": dest,
        "read": False,
    }
    INBOX.setdefault(to_id, []).append(row)
    log_path = DATA_DIR / to_id / "inbox.jsonl"
    with log_path.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(public_row(row)) + "\n")

    peer = DEVICES[to_id]
    print(
        f"sketch {device.name} → {peer.name} id={sketch_id} "
        f"points={len(points)} ms={row['ms']}",
        flush=True,
    )
    out = public_row(row)
    out["ok"] = True
    out["to"] = to_id
    out["to_name"] = peer.name
    return out


@app.get("/v1/sketches", response_model=None)
def list_sketches(authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()
    unread = [public_row(row) for row in inbox_unread(device.id)]
    return {
        "device_id": device.id,
        "name": device.name,
        "unread": unread,
    }


@app.get("/v1/sketches/{sketch_id}/blob", response_model=None)
def get_blob(sketch_id: str, authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()
    for row in INBOX.get(device.id, []):
        if row["id"] == sketch_id:
            data = Path(row["blob_path"]).read_bytes()
            return Response(content=data, media_type="application/octet-stream")
    return JSONResponse({"error": "not found"}, status_code=404)


@app.post("/v1/sketches/{sketch_id}/read", response_model=None)
def mark_read(sketch_id: str, authorization: str | None = Header(default=None)):
    device = current_device(authorization)
    if device is None:
        return unauthorized()
    for row in INBOX.get(device.id, []):
        if row["id"] == sketch_id:
            row["read"] = True
            print(f"sketch read {device.name} id={sketch_id}", flush=True)
            return {"ok": True, "id": sketch_id, "unread": len(inbox_unread(device.id))}
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
