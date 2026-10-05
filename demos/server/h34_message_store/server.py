#!/usr/bin/env python3
"""Recoverable host-only family-message/1 PCM message store.

This is a single-process demo. Mutation serialization is in-process; multiple
Uvicorn workers sharing the same root are unsupported. Directory fsync support
is reported per acknowledgement and must be qualified on the host filesystem.
"""

from __future__ import annotations

import argparse
import errno
import hashlib
import json
import os
import re
import shutil
import stat
import threading
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterator

import uvicorn
from fastapi import FastAPI, Header, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse, StreamingResponse
from pydantic import BaseModel, ConfigDict, Field, ValidationError

from demos.server._shared.registry import (
    device_for_token,
    load_devices as shared_load_devices,
    parse_bearer,
    resolve_device_for_token,
)
from demos.server.h34_message_store import durable
from demos.server.h34_message_store.pcm_wav import finalize_pcm_chunks_to_wav

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
STORE = ROOT / "data" / "v1_product" / "message_store"
INCOMING = STORE / "incoming"
MESSAGES = STORE / "messages"
TRASH = STORE / "trash"
QUARANTINE = STORE / "quarantine"
STATE = STORE / "state"
CULL_INTENTS = STATE / "cull-intents"
INBOXES = STATE / "inboxes.jsonl"
AUDIT = STATE / "admin-audit.jsonl"
RECOVERY = STATE / "recovery.jsonl"

MAX_CHUNK_BYTES = 192 * 1024
MAX_AUDIO_BYTES = 6 * 1024 * 1024
MAX_DURATION_MS = 180_000
MAX_CHUNK_DURATION_MS = 5000
MAX_CHUNKS = 9000
MAX_OPEN_PER_SENDER = 32
MAX_ACTIVE_UPLOADS_PER_SENDER = 2
DEFAULT_FREE_DISK_FLOOR_BYTES = 64 * 1024 * 1024
DISK_METADATA_ALLOWANCE_BYTES = 64 * 1024
PCM_BYTES_PER_MS = 32  # 16 kHz, mono, signed 16-bit
PREVIEW_TTL_SECONDS = 60
SHA_RE = re.compile(r"^[0-9a-f]{64}$")
UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
_RANGE_RE = re.compile(r"^bytes=(\d*)-(\d*)$")

app = FastAPI(title="h34 message store")
_INDEX: dict[str, dict[str, Any]] = {}
_INBOX_ROWS: list[dict[str, Any]] = []
_INBOX_SEQ = 0
_CULL_PREVIEW: dict[str, dict[str, Any]] = {}
_ACTIVE_UPLOADS: set[tuple[str, str, int]] = set()
_DISK_RESERVATIONS: dict[tuple[str, str, int], int] = {}
_LOCK = threading.RLock()


class StorageUnavailable(Exception):
    """A retryable disk-floor or ENOSPC/EDQUOT interruption."""


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)


class AudioMeta(StrictModel):
    codec: str
    sample_rate_hz: int = 16000
    channels: int = 1
    target_chunk_ms: int = 2000


class CreateBody(StrictModel):
    protocol: str = "family-message/1"
    client_message_id: str | None = None
    to_user_id: str | None = None
    broadcast: bool = False
    audio: AudioMeta


class CompleteBody(StrictModel):
    audio_chunks: int = Field(..., ge=1, le=MAX_CHUNKS)
    duration_ms: int = Field(..., ge=1, le=MAX_DURATION_MS)
    closed_reason: str = "button"
    sketch_sequences: list[int] = Field(default_factory=list)
    source_audio_sha256: str = Field(..., min_length=64, max_length=64)


class CullPreviewBody(StrictModel):
    message_ids: list[str] = Field(default_factory=list)
    completed_before: str | None = None


class CullBody(StrictModel):
    selection_token: str


@app.exception_handler(RequestValidationError)
async def validation_error(_request: Request, _exc: RequestValidationError) -> JSONResponse:
    return err(400, "malformed request")


def err(status: int, message: str, **extra: Any) -> JSONResponse:
    return JSONResponse({"error": message, **extra}, status_code=status)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z")


def parse_timestamp(value: str) -> datetime | None:
    if not isinstance(value, str):
        return None
    try:
        dt = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (TypeError, ValueError):
        return None
    return dt.astimezone(timezone.utc) if dt.tzinfo else None


def registry_devices():
    override = os.environ.get("FAMILY_LINK_DEVICES_REGISTRY")
    return shared_load_devices(Path(override)) if override else shared_load_devices()


def current_device(authorization: str | None):
    token = parse_bearer(authorization)
    if not token:
        return None
    if os.environ.get("FAMILY_LINK_DEVICES_REGISTRY"):
        return device_for_token(token, registry_devices())
    return resolve_device_for_token(token)


def admin_auth(authorization: str | None):
    dev = current_device(authorization)
    if dev is None:
        return None, err(401, "unauthorized")
    if dev.role != "admin":
        return None, err(403, "forbidden")
    return dev, None


def ensure_layout() -> None:
    free_disk_floor_bytes()
    for d in (INCOMING, MESSAGES, TRASH, QUARANTINE, STATE, CULL_INTENTS):
        durable.mkdir_durable(d)
    if not INBOXES.exists():
        durable.atomic_bytes(INBOXES, b"")


def incoming_dir(mid: str) -> Path:
    return INCOMING / mid


def chunk_path(mid: str, seq: int, *, base: Path | None = None) -> Path:
    return (base or incoming_dir(mid)) / "audio" / f"{seq:06d}.chunk"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for block in iter(lambda: fh.read(65536), b""):
            h.update(block)
    return h.hexdigest()


def sha256_concat_chunks(paths: list[Path]) -> str:
    h = hashlib.sha256()
    for path in paths:
        with path.open("rb") as fh:
            for block in iter(lambda: fh.read(65536), b""):
                h.update(block)
    return h.hexdigest()


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return None
    return data if isinstance(data, dict) else None


def save_manifest(mid: str, manifest: dict[str, Any], *, base: Path | None = None,
                  boundary_prefix: str | None = None) -> None:
    durable.atomic_json((base or incoming_dir(mid)) / "manifest.json", manifest,
                        before_replace=f"{boundary_prefix}_temp_fsync" if boundary_prefix else None,
                        after_replace=f"{boundary_prefix}_replace" if boundary_prefix else None)


def limits_payload() -> dict[str, int]:
    return {
        "max_duration_ms": MAX_DURATION_MS,
        "max_audio_chunk_bytes": MAX_CHUNK_BYTES,
        "max_audio_bytes": MAX_AUDIO_BYTES,
        "max_sketch_chunk_bytes": 65536,
        "max_sketch_bytes": 524288,
    }


def durability_payload() -> dict[str, Any]:
    return {"durable": durable.DIRECTORY_FSYNC_SUPPORTED,
            "durability": "file_and_directory_fsync" if durable.DIRECTORY_FSYNC_SUPPORTED else "process_crash_only"}


def free_disk_floor_bytes() -> int:
    raw = os.environ.get("FAMILY_LINK_H34_FREE_DISK_FLOOR_BYTES")
    if raw is None:
        return DEFAULT_FREE_DISK_FLOOR_BYTES
    if not re.fullmatch(r"\d+", raw):
        raise ValueError("FAMILY_LINK_H34_FREE_DISK_FLOOR_BYTES must be a nonnegative integer")
    return int(raw)


def has_disk_capacity(additional_bytes: int) -> bool:
    """Call under _LOCK before a durable write; reserves headroom for metadata."""
    free = shutil.disk_usage(STORE).free
    reserved = sum(_DISK_RESERVATIONS.values())
    return free - reserved - additional_bytes - DISK_METADATA_ALLOWANCE_BYTES >= free_disk_floor_bytes()


