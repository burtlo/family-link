#!/usr/bin/env python3
"""Synthetic LAB-07 health and roster fixture server.

Wi-Fi absence is device-side: this process stays idle for that scenario.
Health JSON is endpoint health, not a person's presence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

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
    """Reject a byte change that leaves fixture_revision unchanged."""
    manifest_path = fixture_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    revision = manifest.get("fixture_revision")
    if not revision:
        raise IdentityError("fixture manifest has no fixture_revision")
    files = manifest.get("files") or {}
    for name, meta in files.items():
        path = fixture_dir / name
        data = path.read_bytes()
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
BODIES = {name: (FIXTURE_DIR / name).read_bytes() for name in MANIFEST["files"]}


def _body(name: str) -> bytes:
    return BODIES[name]


class LabServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, address, handler, run_id: str, delay_ms: int, log: BoundedRedactingLog | None):
        super().__init__(address, handler)
        self.run_id = run_id
        self.delay_ms = delay_ms
        self.log = log
        self.fail_counts = {"fail-once": 0, "fail-twice": 0}


class Handler(BaseHTTPRequestHandler):
    server: LabServer

    def log_request(self, code="-", size="-"):
        return

    def log_message(self, fmt, *args):
        line = (fmt % args).encode("utf-8", errors="replace")
        redacted = redact_line(line)
        sys.stdout.buffer.write(redacted + b"\n")
        sys.stdout.buffer.flush()
        if self.server.log is not None:
            self.server.log.feed("stdout", redacted + b"\n")

    def _send(self, status: int, body: bytes, content_type: str = "application/json") -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Fixture-SHA256", FIXTURE_SHA256)
        self.send_header("X-Run-ID", self.server.run_id)
        self.end_headers()
        self.wfile.write(body)

    def _script(self) -> str:
        parsed = urlsplit(self.path)
        values = parse_qs(parsed.query).get("script", ["eight"])
        script = values[0]
        if not re.fullmatch(r"[a-z0-9-]{1,32}", script):
            return "eight"
        return script

    def _roster_body(self, script: str) -> tuple[int, bytes]:
        if script in {"idle", "wifi-absent"}:
            return 204, b""
        if script == "delayed":
            time.sleep(self.server.delay_ms / 1000)
            return 200, _body("roster-eight.json")
        if script == "invalid-200":
            return 200, _body("roster-invalid.json")
        if script == "malformed":
            return 200, _body("malformed.json")
        if script == "empty":
            return 200, _body("roster-empty.json")
        if script == "one":
            return 200, _body("roster-one.json")
        if script in {"eight", "add", "mute", "unmute"}:
            return 200, _body("roster-eight.json")
        if script in {"remove", "seven"}:
            return 200, _body("roster-seven.json")
        if script == "reorder":
            return 200, _body("roster-reordered.json")
        if script == "fail":
            return 503, b"{}"
        if script == "fail-once":
            self.server.fail_counts["fail-once"] += 1
            if self.server.fail_counts["fail-once"] == 1:
                return 503, b"{}"
            return 200, _body("roster-eight.json")
        if script == "fail-twice":
            self.server.fail_counts["fail-twice"] += 1
            if self.server.fail_counts["fail-twice"] <= 2:
                return 503, b"{}"
            return 200, _body("roster-eight.json")
        return 404, b"{}"

    def do_GET(self):
        parsed = urlsplit(self.path)
        if parsed.path == "/health":
            payload = {
                "run_id": self.server.run_id,
                "fixture_version": MANIFEST["fixture_version"],
                "fixture_sha256": FIXTURE_SHA256,
                "server_version": MANIFEST["server_version"],
            }
            body = json.dumps(payload).encode("utf-8")
            self._send(200, body)
            self.log_message("health status=%s", "200")
            return
        if parsed.path == "/roster":
            status, body = self._roster_body(self._script())
            content = "application/json" if body.startswith(b"{") or body == b"" else "text/plain"
            self._send(status, body, content)
            self.log_message("roster script=%s status=%s", self._script(), status)
            return
        self._send(404, b"{}")
        self.log_message("unknown status=%s", "404")


def create_server(bind: str, port: int, run_id: str, delay_ms: int = 1500,
                  log_path: str | None = None) -> LabServer:
    log = BoundedRedactingLog(log_path) if log_path else None
    return LabServer((bind, port), Handler, run_id, delay_ms, log)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8767)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--delay-ms", type=int, default=1500)
    parser.add_argument("--log", default="")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", args.run_id):
        parser.error("unsafe run ID")
    if args.delay_ms < 0 or args.delay_ms > 5000:
        parser.error("delay-ms out of range")
    server = create_server(args.bind, args.port, args.run_id, args.delay_ms, args.log or None)
    try:
        server.serve_forever(poll_interval=0.1)
    finally:
        if server.log is not None:
            server.log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
