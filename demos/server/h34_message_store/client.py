#!/usr/bin/env python3
"""Automated h34 canonical message_store tests (PCM path)."""

from __future__ import annotations

import argparse
import hashlib
import math
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import httpx

from demos.server._shared.registry import load_devices

ROOT = Path(__file__).resolve().parents[3]
SERVER_PY = Path(__file__).resolve().parent / "server.py"


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pcm_chunk(seconds: float = 2.0, *, freq: float = 440.0) -> bytes:
    rate = 16000
    n = int(rate * seconds)
    frames = bytearray()
    for i in range(n):
        sample = int(0.2 * 32767 * math.sin(2 * math.pi * freq * i / rate))
        frames += struct.pack("<h", sample)
    return bytes(frames)


def put_chunk(
    http: httpx.Client,
    base: str,
    token: str,
    message_id: str,
    seq: int,
    body: bytes,
    start_ms: int,
    duration_ms: int = 2000,
) -> httpx.Response:
    headers = {
        **auth(token),
        "Content-Type": "application/octet-stream",
        "X-Chunk-SHA256": sha256_hex(body),
        "X-Chunk-Start-Ms": str(start_ms),
        "X-Chunk-Duration-Ms": str(duration_ms),
        "X-Chunk-Bytes": str(len(body)),
    }
    return http.put(
        f"{base}/v1/messages/{message_id}/audio/{seq}",
        content=body,
        headers=headers,
    )


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _venv_python() -> str:
    cand = ROOT / ".venv" / "bin" / "python"
    if cand.is_file():
        return str(cand)
    return sys.executable


