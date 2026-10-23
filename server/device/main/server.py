#!/usr/bin/env python3
"""Product v2 desk wire server (P1 core-journey contract revisions)."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import secrets
import shutil
import ssl
import struct
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field, replace
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit
from ipaddress import ip_address

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from server_log import BoundedRedactingLog, redact_line  # noqa: E402

DEVICE_DIR = Path(__file__).resolve().parent.parent
FIXTURE_DIR = DEVICE_DIR / "fixtures"
MANIFEST_PATH = FIXTURE_DIR / "manifest.json"
SERVER_MANIFEST = DEVICE_DIR / "manifest.device.json"
FIXTURE_NAME = "p1-core-journey/v1"
PART_SIZE = 16384
INBOX_MAX_BYTES = 262144
PIN_RETENTION_S = 60
LAST_VIEWED_RETENTION_S = 60
SESSION_TTL_S = 180
OPEN_UPLOAD_TTL_S = 24 * 60 * 60
HEALTH_REASONS = frozenset({"boot", "periodic", "connectivity"})
PIN_RE = re.compile(r"^[1-9]{4}$")
UUID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
    re.I,
)
MESSAGE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,38}$")


class IdentityError(RuntimeError):
    """Fixture bytes changed without a revision bump."""


def verify_fixture_tree(fixture_dir: Path = FIXTURE_DIR) -> dict:
    manifest_path = fixture_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    revision = manifest.get("fixture_revision")
    if not revision:
        raise IdentityError("fixture manifest has no fixture_revision")
    for name, meta in (manifest.get("files") or {}).items():
        data = (fixture_dir / name).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != meta["bytes"] or digest != meta["sha256"]:
            raise IdentityError(f"fixture byte change without revision bump: {name} revision {revision}")
    return manifest


def _utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _b64url_token(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def _redact_text(text: str) -> str:
    text = redact_line(text.encode("utf-8")).decode("utf-8", errors="replace")
    text = re.sub(r'"pin"\s*:\s*"[^"]*"', '"pin":"[REDACTED]"', text)
    text = re.sub(r"(?i)(pin|password)\s*[=:]\s*\S+", r"\1=[REDACTED]", text)
    return text


def _canonical_json(value: dict) -> str:
    """Stable identity representation for replay comparisons."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


MANIFEST = verify_fixture_tree()
MANIFEST_BYTES = MANIFEST_PATH.read_bytes()
FIXTURE_SHA256 = hashlib.sha256(MANIFEST_BYTES).hexdigest()
SERVER_REVISION = json.loads(SERVER_MANIFEST.read_text(encoding="utf-8"))["lab_revision"]
ACCOUNTS = json.loads((FIXTURE_DIR / "accounts.json").read_text(encoding="utf-8"))
ROSTER = json.loads((FIXTURE_DIR / "roster.json").read_text(encoding="utf-8"))
INBOX_MAYA = json.loads((FIXTURE_DIR / "inbox-maya.json").read_text(encoding="utf-8"))
MEDIA_AUDIO = (FIXTURE_DIR / "media-audio.pcm").read_bytes()
MEDIA_SKETCH = (FIXTURE_DIR / "media-sketch.flsk2").read_bytes()
RUNTIME_DIR = DEVICE_DIR / "runtime"
TOMBSTONE_PATH = RUNTIME_DIR / "accepted-tombstones.json"
LAST_VIEWED_PATH = RUNTIME_DIR / "last-viewed.json"


def _pin_operations_path() -> Path:
    """Resolve the path at use time so tests can isolate RUNTIME_DIR."""
    return RUNTIME_DIR / "pin-operations.json"


def _load_tombstones() -> list[dict]:
    if TOMBSTONE_PATH.is_file():
        return json.loads(TOMBSTONE_PATH.read_text(encoding="utf-8")).get("records", [])
    seed = FIXTURE_DIR / "accepted-tombstones.json"
    return json.loads(seed.read_text(encoding="utf-8")).get("records", [])


def _atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=f".{path.name}.", suffix=".tmp", delete=False,
        ) as handle:
            temporary = Path(handle.name)
            json.dump(payload, handle, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        temporary = None
        _fsync_directory(path.parent)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _fsync_directory(directory: Path) -> None:
    """Make an atomic rename durable when the local filesystem supports it."""
    descriptor = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _atomic_write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", dir=path.parent,
            prefix=f".{path.name}.", suffix=".tmp", delete=False,
        ) as handle:
            temporary = Path(handle.name)
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        temporary = None
        _fsync_directory(path.parent)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _save_tombstones(records: list[dict]) -> None:
    _atomic_write_json(TOMBSTONE_PATH, {
        "schema": "p1-accepted-tombstones/v1", "records": records,
    })


def _load_last_viewed_state() -> tuple[dict[str, str], dict[str, dict]]:
    if not LAST_VIEWED_PATH.is_file():
        return {}, {}
    payload = json.loads(LAST_VIEWED_PATH.read_text(encoding="utf-8"))
    if payload.get("schema") != "p1-last-viewed/v1":
        raise IdentityError("unknown last-viewed state schema")
    return payload.get("values", {}), payload.get("operations", {})


def _save_last_viewed_state(values: dict[str, str], operations: dict[str, dict]) -> None:
    _atomic_write_json(LAST_VIEWED_PATH, {
        "schema": "p1-last-viewed/v1", "values": values, "operations": operations,
    })


def _load_pin_records() -> dict[str, "PinRecord"]:
    path = _pin_operations_path()
    if not path.is_file():
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema") != "p1-pin-operations/v1":
        raise IdentityError("unknown pin operation state schema")
    loaded: dict[str, PinRecord] = {}
    for row in payload.get("records", []):
        if not isinstance(row, dict):
            raise IdentityError("invalid pin operation record")
        endpoint_binding = row.get("endpoint_binding")
        operation_id = row.get("operation_id")
        fingerprint = row.get("fingerprint")
        accepted_at = row.get("accepted_at")
        if (
            not isinstance(endpoint_binding, str)
            or not isinstance(operation_id, str)
            or not UUID_RE.fullmatch(operation_id)
            or not isinstance(fingerprint, str)
            or isinstance(accepted_at, bool)
            or not isinstance(accepted_at, (int, float))
        ):
            raise IdentityError("invalid pin operation record")
        body = row.get("body")
        if body is not None and not isinstance(body, dict):
            raise IdentityError("invalid pin operation result")
        session_issued_at = row.get("session_issued_at")
        session_last_refresh_at = row.get("session_last_refresh_at")
        if any(
            value is not None
            and (isinstance(value, bool) or not isinstance(value, (int, float)))
            for value in (session_issued_at, session_last_refresh_at)
        ):
            raise IdentityError("invalid pin operation session timestamps")
        session_revoked = row.get("session_revoked", False)
        if not isinstance(session_revoked, bool):
            raise IdentityError("invalid pin operation session revocation")
        loaded[_pin_record_key(endpoint_binding, operation_id)] = PinRecord(
            operation_id=operation_id,
            endpoint_binding=endpoint_binding,
            fingerprint=fingerprint,
            body=body,
            accepted_at=float(accepted_at),
            session_issued_at=(
                None if session_issued_at is None else float(session_issued_at)
            ),
            session_last_refresh_at=(
                None if session_last_refresh_at is None else float(session_last_refresh_at)
            ),
            session_revoked=session_revoked,
        )
    return loaded