def message_record(mid: str) -> dict[str, Any] | None:
    return _INDEX.get(mid) if UUID_RE.fullmatch(mid) else None


def can_access_message(dev, mid: str) -> bool:
    return dev.role == "admin" or any(
        row.get("message_id") == mid and row.get("to_user") == dev.id
        for row in _INBOX_ROWS
    )


def completed_path(man: dict[str, Any]) -> Path:
    dt = parse_timestamp(man.get("created_at", "")) or datetime.now(timezone.utc)
    return MESSAGES / f"{dt.year:04d}" / f"{dt.month:02d}" / man["message_id"]


def trash_path(man: dict[str, Any]) -> Path:
    dt = parse_timestamp(man.get("completed_at", "")) or datetime.now(timezone.utc)
    return TRASH / f"{dt.year:04d}" / f"{dt.month:02d}" / man["message_id"]


def validated_complete(path: Path) -> dict[str, Any] | None:
    if not (path / "complete").is_file() or (path / "complete").is_symlink() or path.is_symlink() or not UUID_RE.fullmatch(path.name):
        return None
    man = load_json(path / "manifest.json")
    if not man or man.get("message_id") != path.name or man.get("state") != "complete":
        return None
    audio = man.get("audio")
    if not isinstance(audio, dict):
        return None
    if parse_timestamp(man.get("created_at")) is None or parse_timestamp(man.get("completed_at")) is None:
        return None
    if not isinstance(man.get("from_user"), str) or not man["from_user"]:
        return None
    recipients = man.get("delivery_recipients", man.get("recipients"))
    if not isinstance(recipients, list) or not all(isinstance(user, str) for user in recipients):
        return None
    if audio.get("codec") != "pcm_s16le" or audio.get("path") != "media.wav":
        return None
    media = path / "media.wav"
    if media.is_symlink() or not media.is_file():
        return None
    size = audio.get("bytes")
    digest = audio.get("sha256")
    if type(size) is not int or not isinstance(digest, str) or not SHA_RE.fullmatch(digest):
        return None
    try:
        if size != media.stat().st_size or sha256_file(media) != digest:
            return None
    except OSError:
        return None
    return man


def quarantine(path: Path, reason: str) -> None:
    durable.append_jsonl(RECOVERY, {"path": str(path.relative_to(STORE)), "reason": reason, "at": utc_now()})
    dest = QUARANTINE / f"{path.name}-{uuid.uuid4().hex}"
    durable.move_dir(path, dest)


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    data = path.read_bytes()
    rows: list[dict[str, Any]] = []
    offset = 0
    for line in data.splitlines(keepends=True):
        try:
            item = json.loads(line)
            if not isinstance(item, dict):
                raise ValueError("JSONL entry is not an object")
        except (json.JSONDecodeError, UnicodeDecodeError, ValueError) as exc:
            if offset + len(line) == len(data) and not line.endswith(b"\n"):
                durable.atomic_bytes(path, data[:offset])
                durable.append_jsonl(RECOVERY, {"path": str(path.relative_to(STORE)),
                                                "reason": f"torn trailing JSONL row removed: {exc}",
                                                "at": utc_now()})
                break
            durable.append_jsonl(RECOVERY, {"path": str(path.relative_to(STORE)),
                                            "reason": f"corrupt committed JSONL row: {exc}",
                                            "at": utc_now()})
            raise ValueError(f"corrupt committed JSONL row in {path}") from exc
        rows.append(item)
        offset += len(line)
    return rows


def _write_inboxes(rows: list[dict[str, Any]] | None = None) -> None:
    source = _INBOX_ROWS if rows is None else rows
    data = b"".join((json.dumps(row, separators=(",", ":"), sort_keys=True) + "\n").encode() for row in source)
    durable.atomic_bytes(INBOXES, data, before_replace="inbox_temp_fsync",
                         after_replace="inbox_replace")


def _repair_inboxes() -> None:
    global _INBOX_SEQ
    seen: set[tuple[str, str]] = set()
    kept: list[dict[str, Any]] = []
    for row in _INBOX_ROWS:
        key = (str(row.get("message_id")), str(row.get("to_user")))
        if key in seen:
            continue
        seen.add(key)
        kept.append(row)
    if len(kept) != len(_INBOX_ROWS):
        _write_inboxes(kept)
        _INBOX_ROWS[:] = kept
    for mid, rec in sorted(_INDEX.items()):
        if rec["state"] != "complete":
            continue
        man = rec["manifest"]
        recipients = man.get("delivery_recipients") or man.get("recipients") or []
        for user_id in recipients:
            key = (mid, user_id)
            if key in seen:
                continue
            next_seq = _INBOX_SEQ + 1
            row = {"seq": next_seq, "message_id": mid, "to_user": user_id,
                   "from_user": man.get("from_user"), "duration_ms": man.get("duration_ms"),
                   "audio_codec": "pcm_s16le", "read": False,
                   "created_at": man.get("completed_at")}
            candidate = [*_INBOX_ROWS, row]
            _write_inboxes(candidate)
            _INBOX_ROWS[:] = candidate
            _INBOX_SEQ = next_seq
            seen.add(key)
            durable.crash_after("inbox_append")


def _audit_once(event_id: str, row: dict[str, Any]) -> None:
    if any(item.get("event_id") == event_id for item in _read_jsonl(AUDIT)):
        return
    durable.append_jsonl(AUDIT, {"event_id": event_id, **row})


def _cleanup_chunks(path: Path) -> None:
    try:
        for folder in (path / "audio", path / "work"):
            if not folder.is_dir():
                continue
            for child in folder.iterdir():
                if child.is_file():
                    durable.durable_unlink(child)
            if not any(folder.iterdir()):
                folder.rmdir()
                durable.sync_dir(path)
    except OSError as exc:
        if exc.errno in (errno.ENOSPC, errno.EDQUOT):
            return  # Cleanup is optional after canonical publication.
        raise
    durable.crash_after("cleanup")


def _completion_tuple(body: CompleteBody | dict[str, Any]) -> dict[str, Any]:
    data = body.model_dump() if isinstance(body, CompleteBody) else body
    return {"audio_chunks": data["audio_chunks"], "duration_ms": data["duration_ms"],
            "closed_reason": data["closed_reason"], "sketch_sequences": data["sketch_sequences"],
            "source_audio_sha256": data["source_audio_sha256"]}


def _completed_tuple(man: dict[str, Any]) -> dict[str, Any]:
    return {"audio_chunks": man.get("audio_chunks"), "duration_ms": man.get("duration_ms"),
            "closed_reason": man.get("closed_reason"), "sketch_sequences": man.get("sketch_sequences", []),
            "source_audio_sha256": man.get("source_audio_sha256")}


