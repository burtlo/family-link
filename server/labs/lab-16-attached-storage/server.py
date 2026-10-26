#!/usr/bin/env python3
"""LAB-16 diary chunk server (h22-compatible /v1/diary)."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from server_log import BoundedRedactingLog, redact_line  # noqa: E402

SERVER_DIR = Path(__file__).resolve().parent
FIXTURE_PATH = SERVER_DIR / "fixtures" / "device.json"
FIXTURE = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
FIXTURE_SHA256 = hashlib.sha256(FIXTURE_PATH.read_bytes()).hexdigest()
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


def parse_multipart_legacy(body: bytes, content_type: str) -> dict[str, bytes | str]:
    """Retain the original parsing behavior for existing LAB-16 variants."""
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


def parse_multipart_exact(body: bytes, content_type: str) -> dict[str, bytes | str]:
    match = re.search(r"boundary=([^;\s]+)", content_type, re.I)
    if not match:
        return {}
    boundary = match.group(1).strip('"')
    try:
        delim = ("--" + boundary).encode("ascii")
    except UnicodeEncodeError:
        return {}
    if not delim or not body.startswith(delim + b"\r\n"):
        return {}
    out: dict[str, bytes | str] = {}
    cursor = len(delim) + 2
    while cursor <= len(body):
        header_end = body.find(b"\r\n\r\n", cursor)
        if header_end < 0:
            return {}
        next_delimiter = body.find(b"\r\n" + delim, header_end + 4)
        if next_delimiter < 0:
            return {}
        header_blob = body[cursor:header_end]
        payload = body[header_end + 4:next_delimiter]
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
            return {}
        if name in out:
            return {}
        if filename:
            out[name] = payload
        else:
            out[name] = payload.decode("utf-8", errors="replace")
        cursor = next_delimiter + 2 + len(delim)
        if body[cursor:cursor + 2] == b"--":
            cursor += 2
            if body[cursor:cursor + 2] == b"\r\n":
                cursor += 2
            return out if cursor == len(body) else {}
        if body[cursor:cursor + 2] != b"\r\n":
            return {}
        cursor += 2
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
                    "fixture_revision": FIXTURE.get("fixture_revision", FIXTURE["device_id"]),
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
        content_type = self.headers.get("Content-Type") or ""
        if FIXTURE.get("variant") != "upload":
            fields = parse_multipart_legacy(body, content_type)
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
            return
        boundary_match = re.search(r"boundary=([^;\s]+)", content_type, re.I)
        expected_boundary = str(FIXTURE.get("multipart_boundary", "----FamilyLinkLab16"))
        if boundary_match is None or boundary_match.group(1).strip('"') != expected_boundary:
            self._json(400, {"error": "unexpected multipart boundary"})
            return
        fields = parse_multipart_exact(body, content_type)
        expected_fields = set(FIXTURE.get("multipart_fields", ["session", "seq", "blob"]))
        if set(fields) != expected_fields:
            self._json(400, {"error": "multipart fields mismatch"})
            return
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
        expected_session = str(FIXTURE["session"]) if "session" in FIXTURE else None
        expected_chunks = int(FIXTURE["expected_chunks"]) if FIXTURE.get("variant") == "upload" else EXPECTED_CHUNKS
        expected_wav_bytes = int(FIXTURE.get("expected_wav_bytes") or 0)
        expected_pcm_bytes = int(FIXTURE.get("expected_pcm_bytes") or 0)
        if ((expected_session is not None and session != expected_session)
                or seq < 0 or seq >= expected_chunks):
            self._json(400, {"error": "unexpected session or sequence"})
            return
        if expected_wav_bytes and len(blob) != expected_wav_bytes:
            self._json(400, {"error": "unexpected WAV byte count"})
            return
        if len(blob) < 44 or blob[:4] != b"RIFF" or blob[8:16] != b"WAVEfmt " or blob[36:40] != b"data":
            self._json(400, {"error": "invalid WAV header"})
            return
        declared_pcm_bytes = int.from_bytes(blob[40:44], "little")
        if declared_pcm_bytes != len(blob) - 44 or (expected_pcm_bytes and declared_pcm_bytes != expected_pcm_bytes):
            self._json(400, {"error": "WAV PCM length mismatch"})
            return
        wav_sha256 = hashlib.sha256(blob).hexdigest()
        pcm_sha256 = hashlib.sha256(blob[44:]).hexdigest()
        received_at = datetime.now(timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z")
        chunk_id = f"{self.server.run_id}-{session}-{seq:04d}"
        row = {
            "id": chunk_id,
            "device_id": FIXTURE["device_id"],
            "session": session,
            "seq": seq,
            "received_at": received_at,
            "bytes": len(blob),
            "pcm_bytes": declared_pcm_bytes,
            "wav_sha256": wav_sha256,
            "pcm_sha256": pcm_sha256,
            "status": 200,
            "request_body_bytes": length,
            "monotonic_ns": time.monotonic_ns(),
        }
        out = dict(row)
        out["ok"] = True
        row["response_bytes"] = len(json.dumps(out).encode("utf-8"))
        with STORE.lock:
            STORE.chunks.append(row)
        self.log_message("diary session=%s seq=%s status=200 request_body_bytes=%s response_bytes=%s"
                         " wav_bytes=%s pcm_bytes=%s wav_sha256=%s pcm_sha256=%s",
                         session, seq, length, row["response_bytes"], len(blob), declared_pcm_bytes,
                         wav_sha256, pcm_sha256)
        self._json(200, out)


def main() -> None:
    global ACTIVE_TOKEN, EXPECTED_CHUNKS, FIXTURE, FIXTURE_PATH, FIXTURE_SHA256
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8776)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--log-file", default="")
    parser.add_argument("--device-token", default=DEVICE_TOKEN)
    parser.add_argument("--fixture-file", default=str(FIXTURE_PATH))
    parser.add_argument(
        "--expected-chunks",
        type=int,
        default=int(FIXTURE.get("expected_chunks") or 210),
    )
    args = parser.parse_args()
    ACTIVE_TOKEN = args.device_token
    FIXTURE_PATH = Path(args.fixture_file).resolve()
    try:
        FIXTURE_PATH.relative_to(SERVER_DIR.resolve() / "fixtures")
    except ValueError as exc:
        parser.error("--fixture-file must be under the LAB-16 fixtures directory")
    FIXTURE = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    FIXTURE_SHA256 = hashlib.sha256(FIXTURE_PATH.read_bytes()).hexdigest()
    EXPECTED_CHUNKS = int(args.expected_chunks)
    httpd = ThreadingHTTPServer((args.bind, args.port), Handler)
    httpd.run_id = args.run_id
    httpd.run_started_utc = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    if FIXTURE.get("variant") != "upload":
        data_dir = ROOT / "data" / "labs" / "lab-16" / args.run_id
        data_dir.mkdir(parents=True, exist_ok=True)
        httpd.data_dir = data_dir
    httpd.log = BoundedRedactingLog(Path(args.log_file)) if args.log_file else None
    print(f"LAB-16 diary server {args.bind}:{args.port} run_id={args.run_id}", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
