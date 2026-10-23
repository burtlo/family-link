#!/usr/bin/env python3
"""P1 core-journey wire fixture (product contract revisions, not a device lab schema)."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import secrets
import struct
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from server_log import BoundedRedactingLog, redact_line  # noqa: E402

SERVER_DIR = Path(__file__).resolve().parent
FIXTURE_DIR = SERVER_DIR / "fixtures"
MANIFEST_PATH = FIXTURE_DIR / "manifest.json"
SERVER_MANIFEST = SERVER_DIR / "manifest.lab.json"
FIXTURE_NAME = "p1-core-journey/v1"
PART_SIZE = 16384
INBOX_MAX_BYTES = 262144
PIN_RETENTION_S = 60
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


MANIFEST = verify_fixture_tree()
MANIFEST_BYTES = MANIFEST_PATH.read_bytes()
FIXTURE_SHA256 = hashlib.sha256(MANIFEST_BYTES).hexdigest()
SERVER_REVISION = json.loads(SERVER_MANIFEST.read_text(encoding="utf-8"))["lab_revision"]
ACCOUNTS = json.loads((FIXTURE_DIR / "accounts.json").read_text(encoding="utf-8"))
ROSTER = json.loads((FIXTURE_DIR / "roster.json").read_text(encoding="utf-8"))
INBOX_MAYA = json.loads((FIXTURE_DIR / "inbox-maya.json").read_text(encoding="utf-8"))
MEDIA_AUDIO = (FIXTURE_DIR / "media-audio.pcm").read_bytes()
MEDIA_SKETCH = (FIXTURE_DIR / "media-sketch.flsk2").read_bytes()
RUNTIME_DIR = SERVER_DIR / "runtime"
TOMBSTONE_PATH = RUNTIME_DIR / "accepted-tombstones.json"


def _load_tombstones() -> list[dict]:
    if TOMBSTONE_PATH.is_file():
        return json.loads(TOMBSTONE_PATH.read_text(encoding="utf-8")).get("records", [])
    seed = FIXTURE_DIR / "accepted-tombstones.json"
    return json.loads(seed.read_text(encoding="utf-8")).get("records", [])


def _save_tombstones(records: list[dict]) -> None:
    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    payload = {"schema": "p1-accepted-tombstones/v1", "records": records}
    TOMBSTONE_PATH.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


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


class Store:
    def __init__(self, tombstones: list[dict] | None = None) -> None:
        self.lock = threading.Lock()
        self.last_successful_beat_at: float | None = None
        self.drop_next_health_ack = 0
        self.oversized_inbox_once = 0
        self.pin_records: dict[str, PinRecord] = {}
        self.sessions: dict[str, dict] = {}
        self.opens: dict[str, OpenUpload] = {}
        self.accepted: dict[str, AcceptedSend] = {}
        self.last_viewed: dict[str, str] = {}
        self.last_viewed_ops: dict[str, dict] = {}
        if tombstones:
            for row in tombstones:
                self.accepted[row["client_message_id"]] = AcceptedSend(**row)

    def reset_runtime(self) -> None:
        with self.lock:
            self.last_successful_beat_at = None
            self.drop_next_health_ack = 0
            self.oversized_inbox_once = 0
            self.pin_records.clear()
            self.sessions.clear()
            self.opens.clear()
            # accepted tombstones intentionally persist across runtime reset

    def observe_health_clock(self) -> dict:
        with self.lock:
            return {
                "last_successful_beat_at": self.last_successful_beat_at,
                "fixture": FIXTURE_NAME,
            }


STORE = Store(_load_tombstones())


def reload_persisted_tombstones() -> None:
    with STORE.lock:
        for row in _load_tombstones():
            STORE.accepted[row["client_message_id"]] = AcceptedSend(**row)


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
        return str(record["user_id"]), "ok"

    def _issue_session(self, user_id: str, operation_id: str) -> str:
        token = _b64url_token(secrets.token_bytes(32))
        with STORE.lock:
            STORE.sessions[token] = {
                "user_id": user_id,
                "operation_id": operation_id,
                "issued_at": time.time(),
                "revoked": False,
            }
        return token

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
                self._error(401, "unauthenticated", False)
                return
            with STORE.lock:
                if STORE.oversized_inbox_once > 0:
                    STORE.oversized_inbox_once -= 1
                    huge = json.dumps({"schema": "fl.inbox-page/2", "pad": "x" * (INBOX_MAX_BYTES + 1024)})
                    self._send_raw(200, huge.encode("utf-8"))
                    self.log_message("inbox status=%s mode=%s", "200", "oversized")
                    return
            if user != "user-maya":
                empty = {
                    "schema": "fl.inbox-page/2",
                    "inbox_revision": 0,
                    "total_count": 0,
                    "last_viewed_message_id": None,
                    "items": [],
                    "next_cursor": None,
                    "has_more": False,
                }
                self._json(200, empty)
                return
            self._json(200, INBOX_MAYA)
            self.log_message("inbox status=%s revision=%s", "200", INBOX_MAYA["inbox_revision"])
            return
        media_match = re.fullmatch(r"/v2/messages/([^/]+)/media/(audio|sketch)", path)
        if media_match:
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._error(401, "unauthenticated", False)
                return
            message_id, kind = media_match.group(1), media_match.group(2)
            if user != "user-maya" or message_id != INBOX_MAYA["items"][0]["message_id"]:
                self._error(404, "not_found", False)
                return
            body = MEDIA_AUDIO if kind == "audio" else MEDIA_SKETCH
            self._send_raw(200, body, "application/octet-stream")
            self.log_message("media status=%s message=%s kind=%s", "200", message_id, kind)
            return
        receipt_match = re.fullmatch(r"/v2/outgoing/by-client-id/([^/]+)", path)
        if receipt_match:
            client_id = receipt_match.group(1)
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._error(401, "unauthenticated", False)
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
            body = self._read_json(4096) or {}
            with STORE.lock:
                if body.get("reset_runtime"):
                    STORE.reset_runtime()
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
            now = time.time()
            with STORE.lock:
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
                session = self._issue_session(user_id, operation_id)
                result = {
                    "schema": "fl.session-login-result/2",
                    "operation_id": operation_id,
                    "outcome": "authenticated",
                    "user_id": user_id,
                    "session": session,
                }
            with STORE.lock:
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
                self._error(401, "unauthenticated", False)
                return
            refresh_body = self._read_json(64)
            if refresh_body is None:
                self._error(400, "invalid_request", False)
                return
            if refresh_body != {}:
                self._error(400, "invalid_request", False)
                return
            self._empty_json(200)
            return
        if path == "/v2/outgoing":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._error(401, "unauthenticated", False)
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
                    self._json(200, self._receipt_body(accepted))
                    return
                open_upload = STORE.opens.get(client_id)
                if open_upload is not None:
                    if open_upload.sender_user_id != user:
                        self._error(403, "forbidden", False)
                        return
                    if json.dumps(open_upload.create_body, sort_keys=True) != json.dumps(body, sort_keys=True):
                        self._error(409, "identity_conflict", False)
                        return
                    self._json(200, self._open_body(open_upload))
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
                self._error(401, "unauthenticated", False)
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
                    self._json(200, self._receipt_body(accepted))
                    return
                upload = STORE.opens.get(client_id)
                if upload is None or upload.message_id != message_id or upload.sender_user_id != user:
                    self._error(404, "not_found", False)
                    return
                if not self._upload_complete(upload, body):
                    self._error(400, "invalid_request", False)
                    return
                accepted_row = AcceptedSend(
                    client_message_id=client_id,
                    message_id=message_id,
                    sender_user_id=user,
                    accepted_at=_utc_now(),
                    recipient_user_ids=list(upload.create_body.get("recipient_user_ids", [])),
                )
                STORE.accepted[client_id] = accepted_row
                STORE.opens.pop(client_id, None)
                records = [row.__dict__ for row in STORE.accepted.values()]
            _save_tombstones(records)
            self._json(200, self._receipt_body(accepted_row))
            return
        self._error(404, "not_found", False)

    def _upload_complete(self, upload: OpenUpload, complete_body: dict) -> bool:
        audio_meta = complete_body.get("audio") or {}
        expected_parts = int(audio_meta.get("parts", 0))
        if expected_parts != len(upload.audio_parts):
            return False
        if upload.create_body.get("sketch", {}).get("present"):
            sketch_meta = complete_body.get("sketch") or {}
            if int(sketch_meta.get("parts", 0)) != len(upload.sketch_parts):
                return False
        return True

    def do_PUT(self):
        parsed = urlsplit(self.path)
        path = parsed.path
        if path == "/v2/inbox/last-viewed":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None or body.get("schema") != "fl.last-viewed-update/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = str(body.get("operation_id", ""))
            message_id = str(body.get("message_id", ""))
            with STORE.lock:
                prior = STORE.last_viewed_ops.get(operation_id)
                if prior is not None:
                    if prior.get("message_id") != message_id:
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                    self._json(200, prior["result"])
                    return
                STORE.last_viewed[user] = message_id
                result = {
                    "schema": "fl.last-viewed/2",
                    "operation_id": operation_id,
                    "last_viewed_message_id": message_id,
                }
                STORE.last_viewed_ops[operation_id] = {"message_id": message_id, "result": result}
            self._json(200, result)
            return
        part_match = re.fullmatch(r"/v2/outgoing/([^/]+)/parts/(audio|sketch)/(\d+)", path)
        if part_match:
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._error(401, "unauthenticated", False)
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
            chunk = self.rfile.read(length) if length else b""
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
    manifest = json.loads((root / "server/labs/lab-p1-core-journey-wire/fixtures/manifest.json").read_text(encoding="utf-8"))
    manifest_path = root / "server/labs/lab-p1-core-journey-wire/fixtures/manifest.json"
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
