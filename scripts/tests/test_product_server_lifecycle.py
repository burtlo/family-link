"""Process-level lifecycle and readiness regressions for the product server."""
from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from scripts.tests.test_lab_p1_host import DESK, server


ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "server/device/main/server.py"


class ProductServerLifecycleTests(unittest.TestCase):
    def _free_port(self) -> int:
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        return port

    def _request(self, port: int, path: str, bearer: str | None = None) -> tuple[int, dict]:
        request = urllib.request.Request(f"http://127.0.0.1:{port}{path}")
        if bearer is not None:
            request.add_header("Authorization", f"Bearer {bearer}")
        try:
            with urllib.request.urlopen(request, timeout=1.5) as response:
                return response.status, json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            try:
                return exc.code, json.loads(exc.read().decode("utf-8"))
            finally:
                exc.close()

    def _start(self, port: int, runtime: Path, run_id: str,
               ignore_sigint: bool = False) -> subprocess.Popen:
        environment = os.environ.copy()
        environment["FAMILY_LINK_RUNTIME_DIR"] = str(runtime)
        preexec_fn = None
        if ignore_sigint:
            preexec_fn = lambda: signal.signal(signal.SIGINT, signal.SIG_IGN)
        process = subprocess.Popen(
            [
                sys.executable, str(SERVER),
                "--bind", "127.0.0.1", "--port", str(port), "--run-id", run_id,
            ],
            cwd=ROOT,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=preexec_fn,
        )
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if process.poll() is not None:
                stderr = process.stderr.read().decode("utf-8", errors="replace")
                self.fail(f"server exited during startup: {stderr[-1000:]}")
            try:
                status, body = self._request(port, "/health")
                if status == 200 and body.get("status") == "live":
                    return process
            except (OSError, ValueError):
                time.sleep(0.05)
        process.kill()
        process.wait(timeout=3)
        self.fail("server did not become live")

    def test_repeated_real_process_ctrl_c_restart_and_authenticated_hangout(self):
        with tempfile.TemporaryDirectory(prefix="family-link-lifecycle-runtime-") as runtime_name:
            runtime = Path(runtime_name)
            port = self._free_port()
            for iteration in range(3):
                process = self._start(port, runtime, f"lifecycle-{iteration}")
                try:
                    status, roster = self._request(port, "/v2/hangout", DESK)
                    self.assertEqual(status, 200)
                    self.assertEqual(roster["hangout_id"], "hangout-kitchen")
                finally:
                    process.send_signal(signal.SIGINT)
                    process.wait(timeout=4)
                    self.assertIsNotNone(process.returncode)
                    process.stdout.close()
                    process.stderr.close()

    def test_sigkill_after_hangout_allows_immediate_rebind(self):
        with tempfile.TemporaryDirectory(prefix="family-link-sigkill-runtime-") as runtime_name:
            runtime = Path(runtime_name)
            port = self._free_port()
            process = self._start(port, runtime, "sigkill-before-restart")
            status, roster = self._request(port, "/v2/hangout", DESK)
            self.assertEqual(status, 200)
            self.assertEqual(roster["hangout_id"], "hangout-kitchen")
            process.kill()
            process.wait(timeout=4)
            process.stdout.close()
            process.stderr.close()

            replacement = self._start(port, runtime, "sigkill-replacement")
            try:
                status, roster = self._request(port, "/v2/hangout", DESK)
                self.assertEqual(status, 200)
                self.assertEqual(roster["hangout_id"], "hangout-kitchen")
            finally:
                replacement.send_signal(signal.SIGTERM)
                replacement.wait(timeout=4)
                replacement.stdout.close()
                replacement.stderr.close()

    def test_incomplete_request_body_is_bounded_and_server_stays_usable(self):
        with tempfile.TemporaryDirectory(prefix="family-link-request-runtime-") as runtime_name:
            runtime = Path(runtime_name)
            port = self._free_port()
            process = self._start(port, runtime, "bounded-request")
            try:
                client = socket.create_connection(("127.0.0.1", port), timeout=2)
                client.settimeout(server.REQUEST_TIMEOUT_S + 3)
                client.sendall(
                    b"POST /v1/control HTTP/1.1\r\n"
                    b"Host: 127.0.0.1\r\n"
                    b"Authorization: Bearer " + DESK.encode("ascii") + b"\r\n"
                    b"Content-Type: application/json\r\n"
                    b"Content-Length: 16\r\n\r\n{}"
                )
                response = client.recv(4096)
                client.close()
                self.assertTrue(response.startswith(b"HTTP/1.1 400"), response[:80])

                status, roster = self._request(port, "/v2/hangout", DESK)
                self.assertEqual(status, 200)
                self.assertEqual(roster["hangout_id"], "hangout-kitchen")
            finally:
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=4)
                process.stdout.close()
                process.stderr.close()

    @unittest.skipIf(os.name == "nt", "preexec_fn is POSIX-only")
    def test_ctrl_c_exits_when_launcher_inherits_ignored_sigint(self):
        with tempfile.TemporaryDirectory(prefix="family-link-signal-runtime-") as runtime_name:
            process = self._start(self._free_port(), Path(runtime_name), "ignored-sigint",
                                  ignore_sigint=True)
            process.send_signal(signal.SIGINT)
            process.wait(timeout=4)
            self.assertEqual(process.returncode, 0)
            process.stdout.close()
            process.stderr.close()

    def test_damaged_persisted_delivery_is_not_reported_ready(self):
        with tempfile.TemporaryDirectory(prefix="family-link-readiness-runtime-") as runtime_name:
            runtime = Path(runtime_name)
            (runtime / "accepted-tombstones.json").write_text(json.dumps({
                "schema": "p1-accepted-tombstones/v1",
                "records": [{
                    "client_message_id": "11111111-1111-4111-8111-111111111111",
                    "message_id": "msg-missing-media",
                    "sender_user_id": "user-maya",
                    "accepted_at": "2026-10-24T00:00:00Z",
                    "recipient_user_ids": ["user-jordan"],
                    "delivery_item": {
                        "message_id": "msg-missing-media",
                        "sender_user_id": "user-maya",
                        "media": {"audio": {"format_id": "pcm_s16le"}},
                    },
                }],
            }), encoding="utf-8")
            port = self._free_port()
            process = self._start(port, runtime, "degraded-startup")
            try:
                status, health = self._request(port, "/health")
                self.assertEqual(status, 200)
                self.assertEqual(health["status"], "live")
                self.assertFalse(health["ready"])
                status, ready = self._request(port, "/ready")
                self.assertEqual(status, 503)
                self.assertEqual(ready["status"], "degraded")
                self.assertIn("accepted_media_missing", ready["issues"])
            finally:
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=4)
                process.stdout.close()
                process.stderr.close()

    def test_malformed_persisted_tombstones_are_not_reported_ready(self):
        with tempfile.TemporaryDirectory(prefix="family-link-malformed-runtime-") as runtime_name:
            runtime = Path(runtime_name)
            (runtime / "accepted-tombstones.json").write_text("{not-json", encoding="utf-8")
            port = self._free_port()
            process = self._start(port, runtime, "malformed-startup")
            try:
                status, ready = self._request(port, "/ready")
                self.assertEqual(status, 503)
                self.assertEqual(ready["status"], "degraded")
                self.assertIn("accepted_tombstone_unreadable", ready["issues"])
            finally:
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=4)
                process.stdout.close()
                process.stderr.close()

