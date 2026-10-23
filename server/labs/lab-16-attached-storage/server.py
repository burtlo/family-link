#!/usr/bin/env python3
"""LAB-16 diary chunk server (h22-compatible /v1/diary)."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from server_log import BoundedRedactingLog, redact_line  # noqa: E402

SERVER_DIR = Path(__file__).resolve().parent
FIXTURE = json.loads((SERVER_DIR / "fixtures" / "device.json").read_text(encoding="utf-8"))
FIXTURE_SHA256 = hashlib.sha256((SERVER_DIR / "fixtures" / "device.json").read_bytes()).hexdigest()
DEVICE_TOKEN = str(FIXTURE["token"])
ACTIVE_TOKEN = DEVICE_TOKEN
EXPECTED_CHUNKS = int(FIXTURE.get("expected_chunks") or 210)


class Store:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.chunks: list[dict] = []
        self.accepting = True

    def reset(self) -> None:
        with self.lock:
            self.chunks.clear()
            self.accepting = True

    def set_accepting(self, value: bool) -> None:
        with self.lock:
            self.accepting = value


STORE = Store()


def parse_bearer(header: str | None) -> str | None:
    if not header:
        return None
    parts = header.split(None, 1)
    if len(parts) != 2 or parts[0].lower() != "bearer":
        return None
    return parts[1].strip()


def parse_multipart(body: bytes, content_type: str) -> dict[str, bytes | str]:
    match = re.search(r"boundary=([^;\s]+)", content_type, re.I)
    if not match:
        return {}
    boundary = match.group(1).strip('"')
    delim = ("--" + boundary).encode("ascii")
    out: dict[str, bytes | str] = {}
    for part in body.split(delim):
        part = part.strip(b"\r\n")
        if not part or part == b"--":
            continue
        header_blob, _, payload = part.partition(b"\r\n\r\n")
        payload = payload.rstrip(b"\r\n")
        name = None
        filename = None
        for line in header_blob.decode("latin-1", errors="replace").split("\r\n"):
            if line.lower().startswith("content-disposition:"):
                for piece in line.split(";"):
                    piece = piece.strip()
                    if piece.startswith("name="):
                        name = piece.split("=", 1)[1].strip('"')
                    if piece.startswith("filename="):
                        filename = piece.split("=", 1)[1].strip('"')
        if not name:
            continue
        if filename:
            out[name] = payload
        else:
            out[name] = payload.decode("utf-8", errors="replace")
    return out


class Handler(BaseHTTPRequestHandler):
    server_version = "LAB16Diary/1"

    def log_message(self, fmt: str, *args) -> None:
        log = getattr(self.server, "log", None)
        if log is not None:
            log.write(redact_line((fmt % args).encode("utf-8", errors="replace")))
        else:
            super().log_message(fmt, *args)

    def _json(self, code: int, payload: dict) -> None:
        data = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:
        parsed = urlsplit(self.path)
        if parsed.path == "/health":
            with STORE.lock:
                count = len(STORE.chunks)
                accepting = STORE.accepting
            self._json(
                200,
                {
                    "run_id": self.server.run_id,
                    "fixture_sha256": FIXTURE_SHA256,
                    "fixture_revision": FIXTURE["device_id"],
                    "chunks_received": count,
                    "expected_chunks": EXPECTED_CHUNKS,
                    "accepting": accepting,
                },
            )
            return
        if parsed.path == "/v1/diary":
            token = parse_bearer(self.headers.get("Authorization"))
            if token != ACTIVE_TOKEN:
                self._json(401, {"error": "unauthorized"})
                return
            with STORE.lock:
                rows = list(STORE.chunks)
            self._json(
                200,
                {
                    "device_id": FIXTURE["device_id"],
                    "name": FIXTURE["name"],
                    "chunks": rows,
                },
            )
            return
        self._json(404, {"error": "not_found"})

    def do_POST(self) -> None:
        parsed = urlsplit(self.path)
        if parsed.path == "/v1/control":
            length = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(length) if length else b"{}"
            try:
                body = json.loads(raw.decode("utf-8"))
            except json.JSONDecodeError:
                body = {}
            if body.get("reset"):
                STORE.reset()
            if "accepting" in body:
                STORE.set_accepting(bool(body.get("accepting")))
            self._json(200, {"result": "ok"})
            return
        if parsed.path != "/v1/diary":
            self._json(404, {"error": "not_found"})
            return
        token = parse_bearer(self.headers.get("Authorization"))
        if token != ACTIVE_TOKEN:
            self._json(401, {"error": "unauthorized"})
            return
        with STORE.lock:
            if not STORE.accepting:
                self._json(503, {"error": "offline"})
                return
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length)
        fields = parse_multipart(body, self.headers.get("Content-Type") or "")
        session = str(fields.get("session") or "0")
        try:
            seq = int(fields.get("seq") or 0)
        except (TypeError, ValueError):
            self._json(400, {"error": "seq must be int"})
            return
        blob = fields.get("blob")
        if not isinstance(blob, (bytes, bytearray)) or not blob:
            self._json(400, {"error": "blob required"})
            return
        now = datetime.now(timezone.utc)
        received_at = now.strftime("%Y-%m-%dT%H:%M:%S.") + f"{now.microsecond:06d}"[:3] + "Z"
        chunk_id = now.strftime("%Y%m%dT%H%M%S") + f"{now.microsecond:06d}"[:3] + f"-{seq:04d}"
        dest = self.server.data_dir / f"{chunk_id}.wav"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob)
        row = {
            "id": chunk_id,
            "device_id": FIXTURE["device_id"],
            "session": session,
            "seq": seq,
            "received_at": received_at,
            "bytes": len(blob),
        }
        with STORE.lock:
            STORE.chunks.append(row)
        self.log_message("diary session=%s seq=%s bytes=%s", session, seq, len(blob))
        out = dict(row)
        out["ok"] = True
        self._json(200, out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8776)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--log-file", default="")
    parser.add_argument("--device-token", default=DEVICE_TOKEN)
    parser.add_argument(
        "--expected-chunks",
        type=int,
        default=int(FIXTURE.get("expected_chunks") or 210),
    )
    args = parser.parse_args()
    global ACTIVE_TOKEN, EXPECTED_CHUNKS
    ACTIVE_TOKEN = args.device_token
    EXPECTED_CHUNKS = int(args.expected_chunks)
    data_dir = ROOT / "data" / "labs" / "lab-16" / args.run_id
    data_dir.mkdir(parents=True, exist_ok=True)
    httpd = ThreadingHTTPServer((args.bind, args.port), Handler)
    httpd.run_id = args.run_id
    httpd.data_dir = data_dir
    httpd.log = BoundedRedactingLog(Path(args.log_file)) if args.log_file else None
    print(f"LAB-16 diary server {args.bind}:{args.port} run_id={args.run_id}", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