def _validate_completion(mid: str, man: dict[str, Any], body: CompleteBody) -> tuple[int, dict[str, Any]] | None:
    if not SHA_RE.fullmatch(body.source_audio_sha256):
        return 400, {"error": "bad source_audio_sha256"}
    if body.closed_reason not in ("button", "silence", "duration_limit", "recovered"):
        return 400, {"error": "bad closed_reason"}
    if body.sketch_sequences:
        return 400, {"error": "sketch unsupported by PCM demo"}
    chunks = man.get("chunks") or {}
    missing = [s for s in range(body.audio_chunks) if str(s) not in chunks]
    if missing:
        return 409, {"error": "missing_chunks", "missing": missing}
    if len(chunks) != body.audio_chunks:
        return 409, {"error": "chunk count conflict"}
    total_bytes = 0
    start = 0
    paths = []
    for seq in range(body.audio_chunks):
        row = chunks[str(seq)]
        duration = row.get("duration_ms")
        nbytes = row.get("bytes")
        if row.get("start_ms") != start or not isinstance(duration, int) or duration <= 0:
            return 422, {"error": "chunk timeline"}
        if not isinstance(nbytes, int) or nbytes != duration * PCM_BYTES_PER_MS or nbytes > MAX_CHUNK_BYTES:
            return 422, {"error": "chunk size/duration"}
        path = chunk_path(mid, seq)
        if not path.is_file():
            return 409, {"error": "missing_chunks", "missing": [seq]}
        if path.stat().st_size != nbytes or sha256_file(path) != row.get("sha256"):
            return 422, {"error": "chunk checksum"}
        start += duration
        total_bytes += nbytes
        paths.append(path)
    if total_bytes > MAX_AUDIO_BYTES or start > MAX_DURATION_MS:
        return 413, {"error": "audio too large"}
    if start != body.duration_ms or total_bytes != body.duration_ms * PCM_BYTES_PER_MS:
        return 422, {"error": "duration mismatch"}
    if sha256_concat_chunks(paths) != body.source_audio_sha256:
        return 422, {"error": "source_audio_sha256 mismatch"}
    return None


def _finalize_from_intent(mid: str, intent: dict[str, Any]) -> dict[str, Any]:
    inc = incoming_dir(mid)
    source = intent["open_manifest"]
    body = CompleteBody.model_validate(intent["completion"])
    invalid = _validate_completion(mid, source, body)
    if invalid:
        raise ValueError(f"completion source invalid: {invalid[1]}")
    paths = [chunk_path(mid, seq) for seq in range(body.audio_chunks)]
    wav = inc / "media.wav"
    wav_tmp = inc / "media.wav.tmp"
    # An interrupted pre-marker attempt may leave either file. Source chunks
    # remain authoritative, so release that space before reserving a new WAV.
    for leftover in (wav_tmp, wav):
        if leftover.exists():
            durable.durable_unlink(leftover)
    projected_wav_bytes = body.duration_ms * PCM_BYTES_PER_MS + 44
    if not has_disk_capacity(projected_wav_bytes):
        raise StorageUnavailable("insufficient safe free disk for canonical WAV")
    try:
        pcm_bytes = finalize_pcm_chunks_to_wav(
            paths, wav_tmp, sample_rate=16000, channels=1,
            on_first_block=lambda: durable.crash_after("wav_temp_write"))
        durable.crash_after("wav_fsync")
        os.replace(wav_tmp, wav)
        durable.sync_dir(inc)
        durable.crash_after("wav_rename")
    except OSError as exc:
        if exc.errno in (errno.ENOSPC, errno.EDQUOT):
            raise StorageUnavailable("disk filled while materializing canonical WAV") from exc
        raise
    media_sha = sha256_file(wav)
    recipients = intent["delivery_recipients"]
    man = {
        "schema": "family-message-manifest/1", "message_id": mid,
        "client_message_id": source.get("client_message_id"), "state": "complete",
        "from_user": source["from_user"], "recipients": source["recipients"],
        "delivery_recipients": recipients, "broadcast": source["broadcast"],
        "created_at": source["created_at"], "completed_at": intent["completed_at"],
        "duration_ms": body.duration_ms,
        "audio": {"codec": "pcm_s16le", "container": "wav", "path": "media.wav",
                  "bytes": wav.stat().st_size, "sha256": media_sha,
                  "sample_rate_hz": 16000, "channels": 1, "pcm_data_bytes": pcm_bytes},
        "closed_reason": body.closed_reason, "protocol": "family-message/1",
        "audio_chunks": body.audio_chunks, "sketch_sequences": [],
        "source_audio_sha256": body.source_audio_sha256,
    }
    try:
        save_manifest(mid, man, base=inc, boundary_prefix="complete_manifest")
        durable.crash_after("complete_manifest")
        durable.atomic_bytes(inc / "complete", b"", boundary="marker",
                             before_replace="marker_temp_fsync", after_replace="marker_replace")
        dest = completed_path(man)
        if dest.exists():
            raise FileExistsError(dest)
        durable.move_dir(inc, dest, boundary="message_rename",
                         after_parent_create="message_dest_parent",
                         after_rename="message_rename_raw",
                         after_source_sync="message_source_parent_fsync")
    except OSError as exc:
        if exc.errno in (errno.ENOSPC, errno.EDQUOT):
            raise StorageUnavailable("disk filled while publishing canonical message") from exc
        raise
    _INDEX[mid] = {"state": "complete", "path": dest, "manifest": man}
    _repair_inboxes()
    _cleanup_chunks(dest)
    return man


def _adopt_completed_candidate(mid: str, intent: dict[str, Any]) -> bool:
    source = intent.get("open_manifest")
    if not isinstance(source, dict) or source.get("message_id") != mid:
        return False
    dest = completed_path(source)
    man = validated_complete(dest)
    if man is None or man.get("message_id") != mid:
        return False
    _INDEX[mid] = {"state": "complete", "path": dest, "manifest": man}
    return True


def _recover_incoming(path: Path) -> None:
    mid = path.name
    if not UUID_RE.fullmatch(mid) or path.is_symlink():
        quarantine(path, "invalid incoming directory name")
        return
    intent = load_json(path / "completion-intent.json")
    man = load_json(path / "manifest.json")
    if intent is None and (path / "complete").is_file():
        complete_man = validated_complete(path)
        if complete_man is None:
            quarantine(path, "invalid legacy complete candidate")
            return
        dest = completed_path(complete_man)
        if dest.exists():
            quarantine(path, "duplicate committed directory")
            return
        durable.move_dir(path, dest)
        _INDEX[mid] = {"state": "complete", "path": dest, "manifest": complete_man}
        _cleanup_chunks(dest)
        return
    if intent is not None:
        if intent.get("message_id") != mid or not isinstance(intent.get("open_manifest"), dict):
            quarantine(path, "malformed completion intent")
            return
        if (path / "complete").exists():
            complete_man = validated_complete(path)
            if complete_man:
                dest = completed_path(complete_man)
                if dest.exists():
                    quarantine(path, "duplicate committed directory")
                    return
                try:
                    durable.move_dir(path, dest)
                except OSError as exc:
                    if exc.errno in (errno.ENOSPC, errno.EDQUOT):
                        if _adopt_completed_candidate(mid, intent):
                            return
                        _INDEX[mid] = {"state": "finalizing", "path": path,
                                       "manifest": intent["open_manifest"]}
                        return
                    raise
                _INDEX[mid] = {"state": "complete", "path": dest, "manifest": complete_man}
                _cleanup_chunks(dest)
                return
            quarantine(path, "invalid complete candidate")
            return
        try:
            _finalize_from_intent(mid, intent)
        except StorageUnavailable as exc:
            if _adopt_completed_candidate(mid, intent):
                return
            _INDEX[mid] = {"state": "finalizing", "path": path,
                           "manifest": intent["open_manifest"]}
            try:
                durable.append_jsonl(RECOVERY, {"path": str(path.relative_to(STORE)),
                                                "reason": f"completion waiting for disk: {exc}",
                                                "at": utc_now()})
            except OSError:
                pass  # No diagnostic space must not hide a retryable upload.
        except OSError as exc:
            if exc.errno in (errno.ENOSPC, errno.EDQUOT):
                if _adopt_completed_candidate(mid, intent):
                    return
                _INDEX[mid] = {"state": "finalizing", "path": path,
                               "manifest": intent["open_manifest"]}
                return
            quarantine(path, f"unrecoverable completion: {exc}")
        except (ValueError, ValidationError, KeyError) as exc:
            quarantine(path, f"unrecoverable completion: {exc}")
        return
    if (path / "completion-intent.json").exists():
        quarantine(path, "invalid completion intent")
        return
    if man and man.get("message_id") == mid and man.get("state") in ("open", "finalizing"):
        # Legacy finalizing state without an intent has not committed a completion.
        if man["state"] == "finalizing":
            man["state"] = "open"
            save_manifest(mid, man)
        _INDEX[mid] = {"state": "open", "path": path, "manifest": man}
    else:
        quarantine(path, "invalid open manifest")