def _save_pin_records(records: dict[str, "PinRecord"]) -> None:
    rows = []
    for record in sorted(records.values(), key=lambda item: (item.endpoint_binding, item.accepted_at)):
        rows.append({
            "endpoint_binding": record.endpoint_binding,
            "operation_id": record.operation_id,
            "fingerprint": record.fingerprint,
            "body": record.body,
            "accepted_at": record.accepted_at,
            "session_issued_at": record.session_issued_at,
            "session_last_refresh_at": record.session_last_refresh_at,
            "session_revoked": record.session_revoked,
        })
    _atomic_write_json(_pin_operations_path(), {
        "schema": "p1-pin-operations/v1", "records": rows,
    })


def _pin_record_key(endpoint_binding: str, operation_id: str) -> str:
    return f"{endpoint_binding}:{operation_id}"


@dataclass
class PinRecord:
    operation_id: str
    endpoint_binding: str
    fingerprint: str
    body: dict | None
    accepted_at: float
    session_issued_at: float | None = None
    session_last_refresh_at: float | None = None
    session_revoked: bool = False


@dataclass
class OpenUpload:
    client_message_id: str
    message_id: str
    sender_user_id: str
    create_body: dict
    create_fingerprint: str
    frozen_recipient_user_ids: list[str]
    created_at: float
    last_acknowledged_at: float
    audio_parts: dict[int, Path] = field(default_factory=dict)
    sketch_parts: dict[int, Path] = field(default_factory=dict)


@dataclass
class AcceptedSend:
    client_message_id: str
    message_id: str
    sender_user_id: str
    accepted_at: str
    recipient_user_ids: list[str]
    # Optional persisted delivery payload.  Older tombstones omit these
    # fields; they remain receipt-only records after a restart.
    delivery_item: dict | None = None
    audio_b64: str | None = None
    sketch_b64: str | None = None
    create_fingerprint: str | None = None
    complete_fingerprint: str | None = None


def _open_metadata_path(client_message_id: str) -> Path:
    return RUNTIME_DIR / f"open-upload-{client_message_id}.json"


def _open_part_path(client_message_id: str, kind: str, sequence: int) -> Path:
    return RUNTIME_DIR / f"open-upload-{client_message_id}.{kind}.{sequence:08d}.part"


def _accepted_media_path(message_id: str, kind: str) -> Path:
    return RUNTIME_DIR / f"accepted-media-{message_id}.{kind}.bin"


def _part_manifest(parts: dict[int, Path]) -> list[dict[str, int]]:
    manifest = []
    for sequence, path in sorted(parts.items()):
        try:
            byte_count = path.stat().st_size
        except OSError:
            continue
        manifest.append({"sequence": sequence, "byte_count": byte_count})
    return manifest


def _open_payload(upload: OpenUpload) -> dict:
    return {
        "schema": "p2-open-upload/v1",
        "client_message_id": upload.client_message_id,
        "message_id": upload.message_id,
        "sender_user_id": upload.sender_user_id,
        "create_body": upload.create_body,
        "create_fingerprint": upload.create_fingerprint,
        "frozen_recipient_user_ids": upload.frozen_recipient_user_ids,
        "created_at": upload.created_at,
        "last_acknowledged_at": upload.last_acknowledged_at,
        "audio_parts": _part_manifest(upload.audio_parts),
        "sketch_parts": _part_manifest(upload.sketch_parts),
    }


def _save_open_upload(upload: OpenUpload) -> None:
    _atomic_write_json(_open_metadata_path(upload.client_message_id), _open_payload(upload))


def _valid_part_manifest(row: Any) -> bool:
    return (
        isinstance(row, dict)
        and isinstance(row.get("sequence"), int)
        and not isinstance(row.get("sequence"), bool)
        and row["sequence"] >= 0
        and isinstance(row.get("byte_count"), int)
        and not isinstance(row.get("byte_count"), bool)
        and 1 <= row["byte_count"] <= PART_SIZE
    )


def _load_open_uploads() -> dict[str, OpenUpload]:
    loaded: dict[str, OpenUpload] = {}
    if not RUNTIME_DIR.is_dir():
        return loaded
    for metadata_path in sorted(RUNTIME_DIR.glob("open-upload-*.json")):
        try:
            payload = json.loads(metadata_path.read_text(encoding="utf-8"))
            client_id = payload["client_message_id"]
            message_id = payload["message_id"]
            sender_user_id = payload["sender_user_id"]
            create_body = payload["create_body"]
            create_fingerprint = payload["create_fingerprint"]
            recipients = payload["frozen_recipient_user_ids"]
            created_at = payload["created_at"]
            last_acknowledged_at = payload["last_acknowledged_at"]
            if (
                not isinstance(client_id, str) or not UUID_RE.fullmatch(client_id)
                or not isinstance(message_id, str) or not MESSAGE_ID_RE.fullmatch(message_id)
                or not isinstance(sender_user_id, str) or not isinstance(create_body, dict)
                or not isinstance(create_fingerprint, str) or not isinstance(recipients, list)
                or any(not isinstance(item, str) for item in recipients)
                or isinstance(created_at, bool) or not isinstance(created_at, (int, float))
                or isinstance(last_acknowledged_at, bool)
                or not isinstance(last_acknowledged_at, (int, float))
                or create_fingerprint != _canonical_json(create_body)
            ):
                continue
            if time.time() - float(last_acknowledged_at) > OPEN_UPLOAD_TTL_S:
                _remove_open_files(client_id)
                continue
            upload = OpenUpload(
                client_message_id=client_id,
                message_id=message_id,
                sender_user_id=sender_user_id,
                create_body=create_body,
                create_fingerprint=create_fingerprint,
                frozen_recipient_user_ids=list(recipients),
                created_at=float(created_at),
                last_acknowledged_at=float(last_acknowledged_at),
            )
            for kind, target in (("audio", upload.audio_parts), ("sketch", upload.sketch_parts)):
                seen: set[int] = set()
                for row in payload.get(f"{kind}_parts", []):
                    if not _valid_part_manifest(row) or row["sequence"] in seen:
                        continue
                    sequence = row["sequence"]
                    part_path = _open_part_path(client_id, kind, sequence)
                    try:
                        if part_path.stat().st_size != row["byte_count"]:
                            continue
                    except OSError:
                        continue
                    seen.add(sequence)
                    target[sequence] = part_path
            loaded[client_id] = upload
        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
            # A torn or unknown open-upload record is not an accepted send.
            # Leave it untouched for inspection; it cannot become source of truth.
            continue
    return loaded


