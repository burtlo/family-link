#!/usr/bin/env python3
"""Synthetic LAB-09 session fixture. Not a product session or token format."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import secrets
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from server_log import BoundedRedactingLog, redact_line  # noqa: E402

SERVER_DIR = Path(__file__).resolve().parent
FIXTURE_DIR = SERVER_DIR / "fixtures"
MANIFEST_PATH = FIXTURE_DIR / "manifest.json"
SERVER_MANIFEST = SERVER_DIR / "manifest.lab.json"
FIXTURE_NAME = "lab09-session/v1"


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
MANIFEST_BYTES = MANIFEST_PATH.read_bytes()
FIXTURE_SHA256 = hashlib.sha256(MANIFEST_BYTES).hexdigest()
SERVER_REVISION = json.loads(SERVER_MANIFEST.read_text(encoding="utf-8"))["lab_revision"]
USERS = json.loads((FIXTURE_DIR / "users.json").read_text(encoding="utf-8"))
INBOX = json.loads((FIXTURE_DIR / "inbox.json").read_text(encoding="utf-8"))
MALFORMED = (FIXTURE_DIR / "malformed.json").read_bytes()
IMAGE = bytes([0x5A]) * int(USERS["image_bytes"])


def _redact_text(text: str) -> str:
    text = redact_line(text.encode("utf-8")).decode("utf-8", errors="replace")
    text = re.sub(r"(?i)Bearer\s+\S+", "Bearer [REDACTED]", text)
    text = re.sub(r'"(handle|credential|token)"\s*:\s*"[^"]*"', r'"\1":"[REDACTED]"', text)
    return text


class LabServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, address, handler, run_id: str, log: BoundedRedactingLog | None):
        super().__init__(address, handler)
        self.run_id = run_id
        self.log = log
        self.lock = threading.Lock()
        self.handles: dict[str, dict] = {}
        self.counts = {user: 0 for user in USERS["users"]}
        self.revoke_on_mute = 0
        self.leave_server_session = 0


class Handler(BaseHTTPRequestHandler):
    server: LabServer

    def log_request(self, code="-", size="-"):
        return

    def log_message(self, fmt, *args):
        line = _redact_text(fmt % args)
        encoded = line.encode("utf-8", errors="replace")
        sys.stdout.buffer.write(encoded + b"\n")
        sys.stdout.buffer.flush()
        if self.server.log is not None:
            self.server.log.feed("stdout", encoded + b"\n")

    def _send(self, status: int, body: bytes, content_type: str = "application/json") -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Fixture-SHA256", FIXTURE_SHA256)
        self.send_header("X-Run-ID", self.server.run_id)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, status: int, payload: dict) -> None:
        self._send(status, json.dumps(payload, separators=(",", ":")).encode("utf-8"))

    def _read_json(self) -> dict | None:
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length < 0 or length > 4096:
            return None
        raw = self.rfile.read(length) if length else b""
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError):
            return None
        if not isinstance(payload, dict):
            return None
        return payload

    def _session(self) -> tuple[str | None, str]:
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            return None, "missing"
        token = header[7:].strip()
        with self.server.lock:
            record = self.server.handles.get(token)
        if record is None:
            return None, "missing"
        if record.get("expired"):
            return None, "expired"
        if record.get("revoked"):
            return None, "revoked"
        return str(record["user"]), "ok"

    def _reject_auth(self, state: str) -> None:
        if state == "expired":
            self._json(401, {"fixture": FIXTURE_NAME, "session": "expired", "ok": False})
            self.log_message("op status=%s reason=%s", "401", "expired")
            return
        self._json(401, {"fixture": FIXTURE_NAME, "ok": False})
        self.log_message("op status=%s reason=%s", "401", state)

    def _count_actor(self, actor: str, payload: dict | None) -> None:
        """The credential selects the actor. A body user id does not."""
        named = ""
        if payload is not None:
            named = str(payload.get("user_id", ""))
        with self.server.lock:
            if actor in self.server.counts:
                self.server.counts[actor] += 1
            if named and named != actor and named in self.server.counts:
                return

    def _operation(self, kind: str) -> None:
        actor, state = self._session()
        if actor is None:
            self._reject_auth(state)
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        payload = None
        if kind == "media":
            if length > 0:
                self.rfile.read(length)
        else:
            payload = self._read_json()
            if payload is None and length > 0:
                self._send(200, MALFORMED, "text/plain")
                self.log_message("op status=%s kind=%s reason=%s", "200", kind, "malformed")
                return
        self._count_actor(actor, payload)
        if kind == "media":
            self._send(200, IMAGE, "application/octet-stream")
            self.log_message("op status=%s kind=%s actor=%s bytes=%s", "200", kind, actor, len(IMAGE))
            return
        body = {"fixture": FIXTURE_NAME, "ok": True, "actor": actor}
        if kind == "inbox":
            body["cards"] = list(INBOX["cards"])
        self._json(200, body)
        self.log_message("op status=%s kind=%s actor=%s", "200", kind, actor)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/health":
            self._json(
                200,
                {
                    "run_id": self.server.run_id,
                    "fixture_version": MANIFEST["fixture_version"],
                    "fixture_sha256": FIXTURE_SHA256,
                    "server_version": MANIFEST["server_version"],
                },
            )
            self.log_message("health status=%s", "200")
            return
        if path == "/observe":
            with self.server.lock:
                payload = {
                    "counts": dict(self.server.counts),
                    "revoke_on_mute": self.server.revoke_on_mute,
                    "leave_server_session": self.server.leave_server_session,
                    "fixture": FIXTURE_NAME,
                }
            self._json(200, payload)
            self.log_message("observe status=%s", "200")
            return
        if path == "/malformed":
            self._send(200, MALFORMED, "text/plain")
            self.log_message("malformed status=%s", "200")
            return
        self._json(404, {})
        self.log_message("unknown status=%s", "404")

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path == "/session/sign-in":
            payload = self._read_json()
            user = "" if payload is None else str(payload.get("user", ""))
            if user not in self.server.counts:
                self._json(400, {"fixture": FIXTURE_NAME, "ok": False})
                self.log_message("sign-in status=%s", "400")
                return
            handle = secrets.token_hex(16)
            with self.server.lock:
                self.server.handles[handle] = {"user": user, "expired": False, "revoked": False}
            self._json(200, {"fixture": FIXTURE_NAME, "ok": True, "actor": user, "handle": handle})
            self.log_message("sign-in status=%s user=%s", "200", user)
            return
        if path in {"/inbox", "/media", "/playback", "/profile"}:
            self._operation(path[1:])
            return
        if path == "/session/expire":
            actor, state = self._session()
            if actor is None and state == "missing":
                self._reject_auth(state)
                return
            header = self.headers.get("Authorization", "")
            token = header[7:].strip() if header.startswith("Bearer ") else ""
            with self.server.lock:
                record = self.server.handles.get(token)
                if record is not None:
                    record["expired"] = True
            self._json(401, {"fixture": FIXTURE_NAME, "session": "expired", "ok": False})
            self.log_message("expire status=%s", "401")
            return
        if path == "/session/mute":
            actor, state = self._session()
            if actor is None:
                self._reject_auth(state)
                return
            payload = self._read_json() or {}
            mode = str(payload.get("mode", ""))
            header = self.headers.get("Authorization", "")
            token = header[7:].strip() if header.startswith("Bearer ") else ""
            with self.server.lock:
                record = self.server.handles.get(token)
                if mode == "revoke-on-mute":
                    if record is not None:
                        record["revoked"] = True
                    self.server.revoke_on_mute += 1
                elif mode == "leave-server-session":
                    self.server.leave_server_session += 1
                else:
                    self._json(400, {"fixture": FIXTURE_NAME, "ok": False})
                    self.log_message("mute status=%s", "400")
                    return
            self._json(200, {"fixture": FIXTURE_NAME, "ok": True, "mode": mode})
            self.log_message("mute status=%s mode=%s", "200", mode)
            return
        self._json(404, {})
        self.log_message("unknown status=%s", "404")


def create_server(bind: str, port: int, run_id: str, log_path: str | None = None) -> LabServer:
    log = BoundedRedactingLog(log_path) if log_path else None
    return LabServer((bind, port), Handler, run_id, log)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8769)
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