def _scan_dirs(base: Path) -> Iterator[Path]:
    if not base.is_dir():
        return
    for year in sorted(base.iterdir()):
        if year.is_symlink() or not year.is_dir() or not re.fullmatch(r"\d{4}", year.name):
            continue
        for month in sorted(year.iterdir()):
            if month.is_symlink() or not month.is_dir() or not re.fullmatch(r"\d{2}", month.name):
                continue
            for item in sorted(month.iterdir()):
                if item.is_dir():
                    yield item


def _valid_deletion(path: Path, man: dict[str, Any]) -> dict[str, Any] | None:
    deletion = load_json(path / "deletion.json")
    if not deletion or deletion.get("message_id") != man["message_id"]:
        return None
    original = deletion.get("original_path")
    expected = str(completed_path(man).relative_to(STORE))
    # Earlier demo revisions stored the same path as an absolute string and
    # omitted an event ID. Migrate only when it matches this exact message.
    legacy = original == str(completed_path(man)) and "event_id" not in deletion
    if original != expected and not legacy:
        return None
    if not isinstance(deletion.get("actor"), str) or not deletion["actor"]:
        return None
    if parse_timestamp(deletion.get("trashed_at")) is None:
        return None
    if legacy:
        deletion["original_path"] = expected
        deletion["event_id"] = f"legacy:{man['message_id']}:{hashlib.sha256(json.dumps(deletion, sort_keys=True).encode()).hexdigest()[:16]}"
        deletion["restoring"] = False
        durable.atomic_json(path / "deletion.json", deletion)
    if not isinstance(deletion.get("event_id"), str) or not deletion["event_id"]:
        return None
    if type(deletion.get("restoring", False)) is not bool:
        return None
    if deletion.get("restoring"):
        if "restored_at" not in deletion:
            deletion["restored_at"] = utc_now()  # legacy interrupted restore
            durable.atomic_json(path / "deletion.json", deletion)
        elif parse_timestamp(deletion.get("restored_at")) is None:
            return None
    return deletion


def _finish_restore(path: Path, man: dict[str, Any], deletion: dict[str, Any]) -> None:
    dest = completed_path(man)
    if path != dest:
        if dest.exists():
            raise FileExistsError(dest)
        durable.move_dir(path, dest)
    _audit_once(f"restore:{man['message_id']}:{deletion['event_id']}",
                {"action": "restore", "message_id": man["message_id"], "actor": deletion["actor"],
                 "at": deletion.get("restored_at") or utc_now()})
    durable.durable_unlink(dest / "deletion.json")
    _INDEX[man["message_id"]] = {"state": "complete", "path": dest, "manifest": man}


def _apply_cull_batch(batch: dict[str, Any]) -> list[str]:
    moved: list[str] = []
    batch_id = batch["batch_id"]
    for detail in batch["details"]:
        mid = detail["message_id"]
        event_id = f"{batch_id}:{mid}"
        rec = message_record(mid)
        if rec is None:
            raise ValueError(f"missing selected message {mid}")
        man = rec["manifest"]
        path = Path(rec["path"])
        if rec["state"] == "trashed":
            deletion = _valid_deletion(path, man)
            if deletion is None or deletion["event_id"] != event_id:
                raise ValueError(f"conflicting trash state for {mid}")
            _audit_once(event_id, {"action": "cull", "message_id": mid,
                                   "actor": batch["actor"], "at": deletion["trashed_at"]})
            moved.append(mid)
            continue
        if rec["state"] != "complete" or str(path.relative_to(STORE)) != detail["path"] or sha256_file(path / "manifest.json") != detail["manifest_sha256"]:
            raise ValueError(f"selected manifest changed for {mid}")
        refs = sorted(row["to_user"] for row in _INBOX_ROWS if row.get("message_id") == mid)
        media = path / "media.wav"
        if (media.is_symlink() or not media.is_file() or media.stat().st_size != detail["bytes"]
                or sha256_file(media) != detail["media_sha256"]
                or man["audio"]["sha256"] != detail["media_sha256"]
                or refs != detail["inbox_users"]):
            raise ValueError(f"selected references or media changed for {mid}")
        dest = trash_path(man)
        if dest.exists():
            raise ValueError(f"trash destination exists for {mid}")
        deletion = load_json(path / "deletion.json")
        if deletion is None:
            deletion = {"message_id": mid, "original_path": str(path.relative_to(STORE)),
                        "trashed_at": utc_now(), "actor": batch["actor"],
                        "selector": batch["selector"], "event_id": event_id,
                        "restoring": False}
            durable.atomic_json(path / "deletion.json", deletion, boundary="cull_deletion",
                                before_replace="cull_deletion_temp_fsync",
                                after_replace="cull_deletion_replace")
        elif deletion.get("event_id") != event_id:
            raise ValueError(f"conflicting deletion intent for {mid}")
        durable.move_dir(path, dest, boundary="cull_rename",
                         after_parent_create="cull_dest_parent",
                         after_rename="cull_rename_raw",
                         after_source_sync="cull_source_parent_fsync")
        _INDEX[mid] = {"state": "trashed", "path": dest, "manifest": man}
        _audit_once(event_id, {"action": "cull", "message_id": mid,
                               "actor": batch["actor"], "at": deletion["trashed_at"]})
        durable.crash_after("cull_audit")
        moved.append(mid)
    durable.durable_unlink(CULL_INTENTS / f"{batch_id}.json")
    return moved


def _resume_cull_journals() -> None:
    for path in sorted(CULL_INTENTS.glob("*.json")):
        batch = load_json(path)
        if not batch or not UUID_RE.fullmatch(str(batch.get("batch_id", ""))) or not isinstance(batch.get("details"), list):
            durable.append_jsonl(RECOVERY, {"path": str(path.relative_to(STORE)),
                                            "reason": "invalid cull intent", "at": utc_now()})
            continue
        try:
            _apply_cull_batch(batch)
        except (OSError, ValueError, KeyError) as exc:
            durable.append_jsonl(RECOVERY, {"path": str(path.relative_to(STORE)),
                                            "reason": f"cull recovery stalled: {exc}", "at": utc_now()})