def _remove_open_files(client_message_id: str) -> None:
    metadata_path = _open_metadata_path(client_message_id)
    metadata_path.unlink(missing_ok=True)
    for path in RUNTIME_DIR.glob(f"open-upload-{client_message_id}.*.part"):
        path.unlink(missing_ok=True)


def _expire_open_locked(client_id: str, upload: OpenUpload | None = None) -> bool:
    if upload is None:
        upload = STORE.opens.get(client_id)
    if upload is None:
        return False
    if time.time() - upload.last_acknowledged_at <= OPEN_UPLOAD_TTL_S:
        return False
    STORE.opens.pop(client_id, None)
    _remove_open_files(client_id)
    return True


def _accepted_from_row(row: dict) -> AcceptedSend:
    """Load current rows and tolerate the P1 base64 fields during migration."""
    return AcceptedSend(
        client_message_id=row["client_message_id"],
        message_id=row["message_id"],
        sender_user_id=row["sender_user_id"],
        accepted_at=row["accepted_at"],
        recipient_user_ids=list(row.get("recipient_user_ids", [])),
        delivery_item=row.get("delivery_item"),
        audio_b64=row.get("audio_b64"),
        sketch_b64=row.get("sketch_b64"),
        create_fingerprint=row.get("create_fingerprint"),
        complete_fingerprint=row.get("complete_fingerprint"),
    )


def _accepted_row_payload(accepted: AcceptedSend) -> dict:
    """Persist receipt and delivery metadata, never accepted media bytes."""
    return {
        "client_message_id": accepted.client_message_id,
        "message_id": accepted.message_id,
        "sender_user_id": accepted.sender_user_id,
        "accepted_at": accepted.accepted_at,
        "recipient_user_ids": accepted.recipient_user_ids,
        "delivery_item": accepted.delivery_item,
        "create_fingerprint": accepted.create_fingerprint,
        "complete_fingerprint": accepted.complete_fingerprint,
    }


class Store:
    def __init__(self, tombstones: list[dict] | None = None) -> None:
        self.lock = threading.Lock()
        self.last_successful_beat_at: float | None = None
        self.drop_next_health_ack = 0
        self.oversized_inbox_once = 0
        self.pin_records: dict[str, PinRecord] = _load_pin_records()
        self.invalidated_operations: set[tuple[str, str]] = set()
        self.sessions: dict[str, dict] = {}
        self.opens: dict[str, OpenUpload] = _load_open_uploads()
        self.accepted: dict[str, AcceptedSend] = {}
        # Runtime delivery state.  The fixture remains the seed inbox for Maya;
        # accepted sends are published into these per-user snapshots and media
        # stores instead of disappearing at complete time.
        self.inboxes: dict[str, dict] = {}
        # Durable files are the source of truth. This index contains paths only
        # and is populated lazily from accepted metadata at startup.
        self.media: dict[str, dict[str, Path]] = {}
        self.last_viewed, self.last_viewed_ops = _load_last_viewed_state()
        if tombstones:
            for row in tombstones:
                self.accepted[row["client_message_id"]] = _accepted_from_row(row)
        self._restore_persisted_pin_sessions_locked()

    def _restore_persisted_pin_sessions_locked(self) -> None:
        """Restore only recently accepted sessions needed for login replay."""
        # A replayed authenticated login must return a usable credential even
        # when the request crossed a process restart. The credential remains
        # subject to the normal three-minute lifetime and endpoint binding.
        for record in self.pin_records.values():
            if time.time() - record.accepted_at > PIN_RETENTION_S:
                continue
            if not record.body or record.body.get("outcome") != "authenticated":
                continue
            session = record.body.get("session")
            user_id = record.body.get("user_id")
            operation_id = record.body.get("operation_id")
            if (
                isinstance(session, str)
                and isinstance(user_id, str)
                and isinstance(operation_id, str)
            ):
                issued_at = record.session_issued_at
                if issued_at is None:
                    issued_at = record.accepted_at
                last_refresh_at = record.session_last_refresh_at
                if last_refresh_at is None:
                    last_refresh_at = issued_at
                self.sessions[session] = {
                    "user_id": user_id,
                    "operation_id": operation_id,
                    "endpoint_binding": record.endpoint_binding,
                    "issued_at": issued_at,
                    "last_refresh_at": last_refresh_at,
                    "revoked": record.session_revoked,
                }

    def reset_runtime(self) -> None:
        with self.lock:
            self.last_successful_beat_at = None
            self.drop_next_health_ack = 0
            self.oversized_inbox_once = 0
            self.pin_records.clear()
            self.invalidated_operations.clear()
            self.sessions.clear()
            self.opens = _load_open_uploads()
            self.pin_records = _load_pin_records()
            self._restore_persisted_pin_sessions_locked()
            self.inboxes.clear()
            self.media.clear()
            self.last_viewed, self.last_viewed_ops = _load_last_viewed_state()
            self.accepted.clear()
            for row in _load_tombstones():
                self.accepted[row["client_message_id"]] = _accepted_from_row(row)
            _restore_persisted_deliveries_locked()
            # Accepted receipts, delivered media, and last-viewed state survive.

    def observe_health_clock(self) -> dict:
        with self.lock:
            return {
                "last_successful_beat_at": self.last_successful_beat_at,
                "fixture": FIXTURE_NAME,
            }


STORE = Store(_load_tombstones())


def _empty_inbox() -> dict:
    return {
        "schema": "fl.inbox-page/2",
        "inbox_revision": 0,
        "total_count": 0,
        "last_viewed_message_id": None,
        "items": [],
        "next_cursor": None,
        "has_more": False,
    }


def _inbox_for_user_locked(user_id: str) -> dict:
    snapshot = STORE.inboxes.get(user_id)
    if snapshot is None:
        # Copy the fixture so later publication never mutates the module-level
        # seed shared by tests and other requests.
        snapshot = json.loads(json.dumps(INBOX_MAYA if user_id == "user-maya" else _empty_inbox()))
        STORE.inboxes[user_id] = snapshot
    if user_id in STORE.last_viewed:
        selected = STORE.last_viewed[user_id]
        snapshot["last_viewed_message_id"] = selected if any(
            item.get("message_id") == selected for item in snapshot["items"]
        ) else None
    return snapshot


def _roster_member(user_id: str) -> dict | None:
    return next((row for row in ROSTER.get("users", []) if row.get("user_id") == user_id), None)


def _validate_create_body(body: dict, sender_user_id: str) -> tuple[int, str] | None:
    recipients = body.get("recipient_user_ids")
    if not isinstance(recipients, list) or not recipients:
        return 400, "invalid_request"
    if len(recipients) > 7:
        return 422, "invalid_request"
    if any(not isinstance(recipient, str) for recipient in recipients):
        return 400, "invalid_request"
    if len(set(recipients)) != len(recipients) or sender_user_id in recipients:
        return 422, "recipient_unavailable"
    if any(_roster_member(recipient) is None for recipient in recipients):
        return 422, "recipient_unavailable"

    audio = body.get("audio")
    if not isinstance(audio, dict):
        return 400, "invalid_request"
    if audio.get("codec") != "pcm_s16le" or audio.get("sample_rate_hz") != 16000 or audio.get("channels") != 1:
        return 400, "invalid_request"
    duration_ms = audio.get("duration_ms")
    if isinstance(duration_ms, bool) or not isinstance(duration_ms, int) or not 1 <= duration_ms <= 180000:
        return 400, "invalid_request"

    if "sketch" in body:
        sketch = body.get("sketch")
        if not isinstance(sketch, dict) or sketch.get("present") is not True or sketch.get("format_id") != "flsk2":
            return 400, "invalid_request"
    return None