def _wait_port(port: int, timeout: float = 8.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            try:
                sock.connect(("127.0.0.1", port))
                return True
            except OSError:
                time.sleep(0.05)
    return False


def _spawn_server(port: int, family_root: Path) -> subprocess.Popen:
    env = os.environ.copy()
    env["PYTHONPATH"] = str(ROOT) + os.pathsep + env.get("PYTHONPATH", "")
    env["FAMILY_LINK_ROOT"] = str(family_root)
    return subprocess.Popen(
        [_venv_python(), str(SERVER_PY), "--host", "127.0.0.1", "--port", str(port)],
        cwd=str(ROOT),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def test_restart_survival(devices) -> int:
    sender = devices["box-a"]
    recipient = devices["box-b"]
    chunk0 = pcm_chunk(2.0)
    chunk1 = pcm_chunk(2.0, freq=523.25)
    source_hash = sha256_hex(chunk0 + chunk1)

    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        port = _free_port()
        proc = _spawn_server(port, root)
        base = f"http://127.0.0.1:{port}"
        try:
            if not _wait_port(port):
                return fail("restart server did not bind")
            with httpx.Client(timeout=15.0) as http:
                created = http.post(
                    f"{base}/v1/messages",
                    json={
                        "protocol": "family-message/1",
                        "to_user_id": recipient.id,
                        "audio": {
                            "codec": "pcm_s16le",
                            "sample_rate_hz": 16000,
                            "channels": 1,
                            "target_chunk_ms": 2000,
                        },
                    },
                    headers=auth(sender.token),
                )
                if created.status_code != 201:
                    return fail(f"restart create {created.status_code}")
                message_id = created.json()["message_id"]
                r0 = put_chunk(http, base, sender.token, message_id, 0, chunk0, 0)
                if r0.status_code != 201:
                    return fail(f"restart put0 {r0.status_code}")

            proc.terminate()
            proc.wait(timeout=3)

            proc = _spawn_server(port, root)
            if not _wait_port(port):
                return fail("restart server pass2 did not bind")
            with httpx.Client(timeout=15.0) as http:
                status = http.get(
                    f"{base}/v1/messages/{message_id}/upload",
                    headers=auth(sender.token),
                )
                if status.status_code != 200 or status.json().get("state") != "open":
                    return fail(f"restart upload status {status.status_code} {status.text}")
                received = status.json()["audio"]["received"]
                if received != [0]:
                    return fail(f"restart expected [0] got {received}")

                r1 = put_chunk(http, base, sender.token, message_id, 1, chunk1, 2000)
                if r1.status_code != 201:
                    return fail(f"restart put1 {r1.status_code}")

                done = http.post(
                    f"{base}/v1/messages/{message_id}/complete",
                    json={
                        "audio_chunks": 2,
                        "duration_ms": 4000,
                        "closed_reason": "button",
                        "source_audio_sha256": source_hash,
                    },
                    headers=auth(sender.token),
                )
                if done.status_code != 201:
                    return fail(f"restart complete {done.status_code} {done.text}")
                wav_sha = done.json()["audio"]["sha256"]

            proc.terminate()
            proc.wait(timeout=3)

            proc = _spawn_server(port, root)
            if not _wait_port(port):
                return fail("restart server pass3 did not bind")
            with httpx.Client(timeout=15.0) as http:
                audio = http.get(
                    f"{base}/v1/messages/{message_id}/audio",
                    headers=auth(recipient.token),
                )
                if audio.status_code != 200:
                    return fail(f"restart GET audio {audio.status_code}")
                if audio.content[:4] != b"RIFF":
                    return fail("restart not wav")
                if sha256_hex(audio.content) != wav_sha:
                    return fail("restart wav hash mismatch after reboot")

                inbox = http.get(f"{base}/v1/inbox", headers=auth(recipient.token))
                if inbox.status_code != 200:
                    return fail(f"restart inbox {inbox.status_code}")
                ids = [row["message_id"] for row in inbox.json().get("inbox", [])]
                if message_id not in ids:
                    return fail(f"restart inbox missing message {ids}")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
    print("-- PASS restart_survival")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    devices = load_devices()
    sender = devices["box-a"]
    recipient = devices["box-b"]

    chunk = pcm_chunk(2.0)
    chunk_b = pcm_chunk(2.0, freq=880.0)
    chunks_for_complete = [chunk, chunk, chunk]
    source_hash = sha256_hex(b"".join(chunks_for_complete))

    with httpx.Client(timeout=15.0) as http:
        created = http.post(
            f"{base}/v1/messages",
            json={
                "protocol": "family-message/1",
                "to_user_id": recipient.id,
                "audio": {
                    "codec": "pcm_s16le",
                    "sample_rate_hz": 16000,
                    "channels": 1,
                    "target_chunk_ms": 2000,
                },
            },
            headers=auth(sender.token),
        )
        if created.status_code != 201:
            return fail(f"create {created.status_code} {created.text}")
        message_id = created.json()["message_id"]

        for seq, start in ((2, 4000), (0, 0), (1, 2000)):
            resp = put_chunk(http, base, sender.token, message_id, seq, chunk, start)
            if resp.status_code != 201:
                return fail(f"put seq{seq} {resp.status_code}")

        dup = put_chunk(http, base, sender.token, message_id, 1, chunk, 2000)
        if dup.status_code != 200:
            return fail(f"duplicate chunk expected 200 got {dup.status_code}")

        conflict = put_chunk(http, base, sender.token, message_id, 1, chunk_b, 2000)
        if conflict.status_code != 409:
            return fail(f"conflict expected 409 got {conflict.status_code}")

        status = http.get(
            f"{base}/v1/messages/{message_id}/upload", headers=auth(sender.token)
        )
        if status.status_code != 200:
            return fail(f"upload status {status.status_code}")
        if status.json()["audio"]["received"] != [0, 1, 2]:
            return fail(f"received sequences {status.json()['audio']['received']}")

        missing_block = http.post(
            f"{base}/v1/messages/{message_id}/complete",
            json={
                "audio_chunks": 4,
                "duration_ms": 8000,
                "closed_reason": "button",
                "source_audio_sha256": source_hash,
            },
            headers=auth(sender.token),
        )
        if missing_block.status_code != 409:
            return fail(f"missing finalize expected 409 got {missing_block.status_code}")

        done = http.post(
            f"{base}/v1/messages/{message_id}/complete",
            json={
                "audio_chunks": 3,
                "duration_ms": 6000,
                "closed_reason": "button",
                "source_audio_sha256": source_hash,
            },
            headers=auth(sender.token),
        )
        if done.status_code not in (200, 201):
            return fail(f"complete {done.status_code} {done.text}")
        manifest = done.json()
        wav_sha = manifest["audio"]["sha256"]

        store_root = ROOT / "data" / "v1_product" / "message_store"
        complete_marker = list(store_root.glob(f"messages/**/{message_id}/complete"))
        if not complete_marker:
            return fail("missing complete marker under messages/YYYY/MM/")
        chunk_left = list(complete_marker[0].parent.glob("audio/*.chunk"))
        if chunk_left:
            return fail(f"chunks not removed after finalize: {chunk_left}")

        inbox = http.get(f"{base}/v1/inbox", headers=auth(recipient.token))
        if inbox.status_code != 200:
            return fail(f"inbox {inbox.status_code}")
        inbox_ids = [r["message_id"] for r in inbox.json().get("inbox", [])]
        if message_id not in inbox_ids:
            return fail("completed message not in recipient inbox")

        audio = http.get(
            f"{base}/v1/messages/{message_id}/audio", headers=auth(recipient.token)
        )
        if audio.status_code != 200:
            return fail(f"GET audio {audio.status_code}")
        if not audio.headers.get("accept-ranges", "").lower().startswith("bytes"):
            return fail("missing Accept-Ranges")
        clen = int(audio.headers.get("content-length", "0"))
        if clen < 44 or audio.content[:4] != b"RIFF":
            return fail("not wav media")
        if sha256_hex(audio.content) != wav_sha:
            return fail("wav content hash mismatch")

        mid = clen // 2
        partial = http.get(
            f"{base}/v1/messages/{message_id}/audio",
            headers={**auth(recipient.token), "Range": f"bytes={mid}-"},
        )
        if partial.status_code != 206:
            return fail(f"range GET expected 206 got {partial.status_code}")
        cr = partial.headers.get("content-range", "")
        if not cr.startswith(f"bytes {mid}-"):
            return fail(f"bad content-range {cr}")

        preview = http.post(
            f"{base}/v1/admin/messages/cull/preview",
            json={"message_ids": [message_id]},
            headers=auth(sender.token),
        )
        if preview.status_code != 200:
            return fail(f"cull preview {preview.status_code}")
        token = preview.json()["selection_token"]
        before_hash = sha256_hex(audio.content)
        culled = http.post(
            f"{base}/v1/admin/messages/cull",
            json={"selection_token": token},
            headers=auth(sender.token),
        )
        if culled.status_code != 200:
            return fail(f"cull {culled.status_code}")
        restored = http.post(
            f"{base}/v1/admin/messages/{message_id}/restore",
            headers=auth(sender.token),
        )
        if restored.status_code != 200:
            return fail(f"restore {restored.status_code}")
        if restored.json().get("media_sha256") != wav_sha:
            return fail("restore hash mismatch")

        audio2 = http.get(
            f"{base}/v1/messages/{message_id}/audio", headers=auth(recipient.token)
        )
        if sha256_hex(audio2.content) != before_hash:
            return fail("media changed after trash/restore")

        print(
            f"-- PASS h34_message_store message_id={message_id} "
            f"chunk_bytes={len(chunk)} wav_bytes={clen}"
        )

    rc = test_restart_survival(devices)
    if rc != 0:
        return rc
    return 0


if __name__ == "__main__":
    sys.exit(main())