def _reconstruct_orphan_trash(path: Path, man: dict[str, Any]) -> bool:
    """Recover a rename-before-deletion crash only from an exact durable batch."""
    if (path / "deletion.json").exists() or path != trash_path(man):
        return False
    mid = man["message_id"]
    manifest_hash = sha256_file(path / "manifest.json")
    for journal in sorted(CULL_INTENTS.glob("*.json")):
        batch = load_json(journal)
        if not batch or not UUID_RE.fullmatch(str(batch.get("batch_id", ""))):
            continue
        if not isinstance(batch.get("actor"), str) or not batch["actor"]:
            continue
        if parse_timestamp(batch.get("created_at")) is None or not isinstance(batch.get("details"), list):
            continue
        for detail in batch["details"]:
            if not isinstance(detail, dict) or detail.get("message_id") != mid:
                continue
            if (detail.get("path") != str(completed_path(man).relative_to(STORE))
                    or detail.get("manifest_sha256") != manifest_hash
                    or detail.get("media_sha256") != man["audio"]["sha256"]
                    or detail.get("bytes") != man["audio"]["bytes"]):
                continue
            deletion = {"message_id": mid,
                        "original_path": str(completed_path(man).relative_to(STORE)),
                        "trashed_at": batch["created_at"], "actor": batch["actor"],
                        "selector": batch.get("selector"),
                        "event_id": f"{batch['batch_id']}:{mid}", "restoring": False}
            durable.atomic_json(path / "deletion.json", deletion)
            durable.append_jsonl(RECOVERY, {"path": str(path.relative_to(STORE)),
                                            "reason": "reconstructed deletion from cull intent",
                                            "at": utc_now()})
            return True
    return False


def rebuild_index() -> None:
    global _INBOX_SEQ
    with _LOCK:
        ensure_layout()
        _INDEX.clear()
        _INBOX_ROWS[:] = _read_jsonl(INBOXES)
        _INBOX_SEQ = max((int(row.get("seq", 0)) for row in _INBOX_ROWS), default=0)
        for path in _scan_dirs(MESSAGES):
            man = validated_complete(path)
            if man is None:
                quarantine(path, "invalid completed message")
                continue
            if path != completed_path(man):
                quarantine(path, "completed message in wrong month directory")
                continue
            deletion = load_json(path / "deletion.json")
            if deletion:
                deletion = _valid_deletion(path, man)
                if deletion is None:
                    quarantine(path, "invalid deletion metadata")
                    continue
                if deletion.get("restoring"):
                    _finish_restore(path, man, deletion)
                else:
                    dest = trash_path(man)
                    if dest.exists():
                        quarantine(path, "duplicate trash destination")
                        continue
                    durable.move_dir(path, dest)
                    _INDEX[man["message_id"]] = {"state": "trashed", "path": dest, "manifest": man}
                continue
            mid = man["message_id"]
            if mid in _INDEX:
                quarantine(path, "duplicate message id")
                continue
            _INDEX[mid] = {"state": "complete", "path": path, "manifest": man}
            _cleanup_chunks(path)
        for path in _scan_dirs(TRASH):
            man = validated_complete(path)
            if man is None:
                quarantine(path, "invalid trashed message")
                continue
            if path != trash_path(man):
                quarantine(path, "trashed message in wrong month directory")
                continue
            if not (path / "deletion.json").exists() and not _reconstruct_orphan_trash(path, man):
                quarantine(path, "orphan trash without matching cull intent")
                continue
            deletion = _valid_deletion(path, man)
            if deletion is None:
                quarantine(path, "invalid deletion metadata")
                continue
            mid = man["message_id"]
            if deletion.get("restoring"):
                _finish_restore(path, man, deletion)
            elif mid in _INDEX and Path(_INDEX[mid]["path"]) == path and _INDEX[mid]["state"] == "trashed":
                _audit_once(deletion["event_id"], {"action": "cull", "message_id": mid,
                                                    "actor": deletion["actor"], "at": deletion["trashed_at"]})
            elif mid in _INDEX:
                quarantine(path, "duplicate message id")
            else:
                _INDEX[mid] = {"state": "trashed", "path": path, "manifest": man}
                _audit_once(deletion["event_id"], {"action": "cull", "message_id": mid,
                                                    "actor": deletion["actor"], "at": deletion["trashed_at"]})
        for path in sorted(INCOMING.iterdir()):
            if path.is_dir():
                _recover_incoming(path)
        _resume_cull_journals()
        try:
            _repair_inboxes()
        except OSError as exc:
            if exc.errno not in (errno.ENOSPC, errno.EDQUOT):
                raise
            # Keep serving canonical state; sender completion retry repairs
            # missing references once storage has headroom again.
            _INBOX_ROWS[:] = _read_jsonl(INBOXES)
            _INBOX_SEQ = max((int(row.get("seq", 0)) for row in _INBOX_ROWS), default=0)


@app.on_event("startup")
def on_startup() -> None:
    rebuild_index()


def _create_conflict(existing: dict[str, Any], body: CreateBody) -> bool:
    audio = existing.get("audio") or {}
    return (any(audio.get(key) != getattr(body.audio, key) for key in
                ("codec", "sample_rate_hz", "channels", "target_chunk_ms"))
            or existing.get("broadcast") != body.broadcast
            or existing.get("recipients") != ([] if body.broadcast else [body.to_user_id]))


@app.post("/v1/messages", response_model=None)
async def post_message(body: CreateBody, authorization: str | None = Header(default=None)):
    sender = current_device(authorization)
    if sender is None:
        return err(401, "unauthorized")
    if body.protocol != "family-message/1" or body.audio.codec != "pcm_s16le":
        return err(400, "unsupported protocol or codec")
    if body.audio.sample_rate_hz != 16000 or body.audio.channels != 1:
        return err(400, "PCM must be 16 kHz mono")
    if not (1 <= body.audio.target_chunk_ms <= MAX_CHUNK_DURATION_MS):
        return err(400, "bad target_chunk_ms")
    if body.broadcast == bool(body.to_user_id):
        return err(400, "select exactly one of broadcast or to_user_id")
    if body.client_message_id is not None and (not body.client_message_id or len(body.client_message_id) > 128):
        return err(400, "bad client_message_id")
    devices = registry_devices()
    if body.to_user_id and (body.to_user_id not in devices or body.to_user_id == sender.id):
        return err(400, "bad recipient")
    with _LOCK:
        if body.client_message_id:
            for mid, rec in _INDEX.items():
                man = rec["manifest"]
                if man.get("client_message_id") == body.client_message_id and man.get("from_user") == sender.id:
                    if _create_conflict(man, body):
                        return err(409, "client_message_id conflict")
                    return JSONResponse({"message_id": mid, "client_message_id": body.client_message_id,
                                         "state": rec["state"], "limits": limits_payload()}, status_code=200)
        open_count = sum(rec["state"] == "open" and rec["manifest"].get("from_user") == sender.id
                         for rec in _INDEX.values())
        if open_count >= MAX_OPEN_PER_SENDER:
            return err(409, "too many open messages")
        mid = str(uuid.uuid4())
        man = {"schema": "family-message-manifest/1", "message_id": mid,
               "client_message_id": body.client_message_id, "state": "open",
               "from_user": sender.id, "recipients": [] if body.broadcast else [body.to_user_id],
               "broadcast": body.broadcast, "created_at": utc_now(), "protocol": body.protocol,
               "audio": {**body.audio.model_dump(), "container": "raw_chunks"},
               "chunks": {}, "audio_bytes": 0}
        base = incoming_dir(mid)
        durable.mkdir_durable(base / "audio")
        durable.mkdir_durable(base / "work")
        save_manifest(mid, man)
        _INDEX[mid] = {"state": "open", "path": base, "manifest": man}
    return JSONResponse({"message_id": mid, "client_message_id": body.client_message_id,
                         "state": "open", "limits": limits_payload()}, status_code=201)