def _read_upload_parts(upload: OpenUpload, kind: str) -> bytes | None:
    parts = upload.audio_parts if kind == "audio" else upload.sketch_parts
    try:
        return b"".join(parts[index].read_bytes() for index in sorted(parts))
    except (OSError, KeyError):
        return None


def _build_accepted_send_locked(upload: OpenUpload, complete_body: dict,
                                accepted_at: str) -> tuple[AcceptedSend, bytes, bytes | None] | None:
    """Build a complete delivery record without changing visible state."""
    recipients = upload.create_body.get("recipient_user_ids")
    if not isinstance(recipients, list) or not recipients or len(recipients) > 7:
        return None
    if len(set(recipients)) != len(recipients) or upload.sender_user_id in recipients:
        return None
    if any(_roster_member(str(user_id)) is None for user_id in recipients):
        return None

    audio_meta = complete_body.get("audio") or {}
    audio = _read_upload_parts(upload, "audio")
    if audio is None:
        return None
    sketch_present = bool(upload.create_body.get("sketch", {}).get("present"))
    sketch = _read_upload_parts(upload, "sketch") if sketch_present else None
    if sketch_present and sketch is None:
        return None
    accepted = AcceptedSend(
        client_message_id=upload.client_message_id,
        message_id=upload.message_id,
        sender_user_id=upload.sender_user_id,
        accepted_at=accepted_at,
        recipient_user_ids=[str(user_id) for user_id in recipients],
        create_fingerprint=_canonical_json(upload.create_body),
        complete_fingerprint=_canonical_json(complete_body),
    )
    sender = _roster_member(upload.sender_user_id) or {}
    item = {
        "message_id": upload.message_id,
        "sender_user_id": upload.sender_user_id,
        "sender_name": sender.get("display_name", upload.sender_user_id),
        "received_at": accepted_at,
        "kind": "audio_sketch" if sketch_present else "audio",
        "duration_ms": int(audio_meta.get("duration_ms", 0)),
        "media": {
            "audio": {
                "format_id": "pcm_s16le",
                "byte_count": len(audio),
                "sha256": hashlib.sha256(audio).hexdigest(),
            }
        },
    }
    if sketch_present:
        item["media"]["sketch"] = {
            "format_id": "flsk2",
            "byte_count": len(sketch),
            "sha256": hashlib.sha256(sketch).hexdigest(),
        }

    accepted.delivery_item = item
    return accepted, audio, sketch


def _apply_accepted_delivery_locked(accepted: AcceptedSend) -> None:
    if not accepted.delivery_item:
        return
    audio_path = _accepted_media_path(accepted.message_id, "audio")
    sketch_path = _accepted_media_path(accepted.message_id, "sketch")
    # P1 rows may still contain base64 media. Migrate those rows once, but all
    # P2 rows use files and never retain media bytes in the accepted index.
    if not audio_path.is_file() and accepted.audio_b64:
        try:
            _atomic_write_bytes(audio_path, base64.b64decode(accepted.audio_b64))
            if accepted.sketch_b64:
                _atomic_write_bytes(sketch_path, base64.b64decode(accepted.sketch_b64))
        except (OSError, ValueError):
            return
    if not audio_path.is_file():
        return
    for recipient in accepted.recipient_user_ids:
        snapshot = _inbox_for_user_locked(str(recipient))
        if any(item.get("message_id") == accepted.message_id for item in snapshot["items"]):
            continue
        snapshot["items"].append(json.loads(json.dumps(accepted.delivery_item)))
        snapshot["total_count"] = len(snapshot["items"])
        snapshot["inbox_revision"] = int(snapshot.get("inbox_revision", 0)) + 1
        selected = STORE.last_viewed.get(str(recipient))
        if selected == accepted.message_id:
            snapshot["last_viewed_message_id"] = selected
    STORE.media[accepted.message_id] = {"audio": audio_path}
    if accepted.delivery_item.get("media", {}).get("sketch") and sketch_path.is_file():
        STORE.media[accepted.message_id]["sketch"] = sketch_path


def _restore_persisted_deliveries_locked() -> None:
    for accepted in STORE.accepted.values():
        _apply_accepted_delivery_locked(accepted)


def _restore_persisted_deliveries() -> None:
    with STORE.lock:
        _restore_persisted_deliveries_locked()


_restore_persisted_deliveries()


def reload_persisted_tombstones() -> None:
    with STORE.lock:
        for row in _load_tombstones():
            STORE.accepted[row["client_message_id"]] = _accepted_from_row(row)
        STORE.opens = _load_open_uploads()
    _restore_persisted_deliveries()


class LabServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, address, handler, run_id: str, log: BoundedRedactingLog | None,
                 tls_enabled: bool = False):
        super().__init__(address, handler)
        self.run_id = run_id
        self.log = log
        self.tls_enabled = tls_enabled


