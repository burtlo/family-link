#!/usr/bin/env python3
"""Hermetic 180-second h34 qualification and process-crash recovery harness."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

import httpx

REPO = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(REPO))

from demos.server.h34_message_store.pcm_wav import _wav_header
RATE = 16_000
DURATION_MS = 180_000
CHUNK_MS = 2_000
CHUNK_BYTES = RATE * CHUNK_MS // 1000 * 2
CHUNKS = DURATION_MS // CHUNK_MS
PCM_BYTES = CHUNKS * CHUNK_BYTES
WAV_BYTES = PCM_BYTES + 44
MEMORY_LIMIT = 1024 * 1024
TOKENS = {"sender": "qualification-sender", "recipient": "qualification-recipient", "unrelated": "qualification-unrelated", "admin": "qualification-admin", "other_admin": "qualification-other-admin"}
BOUNDARIES = {
    "chunk": ["chunk_temp_write", "chunk_temp_fsync", "chunk_rename_raw", "chunk_audio_dir_fsync", "chunk_work_dir_fsync", "chunk_rename", "chunk_manifest_temp_fsync", "chunk_manifest_replace", "chunk_manifest", "chunk_pre_response"],
    "complete": ["intent_temp_fsync", "intent_replace", "intent", "wav_temp_write", "wav_fsync", "wav_rename", "complete_manifest_temp_fsync", "complete_manifest_replace", "complete_manifest", "marker_temp_fsync", "marker_replace", "marker", "message_dest_parent", "message_rename_raw", "message_source_parent_fsync", "message_rename", "inbox_temp_fsync", "inbox_replace", "inbox_append", "cleanup"],
    "cull": ["cull_intent_temp_fsync", "cull_intent_replace", "cull_intent", "cull_deletion_temp_fsync", "cull_deletion_replace", "cull_deletion", "cull_dest_parent", "cull_rename_raw", "cull_source_parent_fsync", "cull_rename", "cull_audit"],
    "restore": ["restore_intent_temp_fsync", "restore_intent_replace", "restore_intent", "restore_dest_parent", "restore_rename_raw", "restore_source_parent_fsync", "restore_rename", "restore_audit", "restore_cleanup"],
}


def need(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def auth(who: str) -> dict[str, str]:
    return {"Authorization": "Bearer " + TOKENS[who]}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def registry(root: Path) -> Path:
    path = root / "devices.yaml"
    path.write_text(
        "devices:\n"
        "  - {id: sender, token: qualification-sender, role: child, peer: recipient}\n"
        "  - {id: recipient, token: qualification-recipient, role: child, peer: sender}\n"
        "  - {id: unrelated, token: qualification-unrelated, role: child, peer: ''}\n"
        "  - {id: admin, token: qualification-admin, role: admin, peer: ''}\n"
        "  - {id: other_admin, token: qualification-other-admin, role: admin, peer: ''}\n",
        encoding="utf-8",
    )
    return path


class Server:
    def __init__(self, root: Path, *, measured: bool = False, crash: str | None = None, extra_env: dict[str, str] | None = None):
        self.root = root
        self.registry = registry(root)
        self.measured = measured
        self.crash = crash
        self.extra_env = extra_env or {}
        self.port = free_port()
        self.base = f"http://127.0.0.1:{self.port}"
        self.metrics = root / "metrics.jsonl"
        self.proc: subprocess.Popen | None = None

    def start(self) -> None:
        env = os.environ.copy()
        env["PYTHONPATH"] = str(REPO) + os.pathsep + env.get("PYTHONPATH", "")
        env["FAMILY_LINK_ROOT"] = str(self.root)
        env["FAMILY_LINK_DEVICES_REGISTRY"] = str(self.registry)
        env.update(self.extra_env)
        env.pop("FAMILY_LINK_H34_CRASH_AFTER", None)
        if self.crash:
            env["FAMILY_LINK_H34_CRASH_AFTER"] = self.crash
        py = REPO / ".venv" / "bin" / "python"
        executable = str(py) if py.is_file() else sys.executable
        command = [executable, str(HERE / ("profiled_server.py" if self.measured else "server.py")), "--port", str(self.port)]
        if self.measured:
            command += ["--metrics", str(self.metrics)]
        with (self.root / "server.log").open("ab") as log:
            self.proc = subprocess.Popen(command, cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"server exited {self.proc.returncode}: {(self.root / 'server.log').read_text()[-3000:]}")
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=0.1):
                    return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("server startup timed out")

    def stop(self) -> int | None:
        if self.proc is None:
            return None
        if self.proc.poll() is None:
            self.proc.terminate()
        try:
            return self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return self.proc.wait(timeout=5)

    def restart(self, *, measured: bool | None = None, crash: str | None = None) -> None:
        self.stop()
        if measured is not None:
            self.measured = measured
        self.crash = crash
        self.port = free_port()
        self.base = f"http://127.0.0.1:{self.port}"
        self.start()


def fixture(path: Path) -> dict:
    raw = hashlib.sha256()
    wav = hashlib.sha256()
    header = _wav_header(PCM_BYTES, RATE, 1)
    wav.update(header)
    with path.open("wb") as out:
        out.write(header)
        for seq in range(CHUNKS):
            block = bytearray(CHUNK_BYTES)
            for i in range(CHUNK_BYTES // 2):
                # Integer arithmetic gives reproducible signed samples on all hosts.
                sample_index = seq * (CHUNK_BYTES // 2) + i
                sample = ((sample_index * 1103515245 + 12345) >> 16) & 0xffff
                struct.pack_into("<H", block, i * 2, sample)
            raw.update(block)
            wav.update(block)
            out.write(block)
    need(path.stat().st_size == WAV_BYTES, "fixture length")
    return {"generator": "integer-lcg-v1", "sample_rate_hz": RATE, "channels": 1, "sample_format": "s16le", "duration_ms": DURATION_MS, "target_chunk_ms": CHUNK_MS, "chunk_count": CHUNKS, "chunk_bytes": CHUNK_BYTES, "pcm_bytes": PCM_BYTES, "wav_bytes": WAV_BYTES, "pcm_sha256": raw.hexdigest(), "wav_sha256": wav.hexdigest()}


def chunk_at(path: Path, seq: int) -> bytes:
    with path.open("rb") as fh:
        fh.seek(44 + seq * CHUNK_BYTES)
        block = fh.read(CHUNK_BYTES)
    need(len(block) == CHUNK_BYTES, f"fixture chunk {seq} is short")
    return block


def create(http: httpx.Client, base: str, target: str = "recipient", *, chunk_ms: int = CHUNK_MS, broadcast: bool = False) -> str:
    selector = {"broadcast": True} if broadcast else {"to_user_id": target}
    response = http.post(base + "/v1/messages", headers=auth("sender"), json={"protocol": "family-message/1", **selector, "audio": {"codec": "pcm_s16le", "sample_rate_hz": RATE, "channels": 1, "target_chunk_ms": chunk_ms}})
    need(response.status_code == 201, f"create: {response.status_code} {response.text}")
    return response.json()["message_id"]


def upload(http: httpx.Client, base: str, message_id: str, seq: int, body: bytes, *, duration_ms: int = CHUNK_MS) -> httpx.Response:
    return http.put(f"{base}/v1/messages/{message_id}/audio/{seq}", content=body, headers={**auth("sender"), "Content-Type": "application/octet-stream", "X-Chunk-SHA256": digest(body), "X-Chunk-Start-Ms": str(seq * duration_ms), "X-Chunk-Duration-Ms": str(duration_ms), "X-Chunk-Bytes": str(len(body))})


def complete(http: httpx.Client, base: str, message_id: str, source_sha: str, chunks: int = CHUNKS) -> httpx.Response:
    return http.post(f"{base}/v1/messages/{message_id}/complete", headers=auth("sender"), json={"audio_chunks": chunks, "duration_ms": chunks * CHUNK_MS, "closed_reason": "button", "source_audio_sha256": source_sha})


def stream_hash(http: httpx.Client, url: str, headers: dict, expected_status: int) -> tuple[str, int, dict]:
    with http.stream("GET", url, headers=headers) as response:
        need(response.status_code == expected_status, f"stream status {response.status_code}, expected {expected_status}")
        h = hashlib.sha256()
        n = 0
        for block in response.iter_bytes(chunk_size=16_384):
            h.update(block)
            n += len(block)
        return h.hexdigest(), n, dict(response.headers)


def file_range_hash(path: Path, start: int, end: int) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        fh.seek(start)
        remaining = end - start + 1
        while remaining:
            block = fh.read(min(16_384, remaining))
            need(bool(block), "fixture range unexpectedly short")
            h.update(block)
            remaining -= len(block)
    return h.hexdigest()


def metrics_since(path: Path, from_line: int = 0) -> list[dict]:
    return [json.loads(line) for line in path.read_text().splitlines()[from_line:]] if path.exists() else []


def await_metrics(path: Path, from_line: int, minimum: int) -> list[dict]:
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        records = metrics_since(path, from_line)
        if len(records) >= minimum:
            return records
        time.sleep(0.02)
    return metrics_since(path, from_line)


def qualification(root: Path) -> dict:
    fixture_path = root / "fixture.wav"
    fixture_info = fixture(fixture_path)
    server = Server(root, measured=True)
    server.start()
    checks: dict = {}
    try:
        with httpx.Client(timeout=90) as http:
            # Prime imports, registry parsing, and route setup before measurements.
            warm = http.get(server.base + "/v1/inbox", headers=auth("recipient"))
            need(warm.status_code == 200, "warmup inbox")
            short_mid = create(http, server.base)
            short_body = chunk_at(fixture_path, 0)
            short_metrics_start = len(metrics_since(server.metrics))
            need(upload(http, server.base, short_mid, 0, short_body).status_code == 201, "short upload")
            need(complete(http, server.base, short_mid, digest(short_body), 1).status_code in (200, 201), "short complete")
            short_hash, short_size, _ = stream_hash(http, f"{server.base}/v1/messages/{short_mid}/audio", {**auth("recipient"), "Range": f"bytes=0-{CHUNK_BYTES + 43}"}, 206)
            need((short_hash, short_size) == (digest(_wav_header(CHUNK_BYTES, RATE, 1) + short_body), CHUNK_BYTES + 44), "short full-range media")
            short_samples = await_metrics(server.metrics, short_metrics_start, 3)
            begin = len(metrics_since(server.metrics))
            mid = create(http, server.base)
            first = chunk_at(fixture_path, 1)
            need(upload(http, server.base, mid, 1, first).status_code == 201, "reordered chunk 1")
            need(upload(http, server.base, mid, 0, chunk_at(fixture_path, 0)).status_code == 201, "reordered chunk 0")
            need(upload(http, server.base, mid, 1, first).status_code == 200, "identical chunk retry")
            altered = bytearray(first)
            altered[0] ^= 1
            need(upload(http, server.base, mid, 1, bytes(altered)).status_code == 409, "conflicting chunk retry")
            for seq in range(2, CHUNKS):
                response = upload(http, server.base, mid, seq, chunk_at(fixture_path, seq))
                need(response.status_code == 201, f"upload {seq}: {response.status_code} {response.text}")
            checks["upload"] = {"status": "pass", "chunks": CHUNKS, "reordered": True, "deduplicated": True, "conflict_rejected": True}
            done = complete(http, server.base, mid, fixture_info["pcm_sha256"])
            need(done.status_code in (200, 201), f"complete: {done.status_code} {done.text}")
            manifest = done.json()
            need(manifest["audio"]["sha256"] == fixture_info["wav_sha256"], "canonical WAV hash")
            need(manifest["audio"]["bytes"] == WAV_BYTES, "canonical WAV size")
            need(manifest["duration_ms"] == DURATION_MS, "canonical duration")
            check_url = f"{server.base}/v1/messages/{mid}/audio"
            full_hash, full_size, full_headers = stream_hash(http, check_url, auth("recipient"), 200)
            need((full_hash, full_size) == (fixture_info["wav_sha256"], WAV_BYTES), "full media stream")
            need(full_headers.get("accept-ranges") == "bytes", "full Accept-Ranges")
            ranges: list[dict] = []
            for start, end in [(0, 63), (WAV_BYTES // 2, WAV_BYTES // 2 + 127), (WAV_BYTES - 128, WAV_BYTES - 1), (0, WAV_BYTES - 1)]:
                hash_value, byte_count, headers = stream_hash(http, check_url, {**auth("recipient"), "Range": f"bytes={start}-{end}"}, 206)
                need(hash_value == file_range_hash(fixture_path, start, end), f"range hash {start}-{end}")
                need(byte_count == end - start + 1, f"range length {start}-{end}")
                need(headers.get("content-range") == f"bytes {start}-{end}/{WAV_BYTES}", f"Content-Range {start}-{end}")
                need(int(headers.get("content-length", "0")) == byte_count, f"Content-Length {start}-{end}")
                ranges.append({"start": start, "end": end, "bytes": byte_count, "sha256": hash_value})
            inbox = http.get(server.base + "/v1/inbox", headers=auth("recipient"))
            need(inbox.status_code == 200, "recipient inbox")
            rows = [row for row in inbox.json()["inbox"] if row["message_id"] == mid]
            need(len(rows) == 1, "recipient inbox multiplicity")
            unrelated = http.get(check_url, headers=auth("unrelated"))
            need(unrelated.status_code == 403, "unrelated media forbidden")
            sender_media = http.get(check_url, headers=auth("sender"))
            need(sender_media.status_code == 403, "sender without inbox denied playback")
            admin_hash, admin_size, _ = stream_hash(http, check_url, auth("admin"), 200)
            need((admin_hash, admin_size) == (fixture_info["wav_sha256"], WAV_BYTES), "admin playback")
            repeat = complete(http, server.base, mid, fixture_info["pcm_sha256"])
            need(repeat.status_code == 200 and repeat.json()["audio"]["sha256"] == fixture_info["wav_sha256"], "sender completion idempotency")
            complete_payload = {"audio_chunks": CHUNKS, "duration_ms": DURATION_MS, "closed_reason": "button", "source_audio_sha256": fixture_info["pcm_sha256"]}
            for actor in ("recipient", "unrelated"):
                denied = http.post(f"{server.base}/v1/messages/{mid}/complete", headers=auth(actor), json=complete_payload)
                need(denied.status_code == 403, f"{actor} completion forbidden")
            altered_completion = http.post(f"{server.base}/v1/messages/{mid}/complete", headers=auth("sender"), json={**complete_payload, "closed_reason": "timeout"})
            need(altered_completion.status_code == 409, "conflicting completion tuple")
            child_preview = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("sender"), json={"message_ids": [mid]})
            need(child_preview.status_code == 403, "child admin preview forbidden")
            denied_cull = http.post(server.base + "/v1/admin/messages/cull", headers=auth("sender"), json={"selection_token": "invalid"})
            need(denied_cull.status_code == 403, "child admin cull forbidden")
            denied_restore = http.post(f"{server.base}/v1/admin/messages/{mid}/restore", headers=auth("sender"))
            need(denied_restore.status_code == 403, "child admin restore forbidden")
            invalid_range = http.get(check_url, headers={**auth("recipient"), "Range": f"bytes={WAV_BYTES}-"})
            need(invalid_range.status_code == 416 and invalid_range.headers.get("content-range") == f"bytes */{WAV_BYTES}", "unsatisfiable range")
            odd_range = http.get(check_url, headers={**auth("recipient"), "Range": "bytes=45-64"})
            need(odd_range.status_code == 400, "odd PCM sample boundary rejected")
            huge_range = http.get(check_url, headers={**auth("recipient"), "Range": "bytes=" + "9" * 1000 + "-"})
            need(huge_range.status_code in (400, 416), f"oversized Range unsafe status {huge_range.status_code}")
            need(http.get(server.base + "/v1/inbox", headers=auth("recipient")).status_code == 200, "server survived oversized Range")
            bad_meta = http.post(server.base + "/v1/messages", headers=auth("sender"), json={"protocol": "family-message/1", "to_user_id": "recipient", "audio": {"codec": "pcm_s16le", "sample_rate_hz": 8000, "channels": 1, "target_chunk_ms": CHUNK_MS}})
            need(bad_meta.status_code in (400, 422), "unsupported PCM rate rejected")
            checks["media"] = {"status": "pass", "message_id": mid, "full_sha256": full_hash, "full_bytes": full_size, "ranges": ranges, "recipient_inbox_count": len(rows)}
            checks["authorization_and_limits"] = {"status": "pass", "sender_repeat": True, "recipient_complete_forbidden": True, "unrelated_complete_forbidden": True, "child_admin_forbidden": True, "admin_playback": True, "sender_without_inbox_playback": 403, "unsatisfiable_range": 416, "odd_pcm_range": 400, "oversized_range_safe": True, "unsupported_rate_rejected": True}
            samples = await_metrics(server.metrics, begin, CHUNKS + 16)
            upload_peaks = [r["python_peak_over_baseline_bytes"] for r in samples if r["route"].startswith("PUT ") and r["status"] == 201]
            final_peaks = [r["python_peak_over_baseline_bytes"] for r in samples if r["route"].endswith("/complete")]
            range_peaks = [r["python_peak_over_baseline_bytes"] for r in samples if r["route"] == "GET /v1/messages/" + mid + "/audio" and r["status"] == 206]
            need(upload_peaks and final_peaks and len(range_peaks) == 4, "memory profiler samples")
            short_upload_peaks = [r["python_peak_over_baseline_bytes"] for r in short_samples if r["route"].startswith("PUT ")]
            short_final_peaks = [r["python_peak_over_baseline_bytes"] for r in short_samples if r["route"].endswith("/complete")]
            short_range_peaks = [r["python_peak_over_baseline_bytes"] for r in short_samples if r["route"].endswith("/audio") and r["status"] == 206]
            need(short_upload_peaks and short_final_peaks and short_range_peaks, "short profiler samples")
            memory = {"method": "tracemalloc.reset_peak per ASGI request; peak minus live allocation baseline; response streaming included", "limit_bytes": MEMORY_LIMIT, "short_2s": {"upload_peak_bytes": max(short_upload_peaks), "finalization_peak_bytes": max(short_final_peaks), "range_full_peak_bytes": max(short_range_peaks)}, "long_180s": {"upload_peak_bytes": max(upload_peaks), "finalization_peak_bytes": max(final_peaks), "range_small_peak_bytes": max(range_peaks[:3]), "range_full_peak_bytes": range_peaks[3]}, "server_rss_peak_bytes": max(r["rss_peak_bytes"] for r in samples), "max_server_request_chunk_bytes": max(r["max_request_chunk_bytes"] for r in samples), "max_server_response_chunk_bytes": max(r["max_response_chunk_bytes"] for r in samples), "samples": len(samples)}
            memory["status"] = "pass" if max(*memory["short_2s"].values(), *memory["long_180s"].values()) < MEMORY_LIMIT and memory["long_180s"]["range_full_peak_bytes"] < memory["short_2s"]["range_full_peak_bytes"] + MEMORY_LIMIT else "fail"
            checks["memory"] = memory
            need(memory["status"] == "pass", f"memory threshold: {memory}")
            short_media = list((root / "data" / "v1_product" / "message_store" / "messages").glob(f"**/{short_mid}/media.wav"))
            need(len(short_media) == 1, "stale-selection fixture media")
            preview_short = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"), json={"message_ids": [short_mid]})
            need(preview_short.status_code == 200 and preview_short.json().get("count") == 1, "stale-selection preview")
            stale_token = preview_short.json()["selection_token"]
            with short_media[0].open("r+b") as media_file:
                media_file.seek(44)
                original_byte = media_file.read(1)
                media_file.seek(44)
                media_file.write(bytes([original_byte[0] ^ 1]))
                media_file.flush()
                os.fsync(media_file.fileno())
            try:
                mutated_playback = http.get(f"{server.base}/v1/messages/{short_mid}/audio", headers=auth("recipient"))
                need(mutated_playback.status_code in (404, 409, 422), f"same-size mutated media served: {mutated_playback.status_code}")
                need("etag" not in mutated_playback.headers, "stale ETag served for mutated media")
                stale = http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": stale_token})
                need(stale.status_code == 409, f"same-size media mutation accepted for cull: {stale.status_code}")
                need(short_media[0].exists(), "stale cull moved mutated media")
                need(not list((root / "data" / "v1_product" / "message_store" / "trash").glob(f"**/{short_mid}")), "stale cull created trash directory")
            finally:
                with short_media[0].open("r+b") as media_file:
                    media_file.seek(44)
                    media_file.write(original_byte)
                    media_file.flush()
                    os.fsync(media_file.fileno())
            checks["stale_media_selection"] = {"status": "pass", "same_size_mutation": True, "cull_status": 409, "playback_rejected": True, "canonical_directory_preserved": True}
            limit_mid = create(http, server.base, chunk_ms=5000)
            five_second_pcm = b"\x00\x10" * (RATE * 5)
            accepted = upload(http, server.base, limit_mid, 0, five_second_pcm, duration_ms=5000)
            need(accepted.status_code == 201, f"5000 ms chunk acceptance: {accepted.status_code} {accepted.text}")
            over_target = http.post(server.base + "/v1/messages", headers=auth("sender"), json={"protocol": "family-message/1", "to_user_id": "recipient", "audio": {"codec": "pcm_s16le", "sample_rate_hz": RATE, "channels": 1, "target_chunk_ms": 5001}})
            need(over_target.status_code in (400, 413, 422), "5001 ms target rejection")
            over_chunk = upload(http, server.base, limit_mid, 1, five_second_pcm + b"\x00" * 32, duration_ms=5001)
            need(over_chunk.status_code in (400, 413, 422), f"5001 ms chunk rejection: {over_chunk.status_code}")
            odd_pcm = upload(http, server.base, limit_mid, 1, five_second_pcm + b"\x00", duration_ms=5000)
            need(odd_pcm.status_code in (400, 422), "odd PCM chunk byte rejection")
            advisory_mid = create(http, server.base, chunk_ms=2000)
            three_second_pcm = b"\x00\x10" * (RATE * 3)
            three_second = upload(http, server.base, advisory_mid, 0, three_second_pcm, duration_ms=3000)
            need(three_second.status_code == 201, f"3000 ms advisory target chunk rejected: {three_second.status_code}")
            checks["chunk_boundaries"] = {"status": "pass", "accepted_duration_ms": 5000, "rejected_duration_ms": 5001, "advisory_target_ms": 2000, "accepted_advisory_chunk_ms": 3000, "odd_pcm_bytes_rejected": True}
        server.restart(measured=False)
        with httpx.Client(timeout=30) as http:
            again_hash, again_bytes, _ = stream_hash(http, f"{server.base}/v1/messages/{mid}/audio", auth("recipient"), 200)
            need((again_hash, again_bytes) == (fixture_info["wav_sha256"], WAV_BYTES), "media after graceful restart")
            inbox = http.get(server.base + "/v1/inbox", headers=auth("recipient"))
            need(len([row for row in inbox.json()["inbox"] if row["message_id"] == mid]) == 1, "inbox after graceful restart")
            preview = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"), json={"message_ids": [mid]})
            need(preview.status_code == 200, f"admin preview {preview.status_code} {preview.text}")
            need(preview.json().get("count") == 1 and preview.json().get("bytes") == WAV_BYTES, "exact cull preview count/bytes")
            token = preview.json()["selection_token"]
            wrong_actor = http.post(server.base + "/v1/admin/messages/cull", headers=auth("other_admin"), json={"selection_token": token})
            need(wrong_actor.status_code in (403, 409), "preview token bound to admin")
            culled = http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": token})
            need(culled.status_code == 200 and mid in culled.json().get("trashed", []), "cull selection")
            replay = http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": token})
            need(replay.status_code in (400, 403, 409), "single-use cull token")
        server.restart()
        with httpx.Client(timeout=30) as http:
            hidden = http.get(f"{server.base}/v1/messages/{mid}/audio", headers=auth("recipient"))
            need(hidden.status_code == 404, "trashed media hidden")
            restored = http.post(f"{server.base}/v1/admin/messages/{mid}/restore", headers=auth("admin"))
            need(restored.status_code == 200, f"restore after restart {restored.status_code} {restored.text}")
        server.restart()
        with httpx.Client(timeout=30) as http:
            restored_hash, restored_bytes, _ = stream_hash(http, f"{server.base}/v1/messages/{mid}/audio", auth("recipient"), 200)
            need((restored_hash, restored_bytes) == (fixture_info["wav_sha256"], WAV_BYTES), "restored media after second restart")
            inbox = http.get(server.base + "/v1/inbox", headers=auth("recipient"))
            need(len([row for row in inbox.json()["inbox"] if row["message_id"] == mid]) == 1, "restored inbox multiplicity")
        checks["restart_and_trash"] = {"status": "pass", "media_sha256": fixture_info["wav_sha256"], "inbox_count": 1}
    finally:
        server.stop()
    return {"fixture": fixture_info, "checks": checks}


def malformed_jsonl_case(root: Path) -> dict:
    """A corrupt committed row between valid rows must stop startup with a diagnostic."""
    root.mkdir(parents=True)
    server = Server(root)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            for _ in range(2):
                mid = create(http, server.base)
                need(upload(http, server.base, mid, 0, source).status_code == 201, "JSONL setup upload")
                need(complete(http, server.base, mid, digest(source), 1).status_code in (200, 201), "JSONL setup complete")
        server.stop()
        inbox_path = root / "data" / "v1_product" / "message_store" / "state" / "inboxes.jsonl"
        rows = inbox_path.read_bytes().splitlines(keepends=True)
        need(len(rows) == 2, "JSONL setup inbox rows")
        corrupt = rows[0] + b"{malformed committed row}\n" + rows[1]
        inbox_path.write_bytes(corrupt)
        try:
            server.restart()
        except RuntimeError:
            pass
        else:
            raise AssertionError("malformed middle JSONL row did not fail closed")
        need(inbox_path.read_bytes() == corrupt, "malformed committed JSONL row was silently rewritten")
        diagnostic = (root / "data" / "v1_product" / "message_store" / "state" / "recovery.jsonl").read_text(encoding="utf-8")
        need("corrupt committed JSONL row" in diagnostic, "missing JSONL recovery diagnostic")
        return {"status": "pass", "committed_rows_before_corruption": 2, "malformed_middle_row": "startup rejected", "diagnostic_recorded": True}
    finally:
        server.stop()


def low_disk_case(root: Path) -> dict:
    """An admission failure must leave a created message with no acknowledged chunk."""
    root.mkdir(parents=True)
    server = Server(root)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            mid = create(http, server.base)
        server.extra_env["FAMILY_LINK_H34_FREE_DISK_FLOOR_BYTES"] = str(10**18)
        server.restart()
        with httpx.Client(timeout=20) as http:
            refused = upload(http, server.base, mid, 0, source)
            need(refused.status_code == 507, f"low-disk upload status {refused.status_code}: {refused.text}")
            status = http.get(f"{server.base}/v1/messages/{mid}/upload", headers=auth("sender"))
            need(status.status_code == 200 and status.json()["audio"]["received"] == [], "low-disk upload state")
            need(status.json()["audio"]["bytes"] == 0, "low-disk acknowledged bytes")
        incoming = root / "data" / "v1_product" / "message_store" / "incoming" / mid
        manifest = json.loads((incoming / "manifest.json").read_text(encoding="utf-8"))
        need(not manifest.get("chunks") and manifest.get("audio_bytes", 0) == 0, "low-disk manifest partial chunk")
        need(not list((incoming / "audio").glob("*.chunk")), "low-disk stored chunk file")
        need(not list((incoming / "work").glob("*.part")), "low-disk temporary chunk file")
        server.extra_env.pop("FAMILY_LINK_H34_FREE_DISK_FLOOR_BYTES")
        server.restart()
        with httpx.Client(timeout=20) as http:
            need(upload(http, server.base, mid, 0, source).status_code == 201, "upload after low-disk recovery")
            need(complete(http, server.base, mid, digest(source), 1).status_code in (200, 201), "completion after low-disk recovery")
        return {"status": "pass", "admission_status": 507, "received_after_refusal": [], "temporary_files_after_refusal": 0, "recovery": "upload and completion passed"}
    finally:
        server.stop()


def completion_disk_case(root: Path) -> dict:
    """Accepted chunks survive a later canonical-WAV admission refusal."""
    root.mkdir(parents=True)
    server = Server(root)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            mid = create(http, server.base)
            need(upload(http, server.base, mid, 0, source).status_code == 201, "completion floor first chunk")
            need(upload(http, server.base, mid, 1, source).status_code == 201, "completion floor second chunk")
        store = root / "data" / "v1_product" / "message_store"
        # Set the floor only after both durable chunks have been admitted.  A
        # huge floor makes the canonical-WAV rejection independent of ordinary
        # filesystem free-space drift during process restart.
        server.extra_env["FAMILY_LINK_H34_FREE_DISK_FLOOR_BYTES"] = str(10**18)
        server.restart()
        with httpx.Client(timeout=20) as http:
            refused = complete(http, server.base, mid, digest(source + source), 2)
            need(refused.status_code == 507, f"canonical WAV floor did not reject: {refused.status_code} {refused.text}")
            status = http.get(f"{server.base}/v1/messages/{mid}/upload", headers=auth("sender"))
            need(status.status_code == 200 and status.json().get("state") == "open", "completion floor upload state")
            need(status.json()["audio"]["received"] == [0, 1], "completion floor lost acknowledged chunks")
        incoming = store / "incoming" / mid
        need(not (incoming / "completion-intent.json").exists(), "completion intent persisted despite 507")
        manifest = json.loads((incoming / "manifest.json").read_text(encoding="utf-8"))
        need(manifest.get("state") == "open" and len(manifest.get("chunks", {})) == 2, "completion 507 changed manifest")
        for seq in (0, 1):
            chunk = incoming / "audio" / f"{seq:06d}.chunk"
            need(chunk.is_file() and digest(chunk.read_bytes()) == digest(source), f"completion 507 damaged chunk {seq}")
        server.extra_env.pop("FAMILY_LINK_H34_FREE_DISK_FLOOR_BYTES")
        server.restart()
        with httpx.Client(timeout=20) as http:
            done = complete(http, server.base, mid, digest(source + source), 2)
            need(done.status_code in (200, 201), f"completion after lowering floor: {done.status_code} {done.text}")
        return {"status": "pass", "chunk_count_before_refusal": 2, "admission_status": 507,
                "floor_bytes": 10**18, "completion_intent_before_recovery": False,
                "recovered_completion": True}
    finally:
        server.stop()


def transient_completion_storage_recovery_case(root: Path) -> dict:
    """A durable intent waits for disk and completes after a later restart.

    This models a real transient capacity loss after the completion intent is
    committed.  It is deliberately distinct from ``completion_disk_case``:
    there the capacity check rejects before an intent exists, while here the
    intent is already durable and startup must retain it for a retry.
    """
    root.mkdir(parents=True)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    source_sha = digest(source)
    expected_wav = digest(_wav_header(CHUNK_BYTES, RATE, 1) + source)
    server = Server(root)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            mid = create(http, server.base)
            need(upload(http, server.base, mid, 0, source).status_code == 201,
                 "transient storage setup upload")

        # The hook fires after completion-intent replacement and its directory
        # fsync, establishing the exact durable state recovery must preserve.
        server.restart(crash="intent")
        with httpx.Client(timeout=20) as http:
            try:
                complete(http, server.base, mid, source_sha, 1)
            except (httpx.TransportError, httpx.RemoteProtocolError):
                pass
        need(server.proc is not None and server.proc.wait(timeout=10) == 86,
             "completion intent crash did not exit 86")

        store = root / "data" / "v1_product" / "message_store"
        incoming = store / "incoming" / mid
        intent_path = incoming / "completion-intent.json"
        need(intent_path.is_file(), "completion intent was not durable before disk recovery")
        intent = json.loads(intent_path.read_text(encoding="utf-8"))
        need(intent.get("message_id") == mid and intent.get("completion", {}).get("source_audio_sha256") == source_sha,
             "durable completion intent changed")

        # Startup sees the intent but cannot allocate the canonical WAV.  The
        # message must remain recoverable and must never be quarantined merely
        # because capacity is temporarily unavailable.
        server.extra_env["FAMILY_LINK_H34_FREE_DISK_FLOOR_BYTES"] = str(10**18)
        server.restart()
        with httpx.Client(timeout=20) as http:
            status = http.get(f"{server.base}/v1/messages/{mid}/upload", headers=auth("sender"))
            need(status.status_code == 200, f"transient disk upload status: {status.status_code} {status.text}")
            payload = status.json()
            need(payload.get("state") == "finalizing", "transient disk did not retain finalizing upload")
            need(payload.get("audio", {}).get("received") == [0], "transient disk lost durable chunk")
            retry = complete(http, server.base, mid, source_sha, 1)
            need(retry.status_code == 507, f"durable completion was not retryable under disk floor: {retry.status_code} {retry.text}")
        need(incoming.is_dir() and intent_path.is_file(), "transient disk removed resumable incoming message")
        quarantined = list((store / "quarantine").glob(f"{mid}-*"))
        need(not quarantined, "transient disk incorrectly quarantined a resumable message")
        recovery = store / "state" / "recovery.jsonl"
        need(recovery.is_file() and "completion waiting for disk" in recovery.read_text(encoding="utf-8"),
             "transient disk recovery diagnostic missing")

        server.extra_env.pop("FAMILY_LINK_H34_FREE_DISK_FLOOR_BYTES")
        server.restart()
        with httpx.Client(timeout=30) as http:
            final = complete(http, server.base, mid, source_sha, 1)
            need(final.status_code == 200, f"completion did not converge after disk recovery: {final.status_code} {final.text}")
            media_hash, media_bytes, _ = stream_hash(http, f"{server.base}/v1/messages/{mid}/audio",
                                                       auth("recipient"), 200)
            need((media_hash, media_bytes) == (expected_wav, CHUNK_BYTES + 44),
                 "transient disk recovered WAV hash/size")
            inbox = http.get(server.base + "/v1/inbox", headers=auth("recipient"))
            need(inbox.status_code == 200, "transient disk recovered inbox unavailable")
            need(len([row for row in inbox.json()["inbox"] if row["message_id"] == mid]) == 1,
                 "transient disk recovered inbox multiplicity")
        need(not incoming.exists(), "completed transient message remained in incoming")
        return {"status": "pass", "intent_crash_exit": 86, "disk_floor_status": 507,
                "recovery_state": "finalizing", "quarantined": False,
                "media_sha256": expected_wav, "media_bytes": CHUNK_BYTES + 44,
                "inbox_count": 1, "restarts": 3}
    finally:
        server.stop()


def suffix_range_case(root: Path) -> dict:
    """A suffix range has exact bytes, digest, and response metadata."""
    root.mkdir(parents=True)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    expected_wav = _wav_header(CHUNK_BYTES, RATE, 1) + source
    expected_sha = digest(expected_wav)
    suffix_bytes = 128
    server = Server(root)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            mid = create(http, server.base)
            need(upload(http, server.base, mid, 0, source).status_code == 201,
                 "suffix range setup upload")
            need(complete(http, server.base, mid, digest(source), 1).status_code in (200, 201),
                 "suffix range setup completion")
            observed_sha, observed_bytes, headers = stream_hash(
                http, f"{server.base}/v1/messages/{mid}/audio",
                {**auth("recipient"), "Range": f"bytes=-{suffix_bytes}"}, 206)
            start = len(expected_wav) - suffix_bytes
            need(observed_sha == digest(expected_wav[start:]), "suffix range hash")
            need(observed_bytes == suffix_bytes, "suffix range byte count")
            need(headers.get("content-range") == f"bytes {start}-{len(expected_wav) - 1}/{len(expected_wav)}",
                 "suffix Content-Range")
            need(headers.get("content-length") == str(suffix_bytes), "suffix Content-Length")
            need(headers.get("accept-ranges") == "bytes", "suffix Accept-Ranges")
            need(headers.get("etag") == f'"{expected_sha}"', "suffix ETag")
        return {"status": "pass", "suffix_bytes": suffix_bytes, "start": start,
                "media_sha256": expected_sha, "content_range": headers["content-range"]}
    finally:
        server.stop()


def completed_before_and_trash_auth_case(root: Path) -> dict:
    """Timestamp selectors are exact and the trash index is admin-only."""
    root.mkdir(parents=True)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    expected_wav_bytes = CHUNK_BYTES + 44
    server = Server(root)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            first = create(http, server.base)
            need(upload(http, server.base, first, 0, source).status_code == 201,
                 "completed-before first upload")
            first_done = complete(http, server.base, first, digest(source), 1)
            need(first_done.status_code in (200, 201), "completed-before first completion")
            time.sleep(0.01)
            second = create(http, server.base)
            need(upload(http, server.base, second, 0, source).status_code == 201,
                 "completed-before second upload")
            second_done = complete(http, server.base, second, digest(source), 1)
            need(second_done.status_code in (200, 201), "completed-before second completion")
            cutoff = second_done.json()["completed_at"]
            preview = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"),
                                json={"completed_before": cutoff})
            need(preview.status_code == 200, f"completed-before preview: {preview.status_code} {preview.text}")
            payload = preview.json()
            need(payload.get("count") == 1 and payload.get("bytes") == expected_wav_bytes,
                 "completed-before count/bytes")
            need(payload.get("affected_inbox_references") == 1,
                 "completed-before inbox references")
            need(payload.get("messages") == [{"message_id": first, "bytes": expected_wav_bytes,
                                               "inbox_references": 1}],
                 "completed-before exact message selection")
            exact = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"),
                              json={"message_ids": [first]})
            need(exact.status_code == 200, "trash authorization setup preview")
            culled = http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"),
                               json={"selection_token": exact.json()["selection_token"]})
            need(culled.status_code == 200 and culled.json().get("trashed") == [first],
                 "trash authorization setup cull")
            child_trash = http.get(server.base + "/v1/admin/messages/trash", headers=auth("sender"))
            need(child_trash.status_code == 403, "child GET trash forbidden")
            admin_trash = http.get(server.base + "/v1/admin/messages/trash", headers=auth("admin"))
            need(admin_trash.status_code == 200 and [row["message_id"] for row in admin_trash.json()["trash"]] == [first],
                 "admin GET trash exact result")
        return {"status": "pass", "completed_before": cutoff, "selected_message_id": first,
                "excluded_message_id": second, "bytes": expected_wav_bytes,
                "child_trash_status": 403}
    finally:
        server.stop()


def preview_token_expiry_case(root: Path) -> dict:
    """A zero-second configured token TTL rejects a freshly issued token."""
    root.mkdir(parents=True)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    server = Server(root, extra_env={"FAMILY_LINK_H34_PREVIEW_TTL_SECONDS": "0"})
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            mid = create(http, server.base)
            need(upload(http, server.base, mid, 0, source).status_code == 201,
                 "expiry setup upload")
            need(complete(http, server.base, mid, digest(source), 1).status_code in (200, 201),
                 "expiry setup completion")
            preview = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"),
                                json={"message_ids": [mid]})
            need(preview.status_code == 200, "expiry preview")
            expired = http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"),
                                json={"selection_token": preview.json()["selection_token"]})
            need(expired.status_code == 400, f"zero-TTL token accepted: {expired.status_code} {expired.text}")
            visible = http.get(f"{server.base}/v1/messages/{mid}/audio", headers=auth("recipient"))
            need(visible.status_code == 200, "expired token changed message visibility")
        return {"status": "pass", "ttl_seconds": 0, "cull_status": 400,
                "message_remained_visible": True}
    finally:
        server.stop()


def corrupt_store_case(root: Path, mode: str) -> dict:
    """One damaged message is quarantined while another remains readable."""
    root.mkdir(parents=True)
    server = Server(root)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    expected_wav = digest(_wav_header(CHUNK_BYTES, RATE, 1) + source)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            good = create(http, server.base)
            bad = create(http, server.base)
            for mid in (good, bad):
                need(upload(http, server.base, mid, 0, source).status_code == 201, "corruption setup upload")
                need(complete(http, server.base, mid, digest(source), 1).status_code in (200, 201), "corruption setup complete")
            if mode in ("invalid_trashed_at", "orphan_trash", "utf8_deletion"):
                preview = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"), json={"message_ids": [bad]})
                need(preview.status_code == 200, "corruption setup preview")
                culled = http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": preview.json()["selection_token"]})
                need(culled.status_code == 200, "corruption setup cull")
        server.stop()
        store = root / "data" / "v1_product" / "message_store"
        if mode in ("audio_type", "utf8_manifest"):
            candidates = list((store / "messages").glob(f"**/{bad}/manifest.json"))
            need(len(candidates) == 1, "corrupt manifest path")
            if mode == "utf8_manifest":
                candidates[0].write_bytes(b"\xff\xfe invalid UTF-8 manifest\n")
            else:
                manifest = json.loads(candidates[0].read_text(encoding="utf-8"))
                manifest["audio"] = "invalid audio type"
                candidates[0].write_text(json.dumps(manifest) + "\n", encoding="utf-8")
        else:
            candidates = list((store / "trash").glob(f"**/{bad}/deletion.json"))
            need(len(candidates) == 1, "corrupt deletion path")
            if mode == "invalid_trashed_at":
                deletion = json.loads(candidates[0].read_text(encoding="utf-8"))
                deletion["trashed_at"] = "not a timestamp"
                candidates[0].write_text(json.dumps(deletion) + "\n", encoding="utf-8")
            elif mode == "orphan_trash":
                candidates[0].unlink()
            elif mode == "utf8_deletion":
                candidates[0].write_bytes(b"\xff\xfe invalid UTF-8 deletion\n")
            else:
                raise ValueError(f"unknown corruption mode: {mode}")
        server.restart()
        with httpx.Client(timeout=20) as http:
            good_hash, good_bytes, _ = stream_hash(http, f"{server.base}/v1/messages/{good}/audio", auth("recipient"), 200)
            need((good_hash, good_bytes) == (expected_wav, CHUNK_BYTES + 44), "healthy message lost during quarantine")
            inaccessible = http.get(f"{server.base}/v1/messages/{bad}/audio", headers=auth("recipient"))
            need(inaccessible.status_code == 404, f"corrupt message visible: {inaccessible.status_code}")
        quarantined = list((store / "quarantine").glob(f"{bad}-*"))
        need(len(quarantined) == 1, f"corrupt {mode} message not quarantined once")
        recovery = store / "state" / "recovery.jsonl"
        need(recovery.is_file() and bad in recovery.read_text(encoding="utf-8"), "missing quarantine diagnostic")
        return {"status": "pass", "case": mode, "healthy_message_sha256": good_hash, "quarantined_directories": len(quarantined), "diagnostic_recorded": True}
    finally:
        server.stop()


def journaled_orphan_trash_case(root: Path) -> dict:
    """A cull journal can identify a moved directory missing deletion metadata."""
    root.mkdir(parents=True)
    server = Server(root)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    expected_wav = digest(_wav_header(CHUNK_BYTES, RATE, 1) + source)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            mid = create(http, server.base)
            need(upload(http, server.base, mid, 0, source).status_code == 201, "journal orphan setup upload")
            need(complete(http, server.base, mid, digest(source), 1).status_code in (200, 201), "journal orphan setup complete")
        server.restart(crash="cull_intent")
        with httpx.Client(timeout=20) as http:
            preview = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"), json={"message_ids": [mid]})
            need(preview.status_code == 200, "journal orphan preview")
            try:
                http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": preview.json()["selection_token"]})
            except httpx.TransportError:
                pass
        need(server.proc is not None and server.proc.wait(timeout=10) == 86, "cull_intent crash for orphan fixture")
        store = root / "data" / "v1_product" / "message_store"
        canonical = list((store / "messages").glob(f"**/{mid}"))
        need(len(canonical) == 1, "journal orphan canonical source")
        dest = store / "trash" / canonical[0].relative_to(store / "messages")
        dest.parent.mkdir(parents=True, exist_ok=True)
        canonical[0].rename(dest)
        need(not (dest / "deletion.json").exists(), "journal orphan fixture unexpectedly has deletion metadata")
        server.restart()
        with httpx.Client(timeout=20) as http:
            hidden = http.get(f"{server.base}/v1/messages/{mid}/audio", headers=auth("recipient"))
            need(hidden.status_code == 404, "journaled orphan exposed media before restore")
            need((dest / "deletion.json").is_file(), "journaled orphan deletion metadata not rebuilt")
            restored = http.post(f"{server.base}/v1/admin/messages/{mid}/restore", headers=auth("admin"))
            need(restored.status_code == 200, f"journaled orphan restore {restored.status_code}: {restored.text}")
            media_hash, media_bytes, _ = stream_hash(http, f"{server.base}/v1/messages/{mid}/audio", auth("recipient"), 200)
            need((media_hash, media_bytes) == (expected_wav, CHUNK_BYTES + 44), "journaled orphan restored hash")
        audit_path = store / "state" / "admin-audit.jsonl"
        audit = [json.loads(line) for line in audit_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        need(len([row for row in audit if row.get("action") == "cull" and row.get("message_id") == mid]) == 1, "journaled orphan cull audit")
        return {"status": "pass", "journal": "cull_intent", "deletion_metadata_rebuilt": True, "restored_media_sha256": media_hash, "cull_audit_rows": 1}
    finally:
        server.stop()


def invalid_utf8_cull_journal_case(root: Path) -> dict:
    """A damaged cull intent is diagnosed without preventing healthy startup."""
    root.mkdir(parents=True)
    server = Server(root)
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    expected_wav = digest(_wav_header(CHUNK_BYTES, RATE, 1) + source)
    server.start()
    try:
        with httpx.Client(timeout=20) as http:
            good = create(http, server.base)
            bad = create(http, server.base)
            for mid in (good, bad):
                need(upload(http, server.base, mid, 0, source).status_code == 201, "UTF-8 journal setup upload")
                need(complete(http, server.base, mid, digest(source), 1).status_code in (200, 201), "UTF-8 journal setup complete")
        server.restart(crash="cull_intent")
        with httpx.Client(timeout=20) as http:
            preview = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"), json={"message_ids": [bad]})
            need(preview.status_code == 200, "UTF-8 journal preview")
            try:
                http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": preview.json()["selection_token"]})
            except httpx.TransportError:
                pass
        need(server.proc is not None and server.proc.wait(timeout=10) == 86, "UTF-8 journal fixture crash")
        store = root / "data" / "v1_product" / "message_store"
        journals = list((store / "state" / "cull-intents").glob("*.json"))
        need(len(journals) == 1, "UTF-8 journal fixture missing intent")
        journals[0].write_bytes(b"\xff\xfe invalid UTF-8 cull journal\n")
        server.restart()
        with httpx.Client(timeout=20) as http:
            good_hash, good_bytes, _ = stream_hash(http, f"{server.base}/v1/messages/{good}/audio", auth("recipient"), 200)
            need((good_hash, good_bytes) == (expected_wav, CHUNK_BYTES + 44), "healthy message lost to corrupt cull journal")
            bad_media = http.get(f"{server.base}/v1/messages/{bad}/audio", headers=auth("recipient"))
            need(bad_media.status_code in (200, 404), f"corrupt cull journal unsafe message status {bad_media.status_code}")
            if bad_media.status_code == 200:
                need(digest(bad_media.content) == expected_wav, "corrupt cull journal changed media")
        recovery = store / "state" / "recovery.jsonl"
        need(recovery.is_file() and "cull" in recovery.read_text(encoding="utf-8").lower(), "corrupt cull journal diagnostic")
        return {"status": "pass", "healthy_message_sha256": good_hash, "affected_message_status": bad_media.status_code, "diagnostic_recorded": True}
    finally:
        server.stop()


def run(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json-out", type=Path, default=Path("h34-qualification.json"))
    parser.add_argument("--skip-faults", action="store_true", help="Run the 180-second transfer only")
    args = parser.parse_args(argv)
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True, check=False)
    result = {"schema": "h34-qualification/1", "run_id": str(uuid.uuid4()), "status": "fail", "source_revision": revision.stdout.strip() if revision.returncode == 0 else None, "environment": {"python": sys.version.split()[0], "platform": platform.platform(), "implementation": platform.python_implementation(), "durability_scope": "process crash; no power-loss claim"}, "checks": {}}
    try:
        with tempfile.TemporaryDirectory(prefix="family-link-h34-qualification-") as td:
            output = qualification(Path(td))
            result.update(output)
            result["checks"]["jsonl_fail_closed"] = malformed_jsonl_case(Path(td) / "jsonl")
            result["checks"]["low_disk_admission"] = low_disk_case(Path(td) / "low-disk")
            result["checks"]["low_disk_completion"] = completion_disk_case(Path(td) / "low-disk-completion")
            result["checks"]["transient_completion_storage_recovery"] = transient_completion_storage_recovery_case(
                Path(td) / "transient-completion-storage")
            result["checks"]["suffix_range"] = suffix_range_case(Path(td) / "suffix-range")
            result["checks"]["completed_before_and_trash_auth"] = completed_before_and_trash_auth_case(
                Path(td) / "completed-before-and-trash-auth")
            result["checks"]["preview_token_expiry"] = preview_token_expiry_case(
                Path(td) / "preview-token-expiry")
            for mode in ("audio_type", "invalid_trashed_at", "orphan_trash", "utf8_manifest", "utf8_deletion"):
                result["checks"]["quarantine_" + mode] = corrupt_store_case(Path(td) / ("corrupt-" + mode), mode)
            result["checks"]["journaled_orphan_trash"] = journaled_orphan_trash_case(Path(td) / "journaled-orphan")
            result["checks"]["invalid_utf8_cull_journal"] = invalid_utf8_cull_journal_case(Path(td) / "invalid-utf8-cull-journal")
            for name, check in output["checks"].items():
                print(f"PASS {name}" if check["status"] == "pass" else f"FAIL {name}", flush=True)
            if not args.skip_faults:
                result["faults"] = fault_matrix(Path(td))
                for row in result["faults"]:
                    print(f"{row['status'].upper()} fault {row['boundary']}", flush=True)
            result["status"] = "pass" if all(c["status"] == "pass" for c in result["checks"].values()) and all(r["status"] == "pass" for r in result.get("faults", [])) else "fail"
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        print("FAIL " + result["error"], file=sys.stderr, flush=True)
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"RESULT {result['status'].upper()} {args.json_out}", flush=True)
    return 0 if result["status"] == "pass" else 1


def fault_matrix(root: Path) -> list[dict]:
    """Fault cases use isolated roots and a two-second message per boundary."""
    results = []
    for operation, names in BOUNDARIES.items():
        for boundary in names:
            case_root = root / "faults" / boundary
            case_root.mkdir(parents=True)
            try:
                results.append(fault_case(case_root, operation, boundary))
            except Exception as exc:
                results.append({"boundary": boundary, "operation": operation, "status": "fail", "error": f"{type(exc).__name__}: {exc}"})
    return results


def fault_case(root: Path, operation: str, boundary: str) -> dict:
    source = b"\x00\x10" * (CHUNK_BYTES // 2)
    source_sha = digest(source)
    expected_wav = digest(_wav_header(CHUNK_BYTES, RATE, 1) + source)
    server = Server(root)
    server.start()
    mid = None
    broadcast = boundary.startswith("inbox_")
    try:
        with httpx.Client(timeout=20) as http:
            mid = create(http, server.base, broadcast=broadcast)
            if operation != "chunk":
                need(upload(http, server.base, mid, 0, source).status_code == 201, "fault setup upload")
            if operation in ("cull", "restore"):
                need(complete(http, server.base, mid, source_sha, 1).status_code in (200, 201), "fault setup complete")
            if operation == "restore":
                token = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"), json={"message_ids": [mid]}).json()["selection_token"]
                need(http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": token}).status_code == 200, "fault setup cull")
        server.restart(crash=boundary)
        with httpx.Client(timeout=20) as http:
            try:
                if operation == "chunk":
                    upload(http, server.base, mid, 0, source)
                elif operation == "complete":
                    complete(http, server.base, mid, source_sha, 1)
                elif operation == "cull":
                    token = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"), json={"message_ids": [mid]}).json()["selection_token"]
                    http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": token})
                else:
                    http.post(f"{server.base}/v1/admin/messages/{mid}/restore", headers=auth("admin"))
            except (httpx.TransportError, httpx.RemoteProtocolError):
                pass
        need(server.proc is not None and server.proc.wait(timeout=10) == 86, f"fault {boundary} did not exit 86")
        server.restart()
        with httpx.Client(timeout=30) as http:
            audio_url = f"{server.base}/v1/messages/{mid}/audio"
            initial = http.get(audio_url, headers=auth("recipient"))
            need(initial.status_code in (200, 404), f"unsafe post-crash media visibility {initial.status_code}")
            if initial.status_code == 200:
                need(len(initial.content) == CHUNK_BYTES + 44 and digest(initial.content) == expected_wav,
                     "post-crash route exposed incomplete or changed media")
            if operation in ("chunk", "complete") and initial.status_code != 200:
                status = http.get(f"{server.base}/v1/messages/{mid}/upload", headers=auth("sender"))
                need(status.status_code == 200, "recoverable upload status")
                if operation == "chunk":
                    need(upload(http, server.base, mid, 0, source).status_code in (200, 201), "recovered chunk retry")
                done = complete(http, server.base, mid, source_sha, 1)
                need(done.status_code in (200, 201), f"recovered completion {done.status_code}: {done.text}")
            if operation in ("cull", "restore"):
                if initial.status_code == 404:
                    restored = http.post(f"{server.base}/v1/admin/messages/{mid}/restore", headers=auth("admin"))
                    need(restored.status_code == 200, f"recover trash {restored.status_code}: {restored.text}")
                elif operation == "cull":
                    preview = http.post(server.base + "/v1/admin/messages/cull/preview", headers=auth("admin"), json={"message_ids": [mid]})
                    need(preview.status_code == 200, "retry cull preview after precommit crash")
                    retry_cull = http.post(server.base + "/v1/admin/messages/cull", headers=auth("admin"), json={"selection_token": preview.json()["selection_token"]})
                    need(retry_cull.status_code == 200, "retry cull after precommit crash")
                    restored = http.post(f"{server.base}/v1/admin/messages/{mid}/restore", headers=auth("admin"))
                    need(restored.status_code == 200, "restore after retried cull")
            media_hash, media_size, _ = stream_hash(http, audio_url, auth("recipient"), 200)
            need((media_hash, media_size) == (expected_wav, CHUNK_BYTES + 44), "fault recovered WAV hash/size")
            rows = http.get(server.base + "/v1/inbox", headers=auth("recipient")).json()["inbox"]
            need(len([row for row in rows if row["message_id"] == mid]) == 1, "fault inbox multiplicity")
            if broadcast:
                other_rows = http.get(server.base + "/v1/inbox", headers=auth("unrelated")).json()["inbox"]
                need(len([row for row in other_rows if row["message_id"] == mid]) == 1, "broadcast second recipient multiplicity")
            marker = list((root / "data" / "v1_product" / "message_store" / "messages").glob(f"**/{mid}/complete"))
            need(len(marker) == 1, "fault completion marker multiplicity")
        server.restart()
        with httpx.Client(timeout=30) as http:
            again_hash, again_size, _ = stream_hash(http, f"{server.base}/v1/messages/{mid}/audio", auth("recipient"), 200)
            need((again_hash, again_size) == (expected_wav, CHUNK_BYTES + 44), "second restart WAV hash/size")
            rows = http.get(server.base + "/v1/inbox", headers=auth("recipient")).json()["inbox"]
            need(len([row for row in rows if row["message_id"] == mid]) == 1, "second restart inbox multiplicity")
            if broadcast:
                other_rows = http.get(server.base + "/v1/inbox", headers=auth("unrelated")).json()["inbox"]
                need(len([row for row in other_rows if row["message_id"] == mid]) == 1, "second restart broadcast recipient multiplicity")
            store = root / "data" / "v1_product" / "message_store"
            markers = list((store / "messages").glob(f"**/{mid}/complete"))
            need(len(markers) == 1, "second restart marker multiplicity")
            need(not (store / "incoming" / mid).exists(), "second restart stale incoming directory")
            need(not list((store / "trash").glob(f"**/{mid}")), "second restart stale trash directory")
            canonical = markers[0].parent
            manifest = json.loads((canonical / "manifest.json").read_text(encoding="utf-8"))
            need(manifest.get("state") == "complete", "second restart manifest state")
            need(manifest.get("source_audio_sha256") == source_sha, "second restart source hash")
            need(manifest.get("audio", {}).get("sha256") == expected_wav, "second restart manifest media hash")
            inventory = {"canonical_directories": 1, "incoming_directories": 0, "trash_directories": 0, "complete_markers": 1, "remaining_chunk_files": len(list(canonical.glob("audio/*.chunk"))), "recipient_inbox_rows": 1, "broadcast_second_inbox_rows": 1 if broadcast else 0}
            need(inventory["remaining_chunk_files"] == 0, "second restart leftover chunk files")
            if operation in ("cull", "restore"):
                audit_path = store / "state" / "admin-audit.jsonl"
                audit_rows = [json.loads(line) for line in audit_path.read_text(encoding="utf-8").splitlines() if line.strip()]
                cull_events = [row for row in audit_rows if row.get("action") == "cull" and row.get("message_id") == mid]
                need(len(cull_events) == 1, f"cull audit multiplicity after {boundary}: {len(cull_events)}")
                inventory["cull_audit_rows"] = len(cull_events)
            if operation == "restore":
                restore_events = [row for row in audit_rows if row.get("action") == "restore" and row.get("message_id") == mid]
                need(len(restore_events) == 1, f"restore audit multiplicity after {boundary}: {len(restore_events)}")
                inventory["restore_audit_rows"] = len(restore_events)
        return {"boundary": boundary, "operation": operation, "status": "pass", "broadcast": broadcast, "initial_media_status": initial.status_code, "media_sha256": media_hash, "source_sha256": source_sha, "inbox_count": 1, "restarts": 2, "inventory": inventory}
    finally:
        server.stop()


if __name__ == "__main__":
    raise SystemExit(run())