@app.put("/v1/messages/{message_id}/audio/{sequence}", response_model=None)
async def put_audio_chunk(
    message_id: str, sequence: int, request: Request,
    authorization: str | None = Header(default=None),
    content_type: str | None = Header(default=None, alias="Content-Type"),
    x_chunk_sha256: str | None = Header(default=None, alias="X-Chunk-SHA256"),
    x_chunk_start_ms: int | None = Header(default=None, alias="X-Chunk-Start-Ms"),
    x_chunk_duration_ms: int | None = Header(default=None, alias="X-Chunk-Duration-Ms"),
    x_chunk_bytes: int | None = Header(default=None, alias="X-Chunk-Bytes"),
):
    sender = current_device(authorization)
    if sender is None:
        return err(401, "unauthorized")
    if not UUID_RE.fullmatch(message_id) or not (0 <= sequence < MAX_CHUNKS):
        return err(400, "bad id or sequence")
    if content_type != "application/octet-stream":
        return err(400, "content type")
    if x_chunk_sha256 is None or not SHA_RE.fullmatch(x_chunk_sha256):
        return err(400, "bad X-Chunk-SHA256")
    if x_chunk_start_ms is None or x_chunk_duration_ms is None or x_chunk_bytes is None:
        return err(400, "missing chunk headers")
    if x_chunk_start_ms < 0 or x_chunk_duration_ms <= 0 or x_chunk_bytes < 0:
        return err(400, "bad chunk timing or bytes")
    if x_chunk_duration_ms > MAX_CHUNK_DURATION_MS or x_chunk_start_ms + x_chunk_duration_ms > MAX_DURATION_MS:
        return err(413, "duration limit")
    if x_chunk_bytes > MAX_CHUNK_BYTES:
        return err(413, "chunk too large")
    if x_chunk_bytes != x_chunk_duration_ms * PCM_BYTES_PER_MS:
        return err(422, "PCM size/duration mismatch")
    with _LOCK:
        rec = message_record(message_id)
        if rec is None:
            return err(404, "not found")
        man = rec["manifest"]
        if man.get("from_user") != sender.id:
            return err(403, "forbidden")
        if rec["state"] != "open":
            return err(409, "not open")
        others = man.get("chunks") or {}
        if sequence == 0 and x_chunk_start_ms != 0:
            return err(422, "first chunk must start at zero")
        prev = others.get(str(sequence - 1))
        nxt = others.get(str(sequence + 1))
        if prev and prev["start_ms"] + prev["duration_ms"] != x_chunk_start_ms:
            return err(422, "chunk timeline conflict")
        if nxt and x_chunk_start_ms + x_chunk_duration_ms != nxt["start_ms"]:
            return err(422, "chunk timeline conflict")
        if sum(c["bytes"] for key, c in others.items() if key != str(sequence)) + x_chunk_bytes > MAX_AUDIO_BYTES:
            return err(413, "audio too large")
        active_for_sender = sum(key[0] == sender.id for key in _ACTIVE_UPLOADS)
        reservation = (sender.id, message_id, sequence)
        # Hold one reservation per message across the request stream. This
        # keeps timeline and aggregate checks valid until manifest commit.
        if any(key[1] == message_id for key in _ACTIVE_UPLOADS) or active_for_sender >= MAX_ACTIVE_UPLOADS_PER_SENDER:
            return err(409, "concurrent upload limit")
        if not has_disk_capacity(x_chunk_bytes):
            return err(507, "insufficient safe free disk")
        _ACTIVE_UPLOADS.add(reservation)
        _DISK_RESERVATIONS[reservation] = x_chunk_bytes
    work = incoming_dir(message_id) / "work" / f"{uuid.uuid4().hex}.part"
    try:
        hasher = hashlib.sha256()
        nbytes = 0
        too_large = False
        with work.open("xb") as out:
            async for block in request.stream():
                if not block:
                    continue
                nbytes += len(block)
                if nbytes > x_chunk_bytes or nbytes > MAX_CHUNK_BYTES:
                    too_large = True
                    break
                hasher.update(block)
                out.write(block)
            if not too_large:
                durable.crash_after("chunk_temp_write")
                out.flush()
                os.fsync(out.fileno())
        if too_large:
            return err(413, "chunk too large")
        durable.crash_after("chunk_temp_fsync")
        if nbytes != x_chunk_bytes or hasher.hexdigest() != x_chunk_sha256:
            return err(400, "chunk byte count or checksum")
        row = {"sha256": x_chunk_sha256, "bytes": nbytes,
               "start_ms": x_chunk_start_ms, "duration_ms": x_chunk_duration_ms}
        with _LOCK:
            rec = message_record(message_id)
            if rec is None or rec["state"] != "open":
                return err(409, "not open")
            man = rec["manifest"]
            existing = (man.get("chunks") or {}).get(str(sequence))
            if existing is not None:
                if existing != row:
                    return err(409, "conflict")
                dest = chunk_path(message_id, sequence)
                if not dest.is_file() or dest.stat().st_size != nbytes or sha256_file(dest) != x_chunk_sha256:
                    return err(409, "stored chunk invalid")
                return JSONResponse({"message_id": message_id, "sequence": sequence,
                                     "sha256": x_chunk_sha256, "bytes": nbytes,
                                     **durability_payload()}, status_code=200)
            dest = chunk_path(message_id, sequence)
            os.replace(work, dest)
            durable.crash_after("chunk_rename_raw")
            durable.sync_dir(dest.parent)
            durable.crash_after("chunk_audio_dir_fsync")
            durable.sync_dir(work.parent)
            durable.crash_after("chunk_work_dir_fsync")
            durable.crash_after("chunk_rename")
            new_man = dict(man)
            new_chunks = dict(man.get("chunks") or {})
            new_chunks[str(sequence)] = row
            new_man["chunks"] = new_chunks
            new_man["audio_bytes"] = sum(c["bytes"] for c in new_chunks.values())
            save_manifest(message_id, new_man, boundary_prefix="chunk_manifest")
            durable.crash_after("chunk_manifest")
            rec["manifest"] = new_man
            durable.crash_after("chunk_pre_response")
            return JSONResponse({"message_id": message_id, "sequence": sequence,
                                 "sha256": x_chunk_sha256, "bytes": nbytes,
                                 **durability_payload()}, status_code=201)
    finally:
        work.unlink(missing_ok=True)
        with _LOCK:
            _ACTIVE_UPLOADS.discard(reservation)
            _DISK_RESERVATIONS.pop(reservation, None)


@app.get("/v1/messages/{message_id}/upload", response_model=None)
def get_upload(message_id: str, authorization: str | None = Header(default=None)):
    sender = current_device(authorization)
    if sender is None:
        return err(401, "unauthorized")
    rec = message_record(message_id)
    if rec is None:
        return err(404, "not found")
    man = rec["manifest"]
    if man.get("from_user") != sender.id:
        return err(403, "forbidden")
    if rec["state"] == "complete":
        return {"message_id": message_id, "state": "complete",
                "audio": {"received": list(range(man["audio_chunks"])),
                          "bytes": man["audio"]["pcm_data_bytes"]}}
    chunks = man.get("chunks") or {}
    return {"message_id": message_id, "state": rec["state"],
            "audio": {"received": sorted(int(k) for k in chunks),
                      "bytes": man.get("audio_bytes", 0)}}


