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
import struct
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
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


@dataclass
class PinRecord:
    fingerprint: str
    body: dict
    accepted_at: float


@dataclass
class OpenUpload:
    client_message_id: str
    message_id: str
    sender_user_id: str
    create_body: dict
    audio_parts: dict[int, bytes] = field(default_factory=dict)
    sketch_parts: dict[int, bytes] = field(default_factory=dict)
    last_part_at: float = 0.0


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


class Store:
    def __init__(self, tombstones: list[dict] | None = None) -> None:
        self.lock = threading.Lock()
        self.last_successful_beat_at: float | None = None
        self.drop_next_health_ack = 0
        self.oversized_inbox_once = 0
        self.pin_records: dict[str, PinRecord] = {}
        self.invalidated_operations: set[str] = set()
        self.sessions: dict[str, dict] = {}
        self.opens: dict[str, OpenUpload] = {}
        self.accepted: dict[str, AcceptedSend] = {}
        # Runtime delivery state.  The fixture remains the seed inbox for Maya;
        # accepted sends are published into these per-user snapshots and media
        # stores instead of disappearing at complete time.
        self.inboxes: dict[str, dict] = {}
        self.media: dict[str, dict[str, bytes]] = {}
        self.last_viewed, self.last_viewed_ops = _load_last_viewed_state()
        if tombstones:
            for row in tombstones:
                self.accepted[row["client_message_id"]] = AcceptedSend(**row)

    def reset_runtime(self) -> None:
        with self.lock:
            self.last_successful_beat_at = None
            self.drop_next_health_ack = 0
            self.oversized_inbox_once = 0
            self.pin_records.clear()
            self.invalidated_operations.clear()
            self.sessions.clear()
            self.opens.clear()
            self.inboxes.clear()
            self.media.clear()
            self.last_viewed, self.last_viewed_ops = _load_last_viewed_state()
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


def _build_accepted_send_locked(upload: OpenUpload, complete_body: dict,
                                accepted_at: str) -> AcceptedSend | None:
    """Build a complete delivery record without changing visible state."""
    recipients = upload.create_body.get("recipient_user_ids")
    if not isinstance(recipients, list) or not recipients or len(recipients) > 7:
        return None
    if len(set(recipients)) != len(recipients) or upload.sender_user_id in recipients:
        return None
    if any(_roster_member(str(user_id)) is None for user_id in recipients):
        return None

    audio_meta = complete_body.get("audio") or {}
    audio = b"".join(upload.audio_parts[index] for index in sorted(upload.audio_parts))
    sketch_present = bool(upload.create_body.get("sketch", {}).get("present"))
    sketch = b"".join(upload.sketch_parts[index] for index in sorted(upload.sketch_parts))
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

    # Keep the accepted delivery alongside the durable receipt so a server
    # restart does not turn a delivered message back into a receipt-only row.
    accepted.delivery_item = item
    accepted.audio_b64 = base64.b64encode(audio).decode("ascii")
    accepted.sketch_b64 = base64.b64encode(sketch).decode("ascii") if sketch_present else None

    return accepted


def _apply_accepted_delivery_locked(accepted: AcceptedSend) -> None:
    if not accepted.delivery_item or not accepted.audio_b64:
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
    STORE.media[accepted.message_id] = {
        "audio": base64.b64decode(accepted.audio_b64),
    }
    if accepted.sketch_b64:
        STORE.media[accepted.message_id]["sketch"] = base64.b64decode(accepted.sketch_b64)


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
            STORE.accepted[row["client_message_id"]] = AcceptedSend(**row)
    _restore_persisted_deliveries()


class LabServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, address, handler, run_id: str, log: BoundedRedactingLog | None):
        super().__init__(address, handler)
        self.run_id = run_id
        self.log = log


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

    def _session_user(self) -> tuple[str | None, str]:
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            return None, "missing"
        token = header[7:].strip()
        with STORE.lock:
            record = STORE.sessions.get(token)
        if record is None:
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
        with STORE.lock:
            self._store_session_locked(token, user_id, operation_id, now)
        return token

    @staticmethod
    def _store_session_locked(token: str, user_id: str, operation_id: str, now: float) -> None:
        STORE.sessions[token] = {
            "user_id": user_id,
            "operation_id": operation_id,
            "issued_at": now,
            "last_refresh_at": now,
            "revoked": False,
        }

    def _pin_fingerprint(self, body: dict) -> str:
        return json.dumps(
            {
                "schema": body.get("schema"),
                "operation_id": body.get("operation_id"),
                "user_id": body.get("user_id"),
                "pin": body.get("pin"),
            },
            sort_keys=True,
            separators=(",", ":"),
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
            if self._desk_token() is None:
                self._error(401, "unauthenticated", False)
                return
            payload = self._read_json(4096)
            if payload is None or payload.get("schema") != "fl.endpoint-health/2":
                self._error(400, "unsupported_schema", False)
                return
            with STORE.lock:
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
            if self._desk_token() is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None or body.get("schema") != "fl.session-login/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = str(body.get("operation_id", ""))
            if not UUID_RE.fullmatch(operation_id):
                self._error(400, "invalid_request", False, operation_id)
                return
            fingerprint = self._pin_fingerprint(body)
            with STORE.lock:
                now = time.time()
                existing = STORE.pin_records.get(operation_id)
                if existing is not None:
                    if existing.fingerprint != fingerprint:
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                    if now - existing.accepted_at > PIN_RETENTION_S:
                        self._error(410, "operation_expired", False, operation_id)
                        return
                    self._json(200, existing.body)
                    self.log_message("pin status=%s mode=%s", "200", "replay")
                    return
                if operation_id in STORE.invalidated_operations:
                    result = {
                        "schema": "fl.session-login-result/2",
                        "operation_id": operation_id,
                        "outcome": "cancelled",
                    }
                else:
                    user_id = str(body.get("user_id", ""))
                    pin = str(body.get("pin", ""))
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
                        self._store_session_locked(session, user_id, operation_id, now)
                        result = {
                            "schema": "fl.session-login-result/2",
                            "operation_id": operation_id,
                            "outcome": "authenticated",
                            "user_id": user_id,
                            "session": session,
                        }
                STORE.pin_records[operation_id] = PinRecord(fingerprint, result, now)
            self._json(200, result)
            self.log_message("pin status=%s outcome=%s", "200", result["outcome"])
            return
        if path == "/v2/session/invalidate":
            if self._desk_token() is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None or body.get("schema") != "fl.session-invalidate/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = str(body.get("operation_id", ""))
            session_token = str(body.get("session", "") or "")
            with STORE.lock:
                if operation_id:
                    STORE.invalidated_operations.add(operation_id)
                for token, record in list(STORE.sessions.items()):
                    if record.get("operation_id") == operation_id or (session_token and token == session_token):
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
                record["last_refresh_at"] = time.time()
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
                  reload_tombstones: bool = False) -> LabServer:
    if reload_tombstones:
        reload_persisted_tombstones()
    log = BoundedRedactingLog(log_path) if log_path else None
    return LabServer((bind, port), Handler, run_id, log)


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
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", args.run_id):
        parser.error("unsafe run ID")
    httpd = create_server(args.bind, args.port, args.run_id, args.log or None)
    try:
        httpd.serve_forever(poll_interval=0.1)
    finally:
        if httpd.log is not None:
            httpd.log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
