#!/usr/bin/env python3
"""Minimal family-message/1 Opus chunk lifecycle for h31 island demo."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import uuid
from pathlib import Path
from typing import Any

import uvicorn
from fastapi import FastAPI, Header, Request
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel, Field

from demos.server._shared.registry import load_devices, parse_bearer, resolve_device_for_token
from demos.server.h31_opus_messages.ogg_mux import (
    duration_ms_from_granule,
    mux_packets_to_ogg,
    packets_from_chunk_files,
)

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
DATA_DIR = ROOT / "data" / "h31_opus_messages"
INCOMING = DATA_DIR / "incoming"
COMPLETED = DATA_DIR / "completed"

MAX_CHUNK_BYTES = 196608
MAX_AUDIO_BYTES = 6 * 1024 * 1024
MAX_DURATION_MS = 180_000

DEVICES = load_devices()
app = FastAPI(title="h31 opus messages")

UUID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"
)


class AudioMeta(BaseModel):
    codec: str
    sample_rate_hz: int = 16000
    channels: int = 1
    target_chunk_ms: int = 2000


class CreateBody(BaseModel):
    protocol: str = "family-message/1"
    client_message_id: str | None = None
    to_user_id: str | None = None
    broadcast: bool = False
    audio: AudioMeta


class CompleteBody(BaseModel):
    audio_chunks: int = Field(..., ge=0)
    duration_ms: int = Field(..., ge=0)
    closed_reason: str = "button"
    sketch_sequences: list[int] = Field(default_factory=list)
    audio_sha256: str | None = None


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def current_device(authorization: str | None):
    token = parse_bearer(authorization)
    if not token:
        return None
    dev = resolve_device_for_token(token)
    if dev is None:
        return None
    return dev


def message_dir(message_id: str, *, completed: bool = False) -> Path:
    base = COMPLETED if completed else INCOMING
    return base / message_id


def meta_path(message_id: str) -> Path:
    return message_dir(message_id) / "meta.json"


def load_meta(message_id: str) -> dict[str, Any] | None:
    path = meta_path(message_id)
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def save_meta(message_id: str, meta: dict[str, Any]) -> None:
    d = message_dir(message_id)
    d.mkdir(parents=True, exist_ok=True)
    tmp = d / "meta.json.tmp"
    tmp.write_text(json.dumps(meta, indent=2), encoding="utf-8")
    tmp.replace(meta_path(message_id))


def chunk_path(message_id: str, seq: int) -> Path:
    return message_dir(message_id) / f"chunk_{seq}.bin"


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            block = fh.read(65536)
            if not block:
                break
            h.update(block)
    return h.hexdigest()


def limits_payload() -> dict[str, int]:
    return {
        "max_duration_ms": MAX_DURATION_MS,
        "max_audio_chunk_bytes": MAX_CHUNK_BYTES,
        "max_audio_bytes": MAX_AUDIO_BYTES,
        "max_sketch_chunk_bytes": 65536,
        "max_sketch_bytes": 524288,
    }


def _ffmpeg_available() -> bool:
    return shutil.which("ffmpeg") is not None


def _ffprobe_duration_ms(path: Path) -> int | None:
    ff = shutil.which("ffprobe")
    if not ff:
        return None
    try:
        out = subprocess.check_output(
            [
                ff,
                "-v",
                "quiet",
                "-show_entries",
                "format=duration",
                "-of",
                "default=noprint_wrappers=1:nokey=1",
                str(path),
            ],
            text=True,
        ).strip()
        return int(float(out) * 1000)
    except (subprocess.CalledProcessError, ValueError):
        return None


def build_index_json(ogg_path: Path, duration_ms: int) -> dict[str, Any]:
    """Map coarse seek points to byte offsets by scanning Ogg page headers."""
    points: list[dict[str, int]] = [{"time_ms": 0, "byte_offset": 0}]
    granule_at: dict[int, int] = {}
    with ogg_path.open("rb") as fh:
        off = 0
        data = fh.read()
    pos = 0
    while pos + 27 <= len(data):
        if data[pos : pos + 4] != b"OggS":
            pos += 1
            continue
        granule = int.from_bytes(data[pos + 6 : pos + 14], "little")
        seg_count = data[pos + 26]
        seg_table = data[pos + 27 : pos + 27 + seg_count]
        body_len = sum(seg_table)
        page_len = 27 + seg_count + body_len
        if granule > 0:
            ms = duration_ms_from_granule(granule)
            granule_at[ms] = pos
        pos += page_len
    for ms in sorted(granule_at.keys()):
        if ms == 0:
            continue
        points.append({"time_ms": ms, "byte_offset": granule_at[ms]})
    points.append({"time_ms": duration_ms, "byte_offset": max(0, len(data) - 1)})
    return {
        "media_type": "audio/ogg",
        "file": "media.ogg",
        "duration_ms": duration_ms,
        "seek_points": points,
    }


def finalize_message(message_id: str, meta: dict[str, Any], body: CompleteBody) -> tuple[int, dict]:
    chunks = meta.get("chunks") or {}
    need = body.audio_chunks
    missing = [s for s in range(need) if str(s) not in chunks]
    if missing:
        return 409, {"error": "missing_chunks", "missing": missing}

    paths = [chunk_path(message_id, s) for s in range(need)]
    for p in paths:
        if not p.is_file():
            return 409, {"error": "missing_chunks", "missing": [int(p.stem.split("_")[1])]}

    total_bytes = sum(int(chunks[str(s)]["bytes"]) for s in range(need))
    if total_bytes > MAX_AUDIO_BYTES:
        return 413, {"error": "audio too large"}

    out_dir = COMPLETED / message_id
    out_dir.mkdir(parents=True, exist_ok=True)
    ogg_path = out_dir / "media.ogg"

    frames, duration_mux_ms = mux_packets_to_ogg(
        packets_from_chunk_files(paths), ogg_path, sample_rate=meta["audio"]["sample_rate_hz"]
    )
    if body.duration_ms > 0:
        duration_ms = body.duration_ms
    elif frames > 0:
        duration_ms = duration_mux_ms
    else:
        duration_ms = need * meta["audio"].get("target_chunk_ms", 2000)

    ff_dur = _ffprobe_duration_ms(ogg_path)
    if ff_dur is not None:
        duration_ms = ff_dur

    media_sha = sha256_file(ogg_path)
    index = build_index_json(ogg_path, duration_ms)
    manifest = {
        "message_id": message_id,
        "state": "complete",
        "protocol": meta.get("protocol", "family-message/1"),
        "sender_id": meta.get("sender_id"),
        "sender_device": meta.get("sender_id"),
        "audio": meta.get("audio"),
        "duration_ms": duration_ms,
        "audio_chunks": need,
        "media_path": "media.ogg",
        "media_sha256": media_sha,
        "media_bytes": ogg_path.stat().st_size,
        "index": index,
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (out_dir / "index.json").write_text(json.dumps(index, indent=2), encoding="utf-8")

    meta["state"] = "complete"
    meta["completed_dir"] = str(out_dir)
    save_meta(message_id, meta)
    return 201, manifest


def parse_range_header(header: str | None, size: int) -> tuple[int, int] | None:
    if not header or not header.startswith("bytes="):
        return None
    spec = header[6:].strip()
    if "," in spec:
        return None
    if spec.startswith("-"):
        return None
    parts = spec.split("-", 1)
    try:
        start = int(parts[0]) if parts[0] else 0
        end = int(parts[1]) if len(parts) > 1 and parts[1] else size - 1
    except ValueError:
        return None
    if start < 0 or end >= size or start > end:
        return None
    return start, end


@app.post("/v1/messages", response_model=None)
async def post_message(
    body: CreateBody, authorization: str | None = Header(default=None)
):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    if body.protocol != "family-message/1":
        return JSONResponse({"error": "bad protocol"}, status_code=400)
    if body.audio.codec != "opus":
        return JSONResponse({"error": "codec must be opus"}, status_code=400)

    client_id = body.client_message_id
    if client_id:
        for mid, meta in _all_open_messages():
            if meta.get("client_message_id") == client_id and meta.get("sender_id") == sender.id:
                return JSONResponse(
                    {
                        "message_id": mid,
                        "client_message_id": client_id,
                        "state": meta.get("state", "open"),
                        "limits": limits_payload(),
                    },
                    status_code=200,
                )

    message_id = str(uuid.uuid4())
    meta = {
        "message_id": message_id,
        "client_message_id": client_id,
        "sender_id": sender.id,
        "state": "open",
        "protocol": body.protocol,
        "audio": body.audio.model_dump(),
        "chunks": {},
    }
    save_meta(message_id, meta)
    return JSONResponse(
        {
            "message_id": message_id,
            "client_message_id": client_id,
            "state": "open",
            "limits": limits_payload(),
        },
        status_code=201,
    )


def _all_open_messages():
    if not INCOMING.is_dir():
        return
    for child in INCOMING.iterdir():
        if not child.is_dir():
            continue
        mp = child / "meta.json"
        if mp.is_file():
            meta = json.loads(mp.read_text(encoding="utf-8"))
            if meta.get("state") != "complete":
                yield child.name, meta


@app.put("/v1/messages/{message_id}/audio/{sequence}", response_model=None)
async def put_audio_chunk(
    message_id: str,
    sequence: int,
    request: Request,
    authorization: str | None = Header(default=None),
    content_type: str | None = Header(default=None, alias="Content-Type"),
    x_chunk_sha256: str | None = Header(default=None, alias="X-Chunk-SHA256"),
    x_chunk_start_ms: int | None = Header(default=None, alias="X-Chunk-Start-Ms"),
    x_chunk_duration_ms: int | None = Header(default=None, alias="X-Chunk-Duration-Ms"),
    x_chunk_bytes: int | None = Header(default=None, alias="X-Chunk-Bytes"),
):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    if not UUID_RE.match(message_id):
        return JSONResponse({"error": "bad id"}, status_code=400)
    if sequence < 0:
        return JSONResponse({"error": "bad sequence"}, status_code=400)
    if content_type != "application/octet-stream":
        return JSONResponse({"error": "content type"}, status_code=400)
    if x_chunk_sha256 is None or x_chunk_duration_ms is None or x_chunk_duration_ms <= 0:
        return JSONResponse({"error": "headers"}, status_code=400)
    if x_chunk_start_ms is None or x_chunk_start_ms < 0:
        return JSONResponse({"error": "start ms"}, status_code=400)

    meta = load_meta(message_id)
    if meta is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    if meta.get("sender_id") != sender.id:
        return JSONResponse({"error": "forbidden"}, status_code=403)
    if meta.get("state") != "open":
        return JSONResponse({"error": "not open"}, status_code=409)

    declared = x_chunk_bytes
    tmp = message_dir(message_id) / f"chunk_{sequence}.bin.tmp"
    tmp.parent.mkdir(parents=True, exist_ok=True)
    hasher = hashlib.sha256()
    nbytes = 0
    with tmp.open("wb") as out:
        async for chunk in request.stream():
            if not chunk:
                continue
            nbytes += len(chunk)
            if nbytes > MAX_CHUNK_BYTES:
                tmp.unlink(missing_ok=True)
                return JSONResponse({"error": "too large"}, status_code=413)
            hasher.update(chunk)
            out.write(chunk)

    digest = hasher.hexdigest()
    if digest != x_chunk_sha256.lower():
        tmp.unlink(missing_ok=True)
        return JSONResponse({"error": "checksum"}, status_code=400)
    if declared is not None and declared != nbytes:
        tmp.unlink(missing_ok=True)
        return JSONResponse({"error": "byte count"}, status_code=400)

    key = str(sequence)
    existing = (meta.get("chunks") or {}).get(key)
    record = {
        "sha256": digest,
        "bytes": nbytes,
        "start_ms": x_chunk_start_ms,
        "duration_ms": x_chunk_duration_ms,
    }
    if existing:
        if existing == record:
            tmp.unlink(missing_ok=True)
            return JSONResponse(
                {
                    "message_id": message_id,
                    "sequence": sequence,
                    "sha256": digest,
                    "bytes": nbytes,
                    "durable": True,
                },
                status_code=200,
            )
        tmp.unlink(missing_ok=True)
        return JSONResponse({"error": "conflict"}, status_code=409)

    dest = chunk_path(message_id, sequence)
    tmp.replace(dest)
    chunks = meta.setdefault("chunks", {})
    chunks[key] = record
    meta["audio_bytes"] = sum(int(c["bytes"]) for c in chunks.values())
    save_meta(message_id, meta)
    return JSONResponse(
        {
            "message_id": message_id,
            "sequence": sequence,
            "sha256": digest,
            "bytes": nbytes,
            "durable": True,
        },
        status_code=201,
    )


@app.get("/v1/messages/{message_id}/upload", response_model=None)
def get_upload(message_id: str, authorization: str | None = Header(default=None)):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    meta = load_meta(message_id)
    if meta is None:
        comp = COMPLETED / message_id / "manifest.json"
        if comp.is_file():
            man = json.loads(comp.read_text(encoding="utf-8"))
            if man.get("sender_id") != sender.id:
                return JSONResponse({"error": "forbidden"}, status_code=403)
            return {
                "message_id": message_id,
                "state": "complete",
                "audio": {
                    "received": list(range(man.get("audio_chunks", 0))),
                    "bytes": man.get("media_bytes", 0),
                },
            }
        return JSONResponse({"error": "not found"}, status_code=404)
    if meta.get("sender_id") != sender.id:
        return JSONResponse({"error": "forbidden"}, status_code=403)
    chunks = meta.get("chunks") or {}
    received = sorted(int(k) for k in chunks.keys())
    return {
        "message_id": message_id,
        "state": meta.get("state", "open"),
        "audio": {
            "received": received,
            "bytes": meta.get("audio_bytes", 0),
        },
    }


@app.post("/v1/messages/{message_id}/complete", response_model=None)
async def post_complete(
    message_id: str,
    body: CompleteBody,
    authorization: str | None = Header(default=None),
):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    meta = load_meta(message_id)
    if meta is None:
        comp_manifest = COMPLETED / message_id / "manifest.json"
        if comp_manifest.is_file():
            return JSONResponse({"ok": True}, status_code=200)
        return JSONResponse({"error": "not found"}, status_code=404)
    if meta.get("sender_id") != sender.id:
        return JSONResponse({"error": "forbidden"}, status_code=403)
    if meta.get("state") == "complete":
        man = json.loads((COMPLETED / message_id / "manifest.json").read_text(encoding="utf-8"))
        return JSONResponse(man, status_code=200)

    status, payload = finalize_message(message_id, meta, body)
    return JSONResponse(payload, status_code=status)


@app.get("/v1/messages/{message_id}/index.json", response_model=None)
def get_index(message_id: str, authorization: str | None = Header(default=None)):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    path = COMPLETED / message_id / "index.json"
    if not path.is_file():
        return JSONResponse({"error": "not found"}, status_code=404)
    man_path = COMPLETED / message_id / "manifest.json"
    if man_path.is_file():
        man = json.loads(man_path.read_text(encoding="utf-8"))
        if man.get("sender_id") != sender.id:
            return JSONResponse({"error": "forbidden"}, status_code=403)
    return JSONResponse(json.loads(path.read_text(encoding="utf-8")))


@app.get("/v1/messages/{message_id}/audio", response_model=None)
def get_audio(
    message_id: str,
    request: Request,
    authorization: str | None = Header(default=None),
):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    ogg = COMPLETED / message_id / "media.ogg"
    man_path = COMPLETED / message_id / "manifest.json"
    if not ogg.is_file() or not man_path.is_file():
        return JSONResponse({"error": "not found"}, status_code=404)
    man = json.loads(man_path.read_text(encoding="utf-8"))
    if man.get("sender_id") != sender.id:
        return JSONResponse({"error": "forbidden"}, status_code=403)

    size = ogg.stat().st_size
    etag = f'"{man.get("media_sha256", "")[:16]}"'
    rng = parse_range_header(request.headers.get("range"), size)
    headers = {
        "Accept-Ranges": "bytes",
        "Content-Type": "audio/ogg",
        "ETag": etag,
        "Content-Length": str(size),
    }
    if rng:
        start, end = rng
        length = end - start + 1
        with ogg.open("rb") as fh:
            fh.seek(start)
            data = fh.read(length)
        headers["Content-Length"] = str(length)
        headers["Content-Range"] = f"bytes {start}-{end}/{size}"
        return Response(data, status_code=206, headers=headers, media_type="audio/ogg")
    with ogg.open("rb") as fh:
        data = fh.read()
    return Response(data, headers=headers, media_type="audio/ogg")


def reset_data() -> None:
    if DATA_DIR.is_dir():
        shutil.rmtree(DATA_DIR)
    INCOMING.mkdir(parents=True, exist_ok=True)
    COMPLETED.mkdir(parents=True, exist_ok=True)


def main() -> None:
    reset_data()
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    mux_note = "python ogg_mux"
    if _ffmpeg_available():
        mux_note += " (ffprobe validation when present)"
    print(f"h31 opus messages data={DATA_DIR} finalize={mux_note}")
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