@app.post("/v1/messages/{message_id}/complete", response_model=None)
async def post_complete(message_id: str, body: CompleteBody,
                        authorization: str | None = Header(default=None)):
    sender = current_device(authorization)
    if sender is None:
        return err(401, "unauthorized")
    if not UUID_RE.fullmatch(message_id):
        return err(400, "bad id")
    with _LOCK:
        rec = message_record(message_id)
        if rec is None or rec["state"] == "trashed":
            return err(404, "not found")
        man = rec["manifest"]
        if man.get("from_user") != sender.id:
            return err(403, "forbidden")
        requested = _completion_tuple(body)
        if rec["state"] == "complete":
            if _completed_tuple(man) != requested:
                return err(409, "completion tuple conflict")
            try:
                _repair_inboxes()
            except OSError as exc:
                if exc.errno in (errno.ENOSPC, errno.EDQUOT):
                    return err(507, "insufficient safe free disk for inbox publication")
                raise
            return JSONResponse(man, status_code=200)
        if rec["state"] == "finalizing":
            intent = load_json(incoming_dir(message_id) / "completion-intent.json")
            if intent is None or intent.get("completion") != requested:
                return err(409, "completion tuple conflict")
            _recover_incoming(incoming_dir(message_id))
            recovered = message_record(message_id)
            if recovered and recovered["state"] == "complete":
                try:
                    _repair_inboxes()
                except OSError as exc:
                    if exc.errno in (errno.ENOSPC, errno.EDQUOT):
                        return err(507, "insufficient safe free disk for inbox publication")
                    raise
                return JSONResponse(recovered["manifest"], status_code=200)
            if recovered and recovered["state"] == "finalizing":
                return err(507, "insufficient safe free disk; completion remains retryable")
            return err(500, "finalization recovery failed")
        if rec["state"] != "open":
            return err(409, "not open")
        if any(key[1] == message_id for key in _ACTIVE_UPLOADS):
            return err(409, "upload in progress")
        invalid = _validate_completion(message_id, man, body)
        if invalid:
            return JSONResponse(invalid[1], status_code=invalid[0])
        if not has_disk_capacity(body.duration_ms * PCM_BYTES_PER_MS + 44):
            return err(507, "insufficient safe free disk")
        if man["broadcast"]:
            recipients = sorted(dev.id for dev in registry_devices().values()
                                if dev.id != sender.id and dev.role != "admin")
        else:
            recipients = list(man["recipients"])
        intent = {"message_id": message_id, "open_manifest": man,
                  "completion": requested, "delivery_recipients": recipients,
                  "completed_at": utc_now()}
        try:
            durable.atomic_json(incoming_dir(message_id) / "completion-intent.json", intent,
                                boundary="intent", before_replace="intent_temp_fsync",
                                after_replace="intent_replace")
        except OSError as exc:
            if exc.errno not in (errno.ENOSPC, errno.EDQUOT):
                raise
            if (incoming_dir(message_id) / "completion-intent.json").is_file():
                rec["state"] = "finalizing"
            return err(507, "insufficient safe free disk; chunks retained")
        rec["state"] = "finalizing"
        try:
            complete_man = _finalize_from_intent(message_id, intent)
        except StorageUnavailable:
            _adopt_completed_candidate(message_id, intent)
            return err(507, "insufficient safe free disk; completion remains retryable")
        except OSError as exc:
            if exc.errno in (errno.ENOSPC, errno.EDQUOT):
                _adopt_completed_candidate(message_id, intent)
                return err(507, "insufficient safe free disk; completion remains retryable")
            return err(500, "finalization interrupted", detail=str(exc))
        except (ValueError, KeyError) as exc:
            # The intent remains durable. Startup or a repeat request retries it.
            return err(500, "finalization interrupted", detail=str(exc))
        return JSONResponse(complete_man, status_code=201)


@app.get("/v1/inbox", response_model=None)
def get_inbox(authorization: str | None = Header(default=None)):
    dev = current_device(authorization)
    if dev is None:
        return err(401, "unauthorized")
    with _LOCK:
        rows = [dict(row) for row in _INBOX_ROWS
                if row.get("to_user") == dev.id
                and (rec := message_record(str(row.get("message_id")))) is not None
                and rec["state"] == "complete"]
    rows.sort(key=lambda row: int(row.get("seq", 0)))
    return {"inbox": rows}


def parse_range_header(header: str | None, size: int) -> tuple[str, int, int]:
    if header is None:
        return "full", 0, size - 1
    match = _RANGE_RE.fullmatch(header.strip())
    if match is None or (not match[1] and not match[2]):
        return "malformed", 0, 0
    if len(match[1]) > 20 or len(match[2]) > 20:
        return "malformed", 0, 0
    if not match[1]:
        suffix = int(match[2])
        if suffix == 0 or size == 0:
            return "unsatisfiable", 0, 0
        start, end = max(0, size - suffix), size - 1
    else:
        start = int(match[1])
        if start >= size:
            return "unsatisfiable", 0, 0
        end = min(int(match[2]), size - 1) if match[2] else size - 1
        if end < start:
            return "unsatisfiable", 0, 0
    # A WAV range may include all or part of the 44-byte header. Any audio
    # bytes included must start and end on complete 16-bit sample boundaries.
    if start >= 44 and (start - 44) % 2:
        return "malformed", 0, 0
    if end >= 44 and (end - 43) % 2:
        return "malformed", 0, 0
    return "range", start, end


def _bounded_file(fh, length: int) -> Iterator[bytes]:
    try:
        remaining = length
        while remaining:
            block = fh.read(min(65536, remaining))
            if not block:
                break
            remaining -= len(block)
            yield block
    finally:
        fh.close()