class Handler(BaseHTTPRequestHandler):
    server: LabServer
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        line = _redact_text(fmt % args)
        encoded = line.encode("utf-8", errors="replace")
        sys.stdout.buffer.write(encoded + b"\n")
        sys.stdout.buffer.flush()
        if self.server.log is not None:
            self.server.log.feed("stdout", encoded + b"\n")

    def _send_raw(self, status: int, body: bytes, content_type: str = "application/json",
                  extra_headers: dict[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Fixture-SHA256", FIXTURE_SHA256)
        self.send_header("X-Run-ID", self.server.run_id)
        if extra_headers:
            for key, value in extra_headers.items():
                self.send_header(key, value)
        if self.server.tls_enabled:
            self.send_header("Strict-Transport-Security", "max-age=31536000")
        self.send_header("Connection", "close")
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _json(self, status: int, payload: dict | list | str) -> None:
        if isinstance(payload, str):
            body = payload.encode("utf-8")
        else:
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self._send_raw(status, body)

    def _empty_json(self, status: int = 200) -> None:
        self._json(status, {})

    def _parse_byte_range(self, total: int) -> tuple[int, int] | None:
        header = self.headers.get("Range", "")
        if not header.startswith("bytes="):
            return None
        spec = header[6:].split(",", 1)[0].strip()
        if "-" not in spec:
            return None
        start_s, end_s = spec.split("-", 1)
        if start_s == "":
            try:
                suffix = int(end_s)
            except ValueError:
                return None
            if suffix <= 0:
                return None
            start = max(0, total - suffix)
            end = total - 1
        else:
            try:
                start = int(start_s)
            except ValueError:
                return None
            if end_s == "":
                end = total - 1
            else:
                try:
                    end = int(end_s)
                except ValueError:
                    return None
        if start < 0 or end < start or start >= total:
            return None
        end = min(end, total - 1)
        return start, end

    def _serve_ranged_bytes(self, body: bytes, content_type: str, log_kind: str,
                            message_id: str, kind: str) -> None:
        span = self._parse_byte_range(len(body))
        extra = {"Accept-Ranges": "bytes"}
        if span is None:
            self._send_raw(200, body, content_type, extra_headers=extra)
            self.log_message("media status=%s message=%s kind=%s", "200", message_id, kind)
            return
        start, end = span
        slice_body = body[start : end + 1]
        extra["Content-Range"] = f"bytes {start}-{end}/{len(body)}"
        self._send_raw(206, slice_body, content_type, extra_headers=extra)
        self.log_message(
            "media status=%s message=%s kind=%s range=%s-%s",
            "206",
            message_id,
            kind,
            start,
            end,
        )

    def _error(self, status: int, code: str, retryable: bool, operation_id: str = "") -> None:
        payload: dict[str, Any] = {
            "schema": "fl.error/2",
            "code": code,
            "message": code,
            "retryable": retryable,
        }
        if operation_id:
            payload["operation_id"] = operation_id
        self._json(status, payload)

    def _read_json(self, max_len: int = 65536) -> dict | None:
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length < 0 or length > max_len:
            return None
        raw = self.rfile.read(length) if length else b""
        if not raw and length:
            return None
        try:
            value = json.loads(raw.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError):
            return None
        return value if isinstance(value, dict) else None

    def _desk_token(self) -> str | None:
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            return None
        token = header[7:].strip()
        if token != ACCOUNTS["desk_token"]:
            return None
        return token

    @staticmethod
    def _endpoint_binding(token: str) -> str:
        """Stable non-secret identity for the authenticated desk credential."""
        return hashlib.sha256(token.encode("utf-8")).hexdigest()

    def _session_user(self) -> tuple[str | None, str]:
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            return None, "missing"
        token = header[7:].strip()
        with STORE.lock:
            record = STORE.sessions.get(token)
        if record is None:
            return None, "missing"
        if record.get("endpoint_binding") != self._endpoint_binding(ACCOUNTS["desk_token"]):
            return None, "missing"
        if record.get("revoked"):
            return None, "revoked"
        last_refresh_at = float(record.get("last_refresh_at", record.get("issued_at", 0.0)))
        if time.time() - last_refresh_at > SESSION_TTL_S:
            return None, "expired"
        return str(record["user_id"]), "ok"

    def _session_error(self, state: str) -> None:
        if state == "expired":
            self._error(403, "session_expired", False)
        else:
            self._error(401, "unauthenticated", False)

    def _issue_session(self, user_id: str, operation_id: str) -> str:
        token = _b64url_token(secrets.token_bytes(32))
        now = time.time()
        endpoint_binding = self._endpoint_binding(ACCOUNTS["desk_token"])
        with STORE.lock:
            self._store_session_locked(token, user_id, operation_id, endpoint_binding, now)
        return token

    @staticmethod
    def _store_session_locked(token: str, user_id: str, operation_id: str,
                              endpoint_binding: str, now: float) -> None:
        STORE.sessions[token] = {
            "user_id": user_id,
            "operation_id": operation_id,
            "endpoint_binding": endpoint_binding,
            "issued_at": now,
            "last_refresh_at": now,
            "revoked": False,
        }

    def _pin_fingerprint(self, body: dict) -> str:
        canonical = json.dumps(
            {
                "schema": body.get("schema"),
                "operation_id": body.get("operation_id"),
                "user_id": body.get("user_id"),
                "pin": body.get("pin"),
            },
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        # The durable replay record needs an equality key, not the PIN itself.
        return hashlib.sha256(canonical).hexdigest()

    @staticmethod
    def _valid_login_body(body: dict) -> bool:
        operation_id = body.get("operation_id")
        user_id = body.get("user_id")
        pin = body.get("pin")
        return (
            isinstance(operation_id, str)
            and UUID_RE.fullmatch(operation_id) is not None
            and isinstance(user_id, str)
            and bool(user_id)
            and isinstance(pin, str)
            and PIN_RE.fullmatch(pin) is not None
        )

    def _expected_pin(self, user_id: str) -> str | None:
        for user in ACCOUNTS["users"]:
            if user["user_id"] == user_id:
                return user["pin"]
        return None

    def do_GET(self):
        parsed = urlsplit(self.path)
        path = parsed.path
        if path == "/health":
            self._json(200, {
                "run_id": self.server.run_id,
                "fixture_version": MANIFEST["fixture_version"],
                "fixture_sha256": FIXTURE_SHA256,
                "server_version": MANIFEST["server_version"],
                "contracts": MANIFEST.get("contracts", {}),
            })
            self.log_message("health status=%s", "200")
            return
        if path == "/internal/health-clock":
            self._json(200, STORE.observe_health_clock())
            return
        if path == "/v2/hangout":
            if self._desk_token() is None:
                self._error(401, "unauthenticated", False)
                return
            self._json(200, ROSTER)
            self.log_message("hangout status=%s revision=%s", "200", ROSTER["revision"])
            return
        if path == "/v2/inbox":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            with STORE.lock:
                if STORE.oversized_inbox_once > 0:
                    STORE.oversized_inbox_once -= 1
                    huge = json.dumps({"schema": "fl.inbox-page/2", "pad": "x" * (INBOX_MAX_BYTES + 1024)})
                    self._send_raw(200, huge.encode("utf-8"))
                    self.log_message("inbox status=%s mode=%s", "200", "oversized")
                    return
                snapshot = json.loads(json.dumps(_inbox_for_user_locked(user)))
            self._json(200, snapshot)
            self.log_message("inbox status=%s revision=%s", "200", snapshot["inbox_revision"])
            return
        media_match = re.fullmatch(r"/v2/messages/([^/]+)/media/(audio|sketch)", path)
        if media_match:
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            message_id, kind = media_match.group(1), media_match.group(2)
            with STORE.lock:
                snapshot = _inbox_for_user_locked(user)
                visible = any(item.get("message_id") == message_id for item in snapshot["items"])
                dynamic_body = STORE.media.get(message_id, {}).get(kind)
            if not visible:
                self._error(404, "not_found", False)
                return
            if dynamic_body is not None:
                body = dynamic_body
            elif user == "user-maya" and message_id == INBOX_MAYA["items"][0]["message_id"]:
                body = MEDIA_AUDIO if kind == "audio" else MEDIA_SKETCH
            else:
                self._error(404, "not_found", False)
                return
            self._serve_ranged_bytes(body, "application/octet-stream", "media", message_id, kind)
            return
        receipt_match = re.fullmatch(r"/v2/outgoing/by-client-id/([^/]+)", path)
        if receipt_match:
            client_id = receipt_match.group(1)
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            with STORE.lock:
                open_upload = STORE.opens.get(client_id)
                accepted = STORE.accepted.get(client_id)
            if accepted is not None and accepted.sender_user_id == user:
                self._json(200, self._receipt_body(accepted))
                return
            if open_upload is not None and open_upload.sender_user_id == user:
                self._json(200, self._open_body(open_upload))
                return
            self._error(404, "not_found", True)
            return
        self._error(404, "not_found", False)

    def _open_body(self, upload: OpenUpload) -> dict:
        body = {
            "schema": "fl.send-open/2",
            "client_message_id": upload.client_message_id,
            "message_id": upload.message_id,
            "state": "open",
            "audio": {"parts_acknowledged": len(upload.audio_parts)},
        }
        if upload.create_body.get("sketch", {}).get("present"):
            body["sketch"] = {"parts_acknowledged": len(upload.sketch_parts)}
        return body

    def _receipt_body(self, accepted: AcceptedSend) -> dict:
        return {
            "schema": "fl.send-receipt/2",
            "client_message_id": accepted.client_message_id,
            "message_id": accepted.message_id,
            "state": "accepted",
            "accepted_at": accepted.accepted_at,
            "recipients": [
                {"user_id": rid, "inbox_message_id": accepted.message_id}
                for rid in accepted.recipient_user_ids
            ],
        }

    def do_POST(self):
        parsed = urlsplit(self.path)
        path = parsed.path
        if path == "/v1/control":
            if not ip_address(self.client_address[0]).is_loopback:
                self._error(403, "forbidden", False)
                return
            if self._desk_token() is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None:
                self._error(400, "invalid_request", False)
                return
            if body.get("reset_runtime"):
                STORE.reset_runtime()
            with STORE.lock:
                if "drop_next_health_ack" in body:
                    STORE.drop_next_health_ack = int(body.get("drop_next_health_ack") or 0)
                if "oversized_inbox_once" in body:
                    STORE.oversized_inbox_once = int(body.get("oversized_inbox_once") or 0)
            self._json(200, {"result": "ok"})
            return
        if path == "/v2/endpoint/health":
            desk_token = self._desk_token()
            if desk_token is None:
                self._error(401, "unauthenticated", False)
                return
            payload = self._read_json(4096)
            if payload is None or payload.get("schema") != "fl.endpoint-health/2":
                self._error(400, "unsupported_schema", False)
                return
            if (
                set(payload) != {"schema", "reason", "boot_id", "muted"}
                or not isinstance(payload.get("reason"), str)
                or payload.get("reason") not in HEALTH_REASONS
                or not isinstance(payload.get("boot_id"), str)
                or not payload.get("boot_id")
                or not isinstance(payload.get("muted"), bool)
            ):
                self._error(400, "invalid_request", False)
                return
            with STORE.lock:
                # Validation and endpoint authentication are complete before
                # the server freshness clock can move.
                STORE.last_successful_beat_at = time.time()
                if STORE.drop_next_health_ack > 0:
                    STORE.drop_next_health_ack -= 1
                    self.send_response(200)
                    self.end_headers()
                    self.log_message("health status=%s mode=%s", "drop", "lost_ack")
                    return
            ack = {
                "schema": "fl.endpoint-health-ack/2",
                "accepted": True,
                "server_time": _utc_now(),
            }
            self._json(200, ack)
            self.log_message("health status=%s", "200")
            return
        if path == "/v2/session/login":
            desk_token = self._desk_token()
            if desk_token is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None or body.get("schema") != "fl.session-login/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = body.get("operation_id")
            operation_id_for_error = operation_id if isinstance(operation_id, str) else ""
            if not self._valid_login_body(body):
                self._error(400, "invalid_request", False, operation_id_for_error)
                return
            endpoint_binding = self._endpoint_binding(desk_token)
            operation_key = _pin_record_key(endpoint_binding, operation_id)
            fingerprint = self._pin_fingerprint(body)
            with STORE.lock:
                now = time.time()
                existing = STORE.pin_records.get(operation_key)
                if existing is not None:
                    if existing.fingerprint != fingerprint:
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                    if now - existing.accepted_at > PIN_RETENTION_S:
                        self._error(410, "operation_expired", False, operation_id)
                        return
                    if existing.body is None:
                        self._error(410, "operation_expired", False, operation_id)
                        return
                    self._json(200, existing.body)
                    self.log_message("pin status=%s mode=%s", "200", "replay")
                    return
                if (endpoint_binding, operation_id) in STORE.invalidated_operations:
                    result = {
                        "schema": "fl.session-login-result/2",
                        "operation_id": operation_id,
                        "outcome": "cancelled",
                    }
                else:
                    user_id = body["user_id"]
                    pin = body["pin"]
                    expected = self._expected_pin(user_id)
                    if expected is None:
                        result = {
                            "schema": "fl.session-login-result/2",
                            "operation_id": operation_id,
                            "outcome": "unknown_user",
                        }
                    elif expected != pin:
                        result = {
                            "schema": "fl.session-login-result/2",
                            "operation_id": operation_id,
                            "outcome": "wrong_pin",
                        }
                    else:
                        session = _b64url_token(secrets.token_bytes(32))
                        self._store_session_locked(
                            session, user_id, operation_id, endpoint_binding, now,
                        )
                        result = {
                            "schema": "fl.session-login-result/2",
                            "operation_id": operation_id,
                            "outcome": "authenticated",
                            "user_id": user_id,
                            "session": session,
                        }
                next_pin_records = dict(STORE.pin_records)
                next_pin_records[operation_key] = PinRecord(
                    operation_id=operation_id,
                    endpoint_binding=endpoint_binding,
                    fingerprint=fingerprint,
                    body=result,
                    accepted_at=now,
                    session_issued_at=now if result.get("outcome") == "authenticated" else None,
                    session_last_refresh_at=now if result.get("outcome") == "authenticated" else None,
                )
                try:
                    _save_pin_records(next_pin_records)
                except OSError:
                    if result.get("session"):
                        STORE.sessions.pop(result["session"], None)
                    self._error(500, "storage_unavailable", True, operation_id)
                    return
                STORE.pin_records = next_pin_records
            self._json(200, result)
            self.log_message("pin status=%s outcome=%s", "200", result["outcome"])
            return
        if path == "/v2/session/invalidate":
            desk_token = self._desk_token()
            if desk_token is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None or body.get("schema") != "fl.session-invalidate/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = body.get("operation_id")
            session_token = body.get("session")
            if not isinstance(operation_id, str) or not UUID_RE.fullmatch(operation_id):
                self._error(400, "invalid_request", False,
                            operation_id if isinstance(operation_id, str) else "")
                return
            if session_token is not None and (
                not isinstance(session_token, str) or not session_token
            ):
                self._error(400, "invalid_request", False, operation_id)
                return
            endpoint_binding = self._endpoint_binding(desk_token)
            with STORE.lock:
                if session_token:
                    session_record = STORE.sessions.get(session_token)
                    if session_record is not None and (
                        session_record.get("endpoint_binding") != endpoint_binding
                        or session_record.get("operation_id") != operation_id
                    ):
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                existing = STORE.pin_records.get(
                    _pin_record_key(endpoint_binding, operation_id)
                )
                if existing is not None and existing.body is not None:
                    next_pin_records = dict(STORE.pin_records)
                    next_pin_records[_pin_record_key(endpoint_binding, operation_id)] = replace(
                        existing, session_revoked=True,
                    )
                    try:
                        _save_pin_records(next_pin_records)
                    except OSError:
                        self._error(500, "storage_unavailable", True, operation_id)
                        return
                    STORE.pin_records = next_pin_records
                STORE.invalidated_operations.add((endpoint_binding, operation_id))
                for token, record in list(STORE.sessions.items()):
                    if (
                        record.get("endpoint_binding") == endpoint_binding
                        and record.get("operation_id") == operation_id
                    ):
                        record["revoked"] = True
            self._json(200, {
                "schema": "fl.session-invalidate-result/2",
                "operation_id": operation_id,
                "revoked": True,
            })
            return
        if path == "/v2/session/refresh":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            refresh_body = self._read_json(64)
            if refresh_body is None:
                self._error(400, "invalid_request", False)
                return
            if refresh_body != {}:
                self._error(400, "invalid_request", False)
                return
            token = self.headers.get("Authorization", "")[7:].strip()
            with STORE.lock:
                record = STORE.sessions.get(token)
                if record is None or record.get("revoked"):
                    self._error(401, "unauthenticated", False)
                    return
                if time.time() - float(record.get("last_refresh_at", record.get("issued_at", 0.0))) > SESSION_TTL_S:
                    self._error(403, "session_expired", False)
                    return
                now = time.time()
                operation_key = _pin_record_key(
                    record["endpoint_binding"], record["operation_id"]
                )
                existing = STORE.pin_records.get(operation_key)
                if existing is not None and existing.body is not None:
                    next_pin_records = dict(STORE.pin_records)
                    next_pin_records[operation_key] = replace(
                        existing, session_last_refresh_at=now,
                    )
                    try:
                        _save_pin_records(next_pin_records)
                    except OSError:
                        self._error(500, "storage_unavailable", True)
                        return
                    STORE.pin_records = next_pin_records
                record["last_refresh_at"] = now
            self._empty_json(200)
            return
        if path == "/v2/outgoing":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            body = self._read_json()
            if body is None or body.get("schema") != "fl.message-create/2":
                self._error(400, "unsupported_schema", False)
                return
            client_id = str(body.get("client_message_id", ""))
            if not UUID_RE.fullmatch(client_id):
                self._error(400, "invalid_request", False)
                return
            with STORE.lock:
                accepted = STORE.accepted.get(client_id)
                if accepted is not None:
                    if accepted.sender_user_id != user:
                        self._error(403, "forbidden", False)
                        return
                    if accepted.create_fingerprint is not None and accepted.create_fingerprint != _canonical_json(body):
                        self._error(409, "identity_conflict", False)
                        return
                    self._json(200, self._receipt_body(accepted))
                    return
                open_upload = STORE.opens.get(client_id)
                if open_upload is not None:
                    if open_upload.sender_user_id != user:
                        self._error(403, "forbidden", False)
                        return
                    if _canonical_json(open_upload.create_body) != _canonical_json(body):
                        self._error(409, "identity_conflict", False)
                        return
                    self._json(200, self._open_body(open_upload))
                    return
                validation_error = _validate_create_body(body, user)
                if validation_error is not None:
                    self._error(validation_error[0], validation_error[1], False)
                    return
                message_id = f"msg-{secrets.token_hex(8)}"
                STORE.opens[client_id] = OpenUpload(client_id, message_id, user, body)
                upload = STORE.opens[client_id]
            self._json(200, self._open_body(upload))
            self.log_message("send-open status=%s client=%s", "200", client_id[:8])
            return
        complete_match = re.fullmatch(r"/v2/outgoing/([^/]+)/complete", path)
        if complete_match:
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            message_id = complete_match.group(1)
            body = self._read_json()
            if body is None or body.get("schema") != "fl.message-complete/2":
                self._error(400, "unsupported_schema", False)
                return
            client_id = str(body.get("client_message_id", ""))
            with STORE.lock:
                accepted = STORE.accepted.get(client_id)
                if accepted is not None and accepted.message_id == message_id:
                    if accepted.sender_user_id != user:
                        self._error(403, "forbidden", False)
                        return
                    if accepted.complete_fingerprint is not None and accepted.complete_fingerprint != _canonical_json(body):
                        self._error(409, "identity_conflict", False)
                        return
                    self._json(200, self._receipt_body(accepted))
                    return
                upload = STORE.opens.get(client_id)
                if upload is None or upload.message_id != message_id or upload.sender_user_id != user:
                    self._error(404, "not_found", False)
                    return
                if not self._upload_complete(upload, body):
                    self._error(413, "invalid_request", False)
                    return
                accepted_row = _build_accepted_send_locked(upload, body, _utc_now())
                if accepted_row is None:
                    self._error(422, "recipient_unavailable", False)
                    return
                records = [row.__dict__ for row in STORE.accepted.values()]
                records.append(accepted_row.__dict__)
                try:
                    _save_tombstones(records)
                except OSError:
                    self._error(500, "storage_unavailable", True)
                    return
                STORE.accepted[client_id] = accepted_row
                STORE.opens.pop(client_id, None)
                _apply_accepted_delivery_locked(accepted_row)
            self._json(200, self._receipt_body(accepted_row))
            return
        self._error(404, "not_found", False)

    def _upload_complete(self, upload: OpenUpload, complete_body: dict) -> bool:
        audio_meta = complete_body.get("audio")
        if not isinstance(audio_meta, dict):
            return False
        audio = self._assemble_parts(upload.audio_parts, audio_meta.get("parts"))
        if audio is None or len(audio) > 5_760_000 or len(audio) % 2 != 0:
            return False
        if not self._matches_media_metadata(audio, audio_meta):
            return False
        create_audio = upload.create_body.get("audio") or {}
        duration_ms = audio_meta.get("duration_ms")
        if isinstance(duration_ms, bool) or not isinstance(duration_ms, int):
            return False
        if duration_ms != create_audio.get("duration_ms"):
            return False
        if len(audio) != duration_ms * 32:
            return False

        sketch_present = upload.create_body.get("sketch", {}).get("present") is True
        sketch_meta = complete_body.get("sketch")
        if not sketch_present:
            return sketch_meta is None and not upload.sketch_parts
        if not isinstance(sketch_meta, dict):
            return False
        sketch = self._assemble_parts(upload.sketch_parts, sketch_meta.get("parts"))
        if sketch is None or len(sketch) % 8 != 0 or not self._valid_sketch_bytes(sketch):
            return False
        return self._matches_media_metadata(sketch, sketch_meta)

    @staticmethod
    def _assemble_parts(parts: dict[int, bytes], expected_parts: Any) -> bytes | None:
        if isinstance(expected_parts, bool) or not isinstance(expected_parts, int):
            return None
        if expected_parts <= 0 or len(parts) != expected_parts or sorted(parts) != list(range(expected_parts)):
            return None
        assembled: list[bytes] = []
        for sequence in range(expected_parts):
            chunk = parts.get(sequence)
            if not isinstance(chunk, bytes) or not 1 <= len(chunk) <= PART_SIZE:
                return None
            if sequence < expected_parts - 1 and len(chunk) != PART_SIZE:
                return None
            assembled.append(chunk)
        return b"".join(assembled)

    @staticmethod
    def _matches_media_metadata(data: bytes, metadata: dict) -> bool:
        byte_count = metadata.get("byte_count")
        digest = metadata.get("sha256")
        return (
            isinstance(byte_count, int)
            and not isinstance(byte_count, bool)
            and byte_count == len(data)
            and isinstance(digest, str)
            and re.fullmatch(r"[0-9a-f]{64}", digest) is not None
            and hashlib.sha256(data).hexdigest() == digest
        )

    @staticmethod
    def _valid_sketch_bytes(data: bytes) -> bool:
        previous_t = -1
        for offset in range(0, len(data), 8):
            x, y = data[offset], data[offset + 1]
            timestamp = struct.unpack_from("<I", data, offset + 4)[0]
            if x > 119 or y > 119 or timestamp < previous_t:
                return False
            previous_t = timestamp
        return True

    def do_PUT(self):
        parsed = urlsplit(self.path)
        path = parsed.path
        if path == "/v2/inbox/last-viewed":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            body = self._read_json(4096)
            if body is None or body.get("schema") != "fl.last-viewed-update/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = str(body.get("operation_id", ""))
            message_id = str(body.get("message_id", ""))
            if not UUID_RE.fullmatch(operation_id) or not message_id:
                self._error(400, "invalid_request", False, operation_id)
                return
            with STORE.lock:
                prior = STORE.last_viewed_ops.get(operation_id)
                if prior is not None:
                    if prior.get("user_id") != user:
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                    if prior.get("message_id") != message_id:
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                    if time.time() - float(prior["accepted_at"]) > LAST_VIEWED_RETENTION_S:
                        self._error(410, "operation_expired", False, operation_id)
                        return
                    self._json(200, prior["result"])
                    return
                snapshot = _inbox_for_user_locked(user)
                if not any(item.get("message_id") == message_id for item in snapshot["items"]):
                    self._error(404, "not_found", False, operation_id)
                    return
                result = {
                    "schema": "fl.last-viewed/2",
                    "operation_id": operation_id,
                    "last_viewed_message_id": message_id,
                }
                next_values = {**STORE.last_viewed, user: message_id}
                next_operations = dict(STORE.last_viewed_ops)
                next_operations[operation_id] = {
                    "user_id": user,
                    "message_id": message_id,
                    "result": result,
                    "accepted_at": time.time(),
                }
                try:
                    _save_last_viewed_state(next_values, next_operations)
                except OSError:
                    self._error(500, "storage_unavailable", True, operation_id)
                    return
                STORE.last_viewed = next_values
                STORE.last_viewed_ops = next_operations
                snapshot["last_viewed_message_id"] = message_id
            self._json(200, result)
            return
        part_match = re.fullmatch(r"/v2/outgoing/([^/]+)/parts/(audio|sketch)/(\d+)", path)
        if part_match:
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            message_id, kind, sequence_s = part_match.group(1), part_match.group(2), part_match.group(3)
            try:
                sequence = int(sequence_s)
            except ValueError:
                self._error(400, "invalid_request", False)
                return
            length = int(self.headers.get("Content-Length", "0") or "0")
            if length < 0 or length > PART_SIZE:
                self._error(400, "invalid_request", False)
                return
            if length == 0:
                self._error(400, "invalid_request", False)
                return
            chunk = self.rfile.read(length)
            if len(chunk) != length:
                self._error(400, "invalid_request", False)
                return
            with STORE.lock:
                upload = None
                for open_upload in STORE.opens.values():
                    if open_upload.message_id == message_id and open_upload.sender_user_id == user:
                        upload = open_upload
                        break
                if upload is None:
                    self._error(404, "not_found", False)
                    return
                parts = upload.audio_parts if kind == "audio" else upload.sketch_parts
                if sequence in parts and parts[sequence] != chunk:
                    self._error(409, "identity_conflict", False)
                    return
                parts[sequence] = chunk
                upload.last_part_at = time.time()
            self._empty_json(200)
            self.log_message("part status=%s message=%s kind=%s seq=%s", "200", message_id, kind, sequence)
            return
        self._error(404, "not_found", False)


def create_server(bind: str, port: int, run_id: str, log_path: str | None = None,
                  reload_tombstones: bool = False, tls_cert: str | None = None,
                  tls_key: str | None = None, production: bool = False) -> LabServer:
    if bool(tls_cert) != bool(tls_key):
        raise ValueError("--tls-cert and --tls-key must be supplied together")
    if production and not (tls_cert and tls_key):
        raise ValueError("production mode requires --tls-cert and --tls-key")
    if reload_tombstones:
        reload_persisted_tombstones()
    log = BoundedRedactingLog(log_path) if log_path else None
    httpd = LabServer((bind, port), Handler, run_id, log, tls_enabled=bool(tls_cert))
    if tls_cert and tls_key:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(certfile=tls_cert, keyfile=tls_key)
        httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
    return httpd


def server_identity(root: Path) -> dict:
    manifest_path = root / "server/device/fixtures/manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    return {
        "revision": SERVER_REVISION,
        "fixture_sha256": hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        "fixture_version": manifest["fixture_version"],
        "contracts": manifest.get("contracts", {}),
    }


def parse_flsk2(data: bytes) -> list[tuple[int, int, int, int]]:
    points = []
    if len(data) % 8 != 0:
        raise ValueError("flsk2 length")
    for offset in range(0, len(data), 8):
        x, y, color, t_ms = struct.unpack_from("<BBHI", data, offset)
        points.append((x, y, color, t_ms))
    return points


def replay_pixels(points: list[tuple[int, int, int, int]]) -> set[tuple[int, int]]:
    return {(x, y) for x, y, _, _ in points}


def chebyshev(a: tuple[int, int], b: tuple[int, int]) -> int:
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=int(MANIFEST.get("default_port", 8777)))
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--log", default="")
    parser.add_argument("--tls-cert", default="",
                        help="PEM server certificate; required with --tls-key")
    parser.add_argument("--tls-key", default="",
                        help="PEM private key; required with --tls-cert")
    parser.add_argument("--production", action="store_true",
                        help="require TLS before serving (for deployed production use)")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", args.run_id):
        parser.error("unsafe run ID")
    try:
        httpd = create_server(
            args.bind, args.port, args.run_id, args.log or None,
            tls_cert=args.tls_cert or None, tls_key=args.tls_key or None,
            production=args.production,
        )
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    try:
        httpd.serve_forever(poll_interval=0.1)
    finally:
        if httpd.log is not None:
            httpd.log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
