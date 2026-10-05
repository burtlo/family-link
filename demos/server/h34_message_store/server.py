#!/usr/bin/env python3
"""Canonical message_store host demo — family-message/1 PCM chunk path."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import uvicorn
from fastapi import FastAPI, Header, Request
from fastapi.responses import FileResponse, JSONResponse, Response
from pydantic import BaseModel, Field

from demos.server._shared.registry import load_devices, parse_bearer, resolve_device_for_token
from demos.server.h34_message_store.pcm_wav import finalize_pcm_chunks_to_wav

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
STORE = ROOT / "data" / "v1_product" / "message_store"
INCOMING = STORE / "incoming"
MESSAGES = STORE / "messages"
TRASH = STORE / "trash"
STATE = STORE / "state"
INBOXES = STATE / "inboxes.jsonl"
AUDIT = STATE / "admin-audit.jsonl"

MAX_CHUNK_BYTES = 196608
MAX_AUDIO_BYTES = 6 * 1024 * 1024
MAX_DURATION_MS = 180_000

app = FastAPI(title="h34 message store")
UUID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"
)

# message_id -> {"state", "path", "manifest"}
_INDEX: dict[str, dict[str, Any]] = {}
_INBOX_SEQ = 0
_CULL_PREVIEW: dict[str, dict[str, Any]] = {}


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
    source_audio_sha256: str = Field(..., min_length=64, max_length=64)


class CullPreviewBody(BaseModel):
    message_ids: list[str] = Field(default_factory=list)
    completed_before: str | None = None


class CullBody(BaseModel):
    selection_token: str


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def current_device(authorization: str | None):
    token = parse_bearer(authorization)
    if not token:
        return None
    return resolve_device_for_token(token)


def ensure_layout() -> None:
    for d in (INCOMING, MESSAGES, TRASH, STATE):
        d.mkdir(parents=True, exist_ok=True)
    if not INBOXES.is_file():
        INBOXES.write_text("", encoding="utf-8")


def incoming_dir(message_id: str) -> Path:
    return INCOMING / message_id


def manifest_path(message_id: str, *, completed_dir: Path | None = None) -> Path:
    if completed_dir is not None:
        return completed_dir / "manifest.json"
    return incoming_dir(message_id) / "manifest.json"


def chunk_path(message_id: str, seq: int) -> Path:
    return incoming_dir(message_id) / "audio" / f"{seq:06d}.chunk"


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


def sha256_concat_chunks(paths: list[Path]) -> str:
    h = hashlib.sha256()
    for path in paths:
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


def load_manifest_file(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def save_manifest(message_id: str, manifest: dict[str, Any], *, base: Path | None = None) -> None:
    d = base or incoming_dir(message_id)
    d.mkdir(parents=True, exist_ok=True)
    tmp = d / "manifest.json.tmp"
    tmp.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    tmp.replace(d / "manifest.json")


def completed_dir_for_manifest(manifest: dict[str, Any]) -> Path | None:
    mid = manifest.get("message_id")
    if not mid:
        return None
    entry = _INDEX.get(mid)
    if entry and entry.get("state") == "complete":
        return Path(entry["path"])
    return None


def message_record(message_id: str) -> dict[str, Any] | None:
    if message_id in _INDEX:
        return _INDEX[message_id]
    return None


def open_manifest(message_id: str) -> dict[str, Any] | None:
    path = manifest_path(message_id)
    if path.is_file():
        return load_manifest_file(path)
    return None


def can_access_message(dev_id: str, manifest: dict[str, Any]) -> bool:
    if manifest.get("from_user") == dev_id:
        return True
    recipients = manifest.get("recipients") or []
    if dev_id in recipients:
        return True
    if manifest.get("broadcast"):
        return True
    return False


def append_inbox_line(entry: dict[str, Any]) -> None:
    global _INBOX_SEQ
    _INBOX_SEQ += 1
    entry = dict(entry)
    entry["seq"] = _INBOX_SEQ
    with INBOXES.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(entry, separators=(",", ":")) + "\n")


def rebuild_index() -> None:
    global _INBOX_SEQ
    _INDEX.clear()
    _INBOX_SEQ = 0
    if INBOXES.is_file():
        for line in INBOXES.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            _INBOX_SEQ = max(_INBOX_SEQ, int(row.get("seq", 0)))

    for complete in MESSAGES.rglob("complete"):
        msg_dir = complete.parent
        man_path = msg_dir / "manifest.json"
        if not man_path.is_file():
            continue
        manifest = load_manifest_file(man_path)
        if manifest is None:
            continue
        mid = manifest.get("message_id") or msg_dir.name
        _INDEX[mid] = {
            "state": "complete",
            "path": msg_dir,
            "manifest": manifest,
        }

    if INCOMING.is_dir():
        for child in INCOMING.iterdir():
            if not child.is_dir():
                continue
            man = child / "manifest.json"
            if not man.is_file():
                continue
            manifest = load_manifest_file(man)
            if manifest is None:
                continue
            state = manifest.get("state", "open")
            if state == "complete":
                continue
            mid = manifest.get("message_id") or child.name
            _INDEX[mid] = {"state": state, "path": child, "manifest": manifest}


def parse_range_header(header: str | None, size: int) -> tuple[int, int] | None:
    if not header or not header.startswith("bytes="):
        return None
    spec = header[6:].strip()
    if "," in spec or spec.startswith("-"):
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


def file_range_response(path: Path, start: int, end: int) -> Response:
    length = end - start + 1
    size = path.stat().st_size
    with path.open("rb") as fh:
        fh.seek(start)
        data = fh.read(length)
    headers = {
        "Content-Length": str(length),
        "Content-Range": f"bytes {start}-{end}/{size}",
        "Accept-Ranges": "bytes",
    }
    return Response(data, status_code=206, headers=headers, media_type="audio/wav")


@app.on_event("startup")
def on_startup() -> None:
    ensure_layout()
    rebuild_index()


def _create_conflict(existing: dict[str, Any], body: CreateBody) -> bool:
    audio = existing.get("audio") or {}
    new_audio = body.audio.model_dump()
    for key in ("codec", "sample_rate_hz", "channels"):
        if audio.get(key) != new_audio.get(key):
            return True
    if existing.get("broadcast") != body.broadcast:
        return True
    if body.broadcast:
        return False
    return existing.get("recipients") != ([body.to_user_id] if body.to_user_id else [])


@app.post("/v1/messages", response_model=None)
async def post_message(body: CreateBody, authorization: str | None = Header(default=None)):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    if body.protocol != "family-message/1":
        return JSONResponse({"error": "bad protocol"}, status_code=400)
    if body.audio.codec != "pcm_s16le":
        return JSONResponse({"error": "codec must be pcm_s16le"}, status_code=400)
    if not body.broadcast and not body.to_user_id:
        return JSONResponse({"error": "recipient required"}, status_code=400)

    client_id = body.client_message_id
    if client_id:
        for mid, rec in _INDEX.items():
            man = rec.get("manifest") or {}
            if man.get("client_message_id") == client_id and man.get("from_user") == sender.id:
                if _create_conflict(man, body):
                    return JSONResponse({"error": "client_message_id conflict"}, status_code=409)
                return JSONResponse(
                    {
                        "message_id": mid,
                        "client_message_id": client_id,
                        "state": rec.get("state", "open"),
                        "limits": limits_payload(),
                    },
                    status_code=200,
                )

    message_id = str(uuid.uuid4())
    recipients = [] if body.broadcast else [body.to_user_id]
    manifest = {
        "schema": "family-message-manifest/1",
        "message_id": message_id,
        "client_message_id": client_id,
        "state": "open",
        "from_user": sender.id,
        "recipients": recipients,
        "broadcast": body.broadcast,
        "created_at": utc_now(),
        "protocol": body.protocol,
        "audio": {
            **body.audio.model_dump(),
            "container": "raw_chunks",
        },
        "chunks": {},
    }
    incoming_dir(message_id).mkdir(parents=True, exist_ok=True)
    (incoming_dir(message_id) / "audio").mkdir(exist_ok=True)
    (incoming_dir(message_id) / "work").mkdir(exist_ok=True)
    save_manifest(message_id, manifest)
    _INDEX[message_id] = {"state": "open", "path": incoming_dir(message_id), "manifest": manifest}
    return JSONResponse(
        {
            "message_id": message_id,
            "client_message_id": client_id,
            "state": "open",
            "limits": limits_payload(),
        },
        status_code=201,
    )


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

    rec = message_record(message_id)
    if rec is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    manifest = rec.get("manifest") or open_manifest(message_id)
    if manifest is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    if manifest.get("from_user") != sender.id:
        return JSONResponse({"error": "forbidden"}, status_code=403)
    if manifest.get("state") != "open":
        return JSONResponse({"error": "not open"}, status_code=409)

    work = incoming_dir(message_id) / "work" / f"{uuid.uuid4().hex}.part"
    work.parent.mkdir(parents=True, exist_ok=True)
    hasher = hashlib.sha256()
    nbytes = 0
    with work.open("wb") as out:
        async for chunk in request.stream():
            if not chunk:
                continue
            nbytes += len(chunk)
            if nbytes > MAX_CHUNK_BYTES:
                work.unlink(missing_ok=True)
                return JSONResponse({"error": "too large"}, status_code=413)
            hasher.update(chunk)
            out.write(chunk)

    digest = hasher.hexdigest()
    if digest != x_chunk_sha256.lower():
        work.unlink(missing_ok=True)
        return JSONResponse({"error": "checksum"}, status_code=400)
    if x_chunk_bytes is not None and x_chunk_bytes != nbytes:
        work.unlink(missing_ok=True)
        return JSONResponse({"error": "byte count"}, status_code=400)
    if nbytes % 2:
        work.unlink(missing_ok=True)
        return JSONResponse({"error": "pcm alignment"}, status_code=400)

    key = str(sequence)
    chunks = manifest.setdefault("chunks", {})
    existing = chunks.get(key)
    record = {
        "sha256": digest,
        "bytes": nbytes,
        "start_ms": x_chunk_start_ms,
        "duration_ms": x_chunk_duration_ms,
    }
    if existing:
        if existing == record:
            work.unlink(missing_ok=True)
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
        work.unlink(missing_ok=True)
        return JSONResponse({"error": "conflict"}, status_code=409)

    dest = chunk_path(message_id, sequence)
    dest.parent.mkdir(parents=True, exist_ok=True)
    work.replace(dest)
    chunks[key] = record
    manifest["audio_bytes"] = sum(int(c["bytes"]) for c in chunks.values())
    save_manifest(message_id, manifest)
    rec["manifest"] = manifest
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
    rec = message_record(message_id)
    if rec is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    manifest = rec.get("manifest") or {}
    if manifest.get("from_user") != sender.id:
        return JSONResponse({"error": "forbidden"}, status_code=403)
    if rec.get("state") == "complete":
        n_chunks = int(manifest.get("audio_chunks", 0))
        bytes_total = int(manifest.get("audio", {}).get("bytes", 0))
        return {
            "message_id": message_id,
            "state": "complete",
            "audio": {"received": list(range(n_chunks)), "bytes": bytes_total},
        }
    chunks = manifest.get("chunks") or {}
    received = sorted(int(k) for k in chunks.keys())
    return {
        "message_id": message_id,
        "state": manifest.get("state", "open"),
        "audio": {"received": received, "bytes": manifest.get("audio_bytes", 0)},
    }


def finalize_message(message_id: str, manifest: dict[str, Any], body: CompleteBody) -> tuple[int, dict]:
    need = body.audio_chunks
    chunks = manifest.get("chunks") or {}
    missing = [s for s in range(need) if str(s) not in chunks]
    if missing:
        return 409, {"error": "missing_chunks", "missing": missing}

    paths = [chunk_path(message_id, s) for s in range(need)]
    for p in paths:
        if not p.is_file():
            return 409, {"error": "missing_chunks", "missing": [int(p.stem)]}

    total_bytes = sum(int(chunks[str(s)]["bytes"]) for s in range(need))
    if total_bytes > MAX_AUDIO_BYTES:
        return 413, {"error": "audio too large"}

    aggregate = sha256_concat_chunks(paths)
    if aggregate != body.source_audio_sha256.lower():
        return 422, {"error": "source_audio_sha256 mismatch"}

    inc = incoming_dir(message_id)
    wav_path = inc / "media.wav"
    sample_rate = int(manifest.get("audio", {}).get("sample_rate_hz", 16000))
    channels = int(manifest.get("audio", {}).get("channels", 1))
    try:
        pcm_bytes = finalize_pcm_chunks_to_wav(
            paths, wav_path, sample_rate=sample_rate, channels=channels
        )
    except ValueError as exc:
        return 422, {"error": str(exc)}

    media_sha = sha256_file(wav_path)
    media_bytes = wav_path.stat().st_size
    completed_at = utc_now()
    created = manifest.get("created_at") or completed_at
    try:
        dt = datetime.fromisoformat(created.replace("Z", "+00:00"))
    except ValueError:
        dt = datetime.now(timezone.utc)
    year, month = f"{dt.year:04d}", f"{dt.month:02d}"

    completed_manifest = {
        "schema": "family-message-manifest/1",
        "message_id": message_id,
        "client_message_id": manifest.get("client_message_id"),
        "state": "complete",
        "from_user": manifest.get("from_user"),
        "recipients": manifest.get("recipients") or [],
        "broadcast": manifest.get("broadcast", False),
        "created_at": created,
        "completed_at": completed_at,
        "duration_ms": body.duration_ms,
        "audio": {
            "codec": "pcm_s16le",
            "container": "wav",
            "path": "media.wav",
            "bytes": media_bytes,
            "sha256": media_sha,
            "sample_rate_hz": sample_rate,
            "channels": channels,
            "pcm_data_bytes": pcm_bytes,
        },
        "closed_reason": body.closed_reason,
        "protocol": manifest.get("protocol", "family-message/1"),
        "audio_chunks": need,
        "source_audio_sha256": aggregate,
    }
    save_manifest(message_id, completed_manifest, base=inc)
    (inc / "complete").write_bytes(b"")

    dest_parent = MESSAGES / year / month
    dest_parent.mkdir(parents=True, exist_ok=True)
    dest = dest_parent / message_id
    if dest.exists():
        return 409, {"error": "already committed"}
    inc.rename(dest)

    for chunk_file in (dest / "audio").glob("*.chunk"):
        chunk_file.unlink()
    audio_dir = dest / "audio"
    if audio_dir.is_dir() and not any(audio_dir.iterdir()):
        audio_dir.rmdir()
    work_dir = dest / "work"
    if work_dir.is_dir():
        for f in work_dir.iterdir():
            f.unlink(missing_ok=True)
        work_dir.rmdir()

    _INDEX[message_id] = {
        "state": "complete",
        "path": dest,
        "manifest": completed_manifest,
    }

    recipients = completed_manifest.get("recipients") or []
    if completed_manifest.get("broadcast"):
        devices = load_devices()
        recipients = [d.id for d in devices.values() if d.id != completed_manifest.get("from_user")]
    for user_id in recipients:
        append_inbox_line(
            {
                "message_id": message_id,
                "to_user": user_id,
                "from_user": completed_manifest.get("from_user"),
                "duration_ms": body.duration_ms,
                "audio_codec": "pcm_s16le",
                "read": False,
                "created_at": completed_at,
            }
        )
    return 201, completed_manifest


@app.post("/v1/messages/{message_id}/complete", response_model=None)
async def post_complete(
    message_id: str,
    body: CompleteBody,
    authorization: str | None = Header(default=None),
):
    sender = current_device(authorization)
    if sender is None:
        return unauthorized()
    rec = message_record(message_id)
    if rec is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    if rec.get("state") == "complete":
        man = rec.get("manifest") or {}
        return JSONResponse(man, status_code=200)
    manifest = rec.get("manifest") or open_manifest(message_id)
    if manifest is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    if manifest.get("from_user") != sender.id:
        return JSONResponse({"error": "forbidden"}, status_code=403)
    if manifest.get("state") == "finalizing":
        return JSONResponse({"error": "busy"}, status_code=409)
    manifest["state"] = "finalizing"
    save_manifest(message_id, manifest)
    status, payload = finalize_message(message_id, manifest, body)
    if status != 201:
        manifest["state"] = "open"
        save_manifest(message_id, manifest)
        rec["manifest"] = manifest
        rec["state"] = "open"
    return JSONResponse(payload, status_code=status)


@app.get("/v1/inbox", response_model=None)
def get_inbox(authorization: str | None = Header(default=None)):
    dev = current_device(authorization)
    if dev is None:
        return unauthorized()
    rows: list[dict[str, Any]] = []
    if INBOXES.is_file():
        for line in INBOXES.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get("to_user") != dev.id:
                continue
            mid = row.get("message_id")
            rec = message_record(mid)
            if rec is None or rec.get("state") != "complete":
                continue
            rows.append(row)
    rows.sort(key=lambda r: int(r.get("seq", 0)))
    return {"inbox": rows}


@app.get("/v1/messages/{message_id}/audio", response_model=None)
def get_audio(
    message_id: str,
    request: Request,
    authorization: str | None = Header(default=None),
):
    dev = current_device(authorization)
    if dev is None:
        return unauthorized()
    rec = message_record(message_id)
    if rec is None or rec.get("state") != "complete":
        return JSONResponse({"error": "not found"}, status_code=404)
    manifest = rec.get("manifest") or {}
    if not can_access_message(dev.id, manifest):
        return JSONResponse({"error": "forbidden"}, status_code=403)
    msg_dir = Path(rec["path"])
    if not (msg_dir / "complete").is_file():
        return JSONResponse({"error": "not complete"}, status_code=404)
    wav = msg_dir / "media.wav"
    if not wav.is_file():
        return JSONResponse({"error": "not found"}, status_code=404)

    size = wav.stat().st_size
    etag = f'"{manifest.get("audio", {}).get("sha256", "")[:32]}"'
    rng = parse_range_header(request.headers.get("range"), size)
    if rng:
        start, end = rng
        resp = file_range_response(wav, start, end)
        resp.headers["ETag"] = etag
        resp.headers["Content-Type"] = "audio/wav"
        return resp
    return FileResponse(
        wav,
        media_type="audio/wav",
        headers={
            "Accept-Ranges": "bytes",
            "Content-Length": str(size),
            "ETag": etag,
        },
    )


@app.post("/v1/admin/messages/cull/preview", response_model=None)
async def admin_cull_preview(
    body: CullPreviewBody,
    authorization: str | None = Header(default=None),
):
    dev = current_device(authorization)
    if dev is None:
        return unauthorized()
    ids = body.message_ids
    total_bytes = 0
    details: list[dict[str, Any]] = []
    for mid in ids:
        rec = message_record(mid)
        if rec is None or rec.get("state") != "complete":
            continue
        man = rec.get("manifest") or {}
        b = int(man.get("audio", {}).get("bytes", 0))
        total_bytes += b
        details.append({"message_id": mid, "bytes": b})
    token = str(uuid.uuid4())
    _CULL_PREVIEW[token] = {"message_ids": ids, "bytes": total_bytes}
    with AUDIT.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps({"action": "cull_preview", "token": token, "ids": ids}) + "\n")
    return {"selection_token": token, "count": len(details), "bytes": total_bytes, "messages": details}


@app.post("/v1/admin/messages/cull", response_model=None)
async def admin_cull(
    body: CullBody,
    authorization: str | None = Header(default=None),
):
    dev = current_device(authorization)
    if dev is None:
        return unauthorized()
    sel = _CULL_PREVIEW.pop(body.selection_token, None)
    if sel is None:
        return JSONResponse({"error": "invalid token"}, status_code=400)
    moved: list[str] = []
    for mid in sel["message_ids"]:
        rec = message_record(mid)
        if rec is None or rec.get("state") != "complete":
            continue
        msg_dir = Path(rec["path"])
        man = rec.get("manifest") or {}
        completed = man.get("completed_at") or utc_now()
        try:
            dt = datetime.fromisoformat(completed.replace("Z", "+00:00"))
        except ValueError:
            dt = datetime.now(timezone.utc)
        dest_parent = TRASH / f"{dt.year:04d}" / f"{dt.month:02d}"
        dest_parent.mkdir(parents=True, exist_ok=True)
        dest = dest_parent / mid
        if dest.exists():
            continue
        msg_dir.rename(dest)
        deletion = {
            "message_id": mid,
            "original_path": str(msg_dir),
            "trashed_at": utc_now(),
            "actor": dev.id,
        }
        (dest / "deletion.json").write_text(json.dumps(deletion, indent=2), encoding="utf-8")
        _INDEX[mid] = {"state": "trashed", "path": dest, "manifest": man}
        moved.append(mid)
    return {"trashed": moved}


@app.post("/v1/admin/messages/{message_id}/restore", response_model=None)
async def admin_restore(message_id: str, authorization: str | None = Header(default=None)):
    dev = current_device(authorization)
    if dev is None:
        return unauthorized()
    rec = message_record(message_id)
    if rec is None or rec.get("state") != "trashed":
        return JSONResponse({"error": "not in trash"}, status_code=404)
    trash_dir = Path(rec["path"])
    man = load_manifest_file(trash_dir / "manifest.json") or {}
    completed = man.get("completed_at") or utc_now()
    try:
        dt = datetime.fromisoformat(completed.replace("Z", "+00:00"))
    except ValueError:
        dt = datetime.now(timezone.utc)
    dest_parent = MESSAGES / f"{dt.year:04d}" / f"{dt.month:02d}"
    dest_parent.mkdir(parents=True, exist_ok=True)
    dest = dest_parent / message_id
    trash_dir.rename(dest)
    (dest / "deletion.json").unlink(missing_ok=True)
    _INDEX[message_id] = {"state": "complete", "path": dest, "manifest": man}
    return {"restored": message_id, "media_sha256": man.get("audio", {}).get("sha256")}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    ensure_layout()
    rebuild_index()
    print(f"h34 message_store root={STORE} indexed={len(_INDEX)} (data preserved on restart)")
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