def _verified_open_media(fh, expected_sha256: str, expected_size: int) -> bool:
    before = os.fstat(fh.fileno())
    if not stat.S_ISREG(before.st_mode) or before.st_size != expected_size:
        return False
    digest = hashlib.sha256()
    fh.seek(0)
    for block in iter(lambda: fh.read(65536), b""):
        digest.update(block)
    after = os.fstat(fh.fileno())
    identity = lambda info: (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
    return identity(before) == identity(after) and digest.hexdigest() == expected_sha256


@app.get("/v1/messages/{message_id}/audio", response_model=None)
def get_audio(message_id: str, request: Request,
              authorization: str | None = Header(default=None)):
    dev = current_device(authorization)
    if dev is None:
        return err(401, "unauthorized")
    with _LOCK:
        rec = message_record(message_id)
        if rec is None or rec["state"] != "complete":
            return err(404, "not found")
        man = rec["manifest"]
        if not can_access_message(dev, message_id):
            return err(403, "forbidden")
        msg_dir = Path(rec["path"])
        if not (msg_dir / "complete").is_file() or (msg_dir / "complete").is_symlink() or msg_dir.is_symlink():
            return err(404, "not complete")
        wav = msg_dir / "media.wav"
        if man.get("audio", {}).get("path") != "media.wav" or wav.is_symlink():
            return err(404, "invalid media")
        try:
            if not wav.resolve(strict=True).is_relative_to(MESSAGES.resolve(strict=True)):
                return err(404, "invalid media")
            fd = os.open(wav, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
            fh = os.fdopen(fd, "rb")
            size = man["audio"]["bytes"]
            if not _verified_open_media(fh, man["audio"]["sha256"], size):
                fh.close()
                return err(404, "invalid media")
        except (OSError, ValueError):
            return err(404, "not found")
        kind, start, end = parse_range_header(request.headers.get("range"), size)
        if kind == "malformed":
            fh.close()
            return err(400, "malformed range")
        if kind == "unsatisfiable":
            fh.close()
            return JSONResponse({"error": "range not satisfiable"}, status_code=416,
                                headers={"Content-Range": f"bytes */{size}", "Accept-Ranges": "bytes"})
        fh.seek(start)
        length = end - start + 1
        headers = {"Content-Length": str(length), "Accept-Ranges": "bytes",
                   "ETag": f'"{man["audio"]["sha256"]}"'}
        if kind == "range":
            headers["Content-Range"] = f"bytes {start}-{end}/{size}"
    return StreamingResponse(_bounded_file(fh, length), status_code=206 if kind == "range" else 200,
                             media_type="audio/wav", headers=headers)


def _selector(body: CullPreviewBody) -> tuple[dict[str, Any] | None, JSONResponse | None]:
    if not body.message_ids and body.completed_before is None:
        return None, err(400, "empty selector")
    if len(set(body.message_ids)) != len(body.message_ids):
        return None, err(400, "duplicate message ID")
    if any(not UUID_RE.fullmatch(mid) for mid in body.message_ids):
        return None, err(400, "bad message ID")
    before = parse_timestamp(body.completed_before) if body.completed_before is not None else None
    if body.completed_before is not None and before is None:
        return None, err(400, "bad completed_before")
    return {"message_ids": sorted(body.message_ids),
            "completed_before": before.isoformat() if before else None}, None


def _select(selector: dict[str, Any]) -> tuple[list[dict[str, Any]], list[str]]:
    explicit = selector["message_ids"]
    candidates = explicit if explicit else sorted(_INDEX)
    before = parse_timestamp(selector["completed_before"]) if selector["completed_before"] else None
    details: list[dict[str, Any]] = []
    stale: list[str] = []
    for mid in candidates:
        rec = message_record(mid)
        if rec is None or rec["state"] != "complete":
            if explicit:
                stale.append(mid)
            continue
        man = rec["manifest"]
        completed = parse_timestamp(man.get("completed_at", ""))
        if completed is None:
            stale.append(mid)
            continue
        if before is not None and completed >= before:
            continue
        path = Path(rec["path"])
        manifest_file = path / "manifest.json"
        media = path / "media.wav"
        if (not manifest_file.is_file() or not (path / "complete").is_file()
                or media.is_symlink() or not media.is_file()):
            stale.append(mid)
            continue
        try:
            media_size = media.stat().st_size
            actual_media_sha = sha256_file(media)
            manifest_sha = sha256_file(manifest_file)
        except OSError:
            stale.append(mid)
            continue
        if media_size != man["audio"]["bytes"] or actual_media_sha != man["audio"]["sha256"]:
            stale.append(mid)
            continue
        refs = sorted(row["to_user"] for row in _INBOX_ROWS if row.get("message_id") == mid)
        details.append({"message_id": mid, "bytes": man["audio"]["bytes"],
                        "inbox_references": len(refs), "inbox_users": refs,
                        "manifest_sha256": manifest_sha,
                        "media_sha256": actual_media_sha,
                        "path": str(path.relative_to(STORE))})
    return details, stale


@app.post("/v1/admin/messages/cull/preview", response_model=None)
async def admin_cull_preview(body: CullPreviewBody,
                             authorization: str | None = Header(default=None)):
    dev, failure = admin_auth(authorization)
    if failure:
        return failure
    selector, failure = _selector(body)
    if failure:
        return failure
    assert selector is not None
    with _LOCK:
        details, stale = _select(selector)
        if stale:
            return err(409, "selector contains unavailable messages", message_ids=stale)
        token = uuid.uuid4().hex
        _CULL_PREVIEW[token] = {"actor": dev.id, "selector": selector, "details": details,
                                "expires_at": time.monotonic() + PREVIEW_TTL_SECONDS}
        durable.append_jsonl(AUDIT, {"action": "cull_preview", "actor": dev.id,
                                     "message_ids": [d["message_id"] for d in details], "at": utc_now()})
    public = [{key: d[key] for key in ("message_id", "bytes", "inbox_references")}
              for d in details]
    return {"selection_token": token, "count": len(details),
            "bytes": sum(d["bytes"] for d in details),
            "affected_inbox_references": sum(d["inbox_references"] for d in details),
            "messages": public}


@app.post("/v1/admin/messages/cull", response_model=None)
async def admin_cull(body: CullBody, authorization: str | None = Header(default=None)):
    dev, failure = admin_auth(authorization)
    if failure:
        return failure
    with _LOCK:
        snapshot = _CULL_PREVIEW.get(body.selection_token)
        if snapshot is None or time.monotonic() >= snapshot["expires_at"]:
            _CULL_PREVIEW.pop(body.selection_token, None)
            return err(400, "invalid or expired selection token")
        if snapshot["actor"] != dev.id:
            return err(403, "selection token belongs to another administrator")
        _CULL_PREVIEW.pop(body.selection_token, None)
        details, stale = _select(snapshot["selector"])
        if stale or details != snapshot["details"]:
            return err(409, "selection stale", message_ids=stale)
        batch = {"batch_id": str(uuid.uuid4()), "actor": dev.id,
                 "selector": snapshot["selector"], "details": details, "created_at": utc_now()}
        durable.atomic_json(CULL_INTENTS / f"{batch['batch_id']}.json", batch, boundary="cull_intent",
                            before_replace="cull_intent_temp_fsync", after_replace="cull_intent_replace")
        try:
            moved = _apply_cull_batch(batch)
        except (OSError, ValueError, KeyError) as exc:
            return err(409, "cull interrupted; recovery will retry", detail=str(exc))
        return {"trashed": moved}


@app.get("/v1/admin/messages/trash", response_model=None)
def admin_trash(authorization: str | None = Header(default=None)):
    _dev, failure = admin_auth(authorization)
    if failure:
        return failure
    with _LOCK:
        rows = []
        for mid, rec in sorted(_INDEX.items()):
            if rec["state"] != "trashed":
                continue
            deletion = _valid_deletion(Path(rec["path"]), rec["manifest"])
            if deletion:
                rows.append({"message_id": mid, "bytes": rec["manifest"]["audio"]["bytes"],
                             "media_sha256": rec["manifest"]["audio"]["sha256"],
                             "trashed_at": deletion["trashed_at"], "actor": deletion["actor"]})
        return {"trash": rows}


@app.post("/v1/admin/messages/{message_id}/restore", response_model=None)
async def admin_restore(message_id: str, authorization: str | None = Header(default=None)):
    dev, failure = admin_auth(authorization)
    if failure:
        return failure
    if not UUID_RE.fullmatch(message_id):
        return err(400, "bad id")
    with _LOCK:
        rec = message_record(message_id)
        if rec is None or rec["state"] != "trashed":
            return err(404, "not in trash")
        src = Path(rec["path"])
        man = validated_complete(src)
        if man is None:
            return err(409, "invalid trash content")
        deletion = _valid_deletion(src, man)
        if deletion is None:
            return err(409, "invalid deletion metadata")
        dest = completed_path(man)
        if dest.exists():
            return err(409, "canonical destination exists")
        deletion["restoring"] = True
        deletion["actor"] = dev.id
        deletion["restored_at"] = utc_now()
        durable.atomic_json(src / "deletion.json", deletion, boundary="restore_intent",
                            before_replace="restore_intent_temp_fsync",
                            after_replace="restore_intent_replace")
        durable.move_dir(src, dest, boundary="restore_rename",
                         after_parent_create="restore_dest_parent",
                         after_rename="restore_rename_raw",
                         after_source_sync="restore_source_parent_fsync")
        _audit_once(f"restore:{message_id}:{deletion['event_id']}",
                    {"action": "restore", "message_id": message_id, "actor": dev.id,
                     "at": deletion["restored_at"]})
        durable.crash_after("restore_audit")
        durable.durable_unlink(dest / "deletion.json")
        durable.crash_after("restore_cleanup")
        _INDEX[message_id] = {"state": "complete", "path": dest, "manifest": man}
        _repair_inboxes()  # Existing references become visible again; missing ones are repaired.
        return {"restored": message_id, "media_sha256": man["audio"]["sha256"]}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    rebuild_index()
    print(f"h34 message_store root={STORE} indexed={len(_INDEX)} "
          f"directory_fsync={durable.DIRECTORY_FSYNC_SUPPORTED}")
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
