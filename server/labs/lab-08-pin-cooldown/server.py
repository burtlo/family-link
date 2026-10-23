#!/usr/bin/env python3
"""Synthetic LAB-08 PIN fixture. Fixture fields are not a product PIN schema."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
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
            raise IdentityError(
                f"fixture byte change without revision bump: {name} revision {revision}"
            )
    return manifest


MANIFEST = verify_fixture_tree()
MANIFEST_BYTES = MANIFEST_PATH.read_bytes()
FIXTURE_SHA256 = hashlib.sha256(MANIFEST_BYTES).hexdigest()
SERVER_REVISION = json.loads(SERVER_MANIFEST.read_text(encoding="utf-8"))["lab_revision"]
ACCOUNTS = {
    item["id"]: item["pin"]
    for item in json.loads((FIXTURE_DIR / "accounts.json").read_text(encoding="utf-8"))["users"]
}
MALFORMED = (FIXTURE_DIR / "malformed.json").read_bytes()


def _redact_text(text: str) -> str:
    text = redact_line(text.encode("utf-8")).decode("utf-8", errors="replace")
    text = re.sub(r'"pin"\s*:\s*"[^"]*"', '"pin":"[REDACTED]"', text)
    text = re.sub(r"(?i)(pin|password)\s*[=:]\s*\S+", r"\1=[REDACTED]", text)
    return text


class LabServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, address, handler, run_id: str, log: BoundedRedactingLog | None):
        super().__init__(address, handler)
        self.run_id = run_id
        self.log = log
        self.lock = threading.Lock()
        self.side_effects = 0
        self.records: dict[str, dict] = {}
        self.request_counts: dict[str, int] = {}
        self.concurrent_observed = 0
        self.mismatch_status = 0


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

    def _read_json(self) -> dict | None:
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length < 0 or length > 2048:
            return None
        raw = self.rfile.read(length) if length else b""
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError):
            return None
        if not isinstance(payload, dict):
            return None
        return payload

    def _result_body(self, outcome: str) -> bytes:
        # side_effects is the count for this key. A replay returns the stored body.
        payload = {"outcome": outcome, "side_effects": 1, "fixture": "lab08-pin/v1"}
        return json.dumps(payload, separators=(",", ":")).encode("utf-8")

    def _login(self, payload: dict, drop: bool) -> None:
        key = str(payload.get("key", ""))
        user = str(payload.get("user", ""))
        pin = str(payload.get("pin", ""))
        if not re.fullmatch(r"[0-9a-f]{64}", key) or not re.fullmatch(r"[A-Za-z0-9-]{1,16}", user):
            self._send(400, b'{"outcome":"rejected","fixture":"lab08-pin/v1"}')
            self.log_message("pin status=%s reason=%s", "400", "shape")
            return
        canonical = json.dumps({"key": key, "user": user, "pin": pin}, sort_keys=True, separators=(",", ":"))
        with self.server.lock:
            self.server.request_counts[key] = self.server.request_counts.get(key, 0) + 1
            existing = self.server.records.get(key)
            if existing is not None:
                if existing["canonical"] != canonical:
                    self.server.mismatch_status = 409
                    body = json.dumps(
                        {
                            "fixture_status": "mismatch",
                            "side_effects": self.server.side_effects,
                            "fixture": "lab08-pin/v1",
                        },
                        separators=(",", ":"),
                    ).encode("utf-8")
                    self._send(409, body)
                    self.log_message("pin status=%s key=%s duplicate=%s", "409", key[:12], "mismatch")
                    return
                self.log_message("pin status=%s key=%s duplicate=%s", "200", key[:12], "replay")
                if drop:
                    self._send(204, b"")
                    return
                self._send(200, existing["body"])
                return
            if self.server.request_counts[key] > 1:
                self.server.concurrent_observed += 1
            expected = ACCOUNTS.get(user)
            if expected is None:
                outcome = "unknown"
            elif expected == pin:
                outcome = "ok"
            else:
                outcome = "wrong"
            self.server.side_effects += 1
            body = self._result_body(outcome)
            self.server.records[key] = {"canonical": canonical, "body": body, "outcome": outcome}
        self.log_message("pin status=%s key=%s outcome=%s side_effects=%s", "200" if not drop else "204",
                         key[:12], outcome, self.server.side_effects)
        if drop:
            self._send(204, b"")
            return
        self._send(200, body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/health":
            payload = {
                "run_id": self.server.run_id,
                "fixture_version": MANIFEST["fixture_version"],
                "fixture_sha256": FIXTURE_SHA256,
                "server_version": MANIFEST["server_version"],
            }
            self._send(200, json.dumps(payload).encode("utf-8"))
            self.log_message("health status=%s", "200")
            return
        if path == "/observe":
            with self.server.lock:
                payload = {
                    "side_effects": self.server.side_effects,
                    "keys": len(self.server.records),
                    "concurrent_observed": self.server.concurrent_observed,
                    "mismatch_status": self.server.mismatch_status,
                    "fixture": "lab08-pin/v1",
                }
            self._send(200, json.dumps(payload).encode("utf-8"))
            self.log_message("observe status=%s side_effects=%s", "200", payload["side_effects"])
            return
        if path == "/malformed":
            self._send(200, MALFORMED, "text/plain")
            self.log_message("malformed status=%s", "200")
            return
        self._send(404, b"{}")
        self.log_message("unknown status=%s", "404")

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path != "/pin":
            self._send(404, b"{}")
            self.log_message("unknown status=%s", "404")
            return
        payload = self._read_json()
        if payload is None:
            self._send(200, MALFORMED, "text/plain")
            self.log_message("pin status=%s reason=%s", "200", "malformed")
            return
        drop = self.headers.get("X-Lab-Drop", "") == "1"
        self._login(payload, drop)


def create_server(bind: str, port: int, run_id: str, log_path: str | None = None) -> LabServer:
    log = BoundedRedactingLog(log_path) if log_path else None
    return LabServer((bind, port), Handler, run_id, log)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8768)
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
