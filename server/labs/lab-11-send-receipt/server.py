#!/usr/bin/env python3
"""Synthetic LAB-11 send and receipt fixture. No household data and no raw payload logs."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from server_log import BoundedRedactingLog, redact_line  # noqa: E402

SERVER_DIR = Path(__file__).resolve().parent
FIXTURE_DIR = SERVER_DIR / "fixtures"
MANIFEST_PATH = FIXTURE_DIR / "manifest.json"
SERVER_MANIFEST = SERVER_DIR / "manifest.lab.json"
MESSAGE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,38}$")


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


MANIFEST = verify_fixture_tree()
FIXTURE_SHA256 = hashlib.sha256(MANIFEST_PATH.read_bytes()).hexdigest()
SERVER_REVISION = json.loads(SERVER_MANIFEST.read_text(encoding="utf-8"))["lab_revision"]
RESTART = json.loads((FIXTURE_DIR / "restart-probe.json").read_text(encoding="utf-8"))


class Store:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.reset()

    def reset(self) -> None:
        self.parts: dict[tuple[str, int], str] = {}
        self.accepted: dict[str, int] = {}
        self.message_count: dict[str, int] = {}
        self.complete_posts: dict[str, int] = {}
        self.receipts: dict[str, str] = {}
        self.recipients: dict[str, int] = {}
        self.push: dict[str, str] = {}
        self.drop_next_acks = 0
        self.drop_next_receipt = False
        self.storage_full = False
        self.push_fail = False
        self.invalid_recipient = False


STORE = Store()


def _durable(message_id: str) -> int:
    return sum(1 for key in STORE.parts if key[0] == message_id)


def _contiguous(message_id: str, part_count: int) -> bool:
    return all((message_id, index) in STORE.parts for index in range(part_count))


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
        line = redact_line((fmt % args).encode("utf-8", errors="replace"))
        sys.stdout.buffer.write(line + b"\n")
        sys.stdout.buffer.flush()
        if self.server.log is not None:
            self.server.log.feed("stdout", line + b"\n")

    def _send(self, status: int, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _json(self, status: int, payload: dict) -> None:
        self._send(status, json.dumps(payload, separators=(",", ":")).encode("utf-8"))

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length < 0 or length > 1024:
            return {}
        raw = self.rfile.read(length) if length else b""
        if not raw:
            return {}
        try:
            value = json.loads(raw.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError):
            return {}
        return value if isinstance(value, dict) else {}

    def _message_id(self, value: str) -> str:
        if not MESSAGE_ID.fullmatch(value or ""):
            return ""
        return value

    def do_GET(self):
        parsed = urlsplit(self.path)
        if parsed.path == "/health":
            self._json(200, {
                "run_id": self.server.run_id,
                "fixture_version": MANIFEST["fixture_version"],
                "fixture_sha256": FIXTURE_SHA256,
                "server_version": MANIFEST["server_version"],
            })
            self.log_message("health status=%s", "200")
            return
        if parsed.path == "/v1/status":
            resumed = ",".join(str(item) for item in RESTART.get("resumed_sequence_ids", []))
            self._json(200, {
                "result": "status",
                "status": RESTART.get("status", "unknown"),
                "resumed_sequences": resumed,
                "accepted": 0,
            })
            self.log_message("status status=%s", "200")
            return
        if parsed.path.startswith("/v1/receipts/"):
            message_id = self._message_id(parsed.path.rsplit("/", 1)[-1])
            with STORE.lock:
                accepted = STORE.accepted.get(message_id, 0)
                payload = {
                    "result": "receipt" if accepted else "absent",
                    "accepted": accepted,
                    "message_count": STORE.message_count.get(message_id, 0),
                    "durable_parts": _durable(message_id),
                    "receipt_id": STORE.receipts.get(message_id, ""),
                    "complete_posts": STORE.complete_posts.get(message_id, 0),
                }
            self._json(200, payload)
            self.log_message("receipt message=%s accepted=%s", message_id, payload["accepted"])
            return
        if parsed.path == "/v1/reconcile":
            query = parsed.query
            message_id = ""
            for piece in query.split("&"):
                if piece.startswith("message_id="):
                    message_id = self._message_id(piece.split("=", 1)[1])
            with STORE.lock:
                payload = {
                    "result": "reconcile",
                    "accepted": STORE.accepted.get(message_id, 0),
                    "durable_parts": _durable(message_id),
                    "message_count": STORE.message_count.get(message_id, 0),
                    "complete_posts": STORE.complete_posts.get(message_id, 0),
                    "receipt_id": STORE.receipts.get(message_id, ""),
                    "recipient_count": STORE.recipients.get(message_id, 0),
                    "push_result": STORE.push.get(message_id, "none"),
                }
            self._json(200, payload)
            self.log_message("reconcile message=%s accepted=%s", message_id, payload["accepted"])
            return
        self._json(404, {"result": "absent"})

    def do_POST(self):
        parsed = urlsplit(self.path)
        if parsed.path == "/v1/control":
            body = self._read_json()
            with STORE.lock:
                if body.get("reset"):
                    STORE.reset()
                if "drop_next_acks" in body:
                    STORE.drop_next_acks = int(body.get("drop_next_acks") or 0)
                if body.get("drop_next_receipt"):
                    STORE.drop_next_receipt = True
                if body.get("storage_full"):
                    STORE.storage_full = True
                if body.get("push_fail"):
                    STORE.push_fail = True
                if body.get("invalid_recipient"):
                    STORE.invalid_recipient = True
            self._json(200, {"result": "ok"})
            self.log_message("control status=%s", "200")
            return
        if parsed.path == "/v1/parts":
            self._part()
            return
        if parsed.path == "/v1/complete":
            self._complete()
            return
        if parsed.path == "/v1/publish":
            self._publish()
            return
        if parsed.path == "/v1/push":
            self._push()
            return
        self._json(404, {"result": "absent"})

    def _part(self) -> None:
        message_id = self._message_id(self.headers.get("X-Client-Message-Id", ""))
        try:
            sequence = int(self.headers.get("X-Sequence", "-1"))
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._json(400, {"result": "bad_request"})
            return
        if not message_id or sequence < 0 or length < 0 or length > 4096:
            self._json(400, {"result": "bad_request"})
            self.log_message("part rejected sequence=%s", sequence)
            return
        body = self.rfile.read(length) if length else b""
        digest = hashlib.sha256(body).hexdigest()
        header = (self.headers.get("X-Content-SHA256", "") or "").lower()
        if header != digest:
            self._json(400, {"result": "bad_request"})
            self.log_message("part hash_mismatch sequence=%s", sequence)
            return
        with STORE.lock:
            if STORE.storage_full:
                self._json(507, {"result": "storage", "accepted": 0, "durable_parts": _durable(message_id)})
                self.log_message("part storage sequence=%s", sequence)
                return
            key = (message_id, sequence)
            current = STORE.parts.get(key)
            if current is not None and current != digest:
                self._json(409, {
                    "result": "identity_conflict",
                    "sequence": sequence,
                    "durable_parts": _durable(message_id),
                    "accepted": STORE.accepted.get(message_id, 0),
                })
                self.log_message("part identity_conflict sequence=%s", sequence)
                return
            if current is None:
                STORE.parts[key] = digest
            if STORE.drop_next_acks > 0:
                STORE.drop_next_acks -= 1
                self._send(204, b"")
                self.log_message("part dropped_ack sequence=%s", sequence)
                return
            payload = {
                "result": "acked",
                "sequence": sequence,
                "durable_parts": _durable(message_id),
                "accepted": STORE.accepted.get(message_id, 0),
                "message_count": STORE.message_count.get(message_id, 0),
            }
        self._json(200, payload)
        self.log_message("part acked sequence=%s durable=%s", sequence, payload["durable_parts"])

    def _accept(self, message_id: str, part_count: int) -> None:
        if STORE.accepted.get(message_id, 0) == 1:
            return
        if not _contiguous(message_id, part_count):
            return
        STORE.accepted[message_id] = 1
        STORE.message_count[message_id] = 1
        STORE.receipts[message_id] = message_id

    def _complete(self) -> None:
        body = self._read_json()
        message_id = self._message_id(str(body.get("client_message_id", "")))
        try:
            part_count = int(body.get("part_count", 0))
        except (TypeError, ValueError):
            part_count = 0
        condition = str(body.get("condition", ""))
        if not message_id:
            self._json(400, {"result": "bad_request"})
            return
        with STORE.lock:
            STORE.complete_posts[message_id] = STORE.complete_posts.get(message_id, 0) + 1
            if STORE.storage_full:
                self._json(507, {"result": "storage", "accepted": 0, "durable_parts": _durable(message_id)})
                return
            if STORE.invalid_recipient:
                self._json(200, {
                    "result": "invalid_recipient",
                    "accepted": 0,
                    "durable_parts": _durable(message_id),
                    "message_count": 0,
                    "complete_posts": STORE.complete_posts[message_id],
                })
                self.log_message("complete invalid message=%s", message_id)
                return
            if condition == "accept-on-recipient-publish":
                payload = {
                    "result": "archived",
                    "accepted": 0,
                    "message_count": 0,
                    "durable_parts": _durable(message_id),
                    "receipt_id": "",
                    "complete_posts": STORE.complete_posts[message_id],
                }
            else:
                self._accept(message_id, part_count)
                payload = {
                    "result": "accepted" if STORE.accepted.get(message_id, 0) == 1 else "incomplete",
                    "accepted": STORE.accepted.get(message_id, 0),
                    "message_count": STORE.message_count.get(message_id, 0),
                    "durable_parts": _durable(message_id),
                    "receipt_id": STORE.receipts.get(message_id, ""),
                    "complete_posts": STORE.complete_posts[message_id],
                }
            if STORE.drop_next_receipt and payload["accepted"] == 1:
                STORE.drop_next_receipt = False
                self._send(204, b"")
                self.log_message("complete dropped_receipt message=%s", message_id)
                return
        self._json(200, payload)
        self.log_message("complete message=%s accepted=%s", message_id, payload["accepted"])

    def _publish(self) -> None:
        body = self._read_json()
        message_id = self._message_id(str(body.get("client_message_id", "")))
        try:
            recipients = int(body.get("recipient_count", 0))
        except (TypeError, ValueError):
            recipients = 0
        if not message_id:
            self._json(400, {"result": "bad_request"})
            return
        with STORE.lock:
            if STORE.invalid_recipient:
                self._json(200, {"result": "invalid_recipient", "accepted": 0, "message_count": 0})
                return
            STORE.recipients[message_id] = recipients
            part_count = _durable(message_id)
            self._accept(message_id, part_count)
            accepted = STORE.accepted.get(message_id, 0)
            payload = {
                "result": "accepted" if accepted == 1 else "incomplete",
                "accepted": accepted,
                "message_count": STORE.message_count.get(message_id, 0),
                "durable_parts": part_count,
                "receipt_id": STORE.receipts.get(message_id, ""),
                "recipient_count": recipients,
                "complete_posts": STORE.complete_posts.get(message_id, 0),
            }
        self._json(200, payload)
        self.log_message("publish message=%s accepted=%s recipients=%s", message_id, payload["accepted"], recipients)

    def _push(self) -> None:
        body = self._read_json()
        message_id = self._message_id(str(body.get("client_message_id", "")))
        with STORE.lock:
            accepted = STORE.accepted.get(message_id, 0)
            if STORE.push_fail:
                STORE.push[message_id] = "failed"
                result = "push_failed"
                push_result = "failed"
            else:
                STORE.push[message_id] = "pushed"
                result = "pushed"
                push_result = "pushed"
            payload = {
                "result": result,
                "push_result": push_result,
                "accepted": accepted,
                "message_count": STORE.message_count.get(message_id, 0),
            }
        self._json(200, payload)
        self.log_message("push message=%s result=%s accepted=%s", message_id, push_result, accepted)


def create_server(bind: str, port: int, run_id: str, log_path: str | None = None) -> LabServer:
    log = BoundedRedactingLog(log_path) if log_path else None
    return LabServer((bind, port), Handler, run_id, log)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8770)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--log", default="")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", args.run_id):
        parser.error("unsafe run ID")
    server = create_server(args.bind, args.port, args.run_id, args.log or None)
    try:
        server.serve_forever(poll_interval=0.1)
    finally:
        if server.log is not None:
            server.log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
