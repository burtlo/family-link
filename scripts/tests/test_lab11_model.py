"""Compile and run the production LAB-11 C model."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import socket
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-11-send-receipt"
LAB10 = ROOT / "client/labs/lab-10-record-canvas/manifest.lab.json"
LAB03 = ROOT / "server/labs/lab-03-media/fixtures/manifest.json"


def _compiler() -> str:
    for name in ("gcc", "cc", "clang"):
        found = shutil.which(name)
        if found:
            return found
    raise unittest.SkipTest("no host C compiler")


class Lab11ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab11-model-")
        cls.binary = Path(cls.temp.name) / "lab11_model_test.exe"
        subprocess.run([
            _compiler(), "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(LAB / "components/lab11_capture/include"),
            "-I", str(LAB / "components/lab11_send/include"),
            str(LAB / "components/lab11_capture/lab11_capture.c"),
            str(LAB / "components/lab11_capture/lab11_sha256.c"),
            str(LAB / "components/lab11_send/lab11_send.c"),
            str(LAB / "tests/model_test.c"),
            "-o", str(cls.binary), "-lws2_32",
        ], check=True, cwd=ROOT)
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "lab11_fixture_server_model", ROOT / "server/labs/lab-11-send-receipt/server.py")
        cls.server = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.server)
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        cls.port = probe.getsockname()[1]
        probe.close()
        cls.httpd = cls.server.create_server("127.0.0.1", cls.port, "lab11-model")
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.thread.join(timeout=2)
        cls.temp.cleanup()

    def run_case(self, name: str, env: dict | None = None) -> subprocess.CompletedProcess:
        merged = os.environ.copy()
        if env:
            merged.update(env)
        return subprocess.run([str(self.binary), name], check=False, cwd=ROOT, env=merged,
                              capture_output=True, text=True)

    def test_lab11_progress_level(self):
        completed = self.run_case("progress")
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_sha256_boot_ownership_progress_overflow_account(self):
        for name in ("sha256", "boot", "ownership", "progress", "overflow", "account"):
            with self.subTest(name=name):
                completed = self.run_case(name)
                self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_lab11_frozen_hash(self):
        policy = json.loads(LAB10.read_text(encoding="utf-8"))["capture_policy"]
        self.assertEqual(policy["accepted_point_cap"], 4096)
        self.assertEqual(policy["duration_cap_ms"], 180000)
        self.assertEqual(policy["queue"]["capacity"], 16)
        media = json.loads(LAB03.read_text(encoding="utf-8"))
        self.assertEqual(media["messages"]["A"]["chunk_bytes"], 1024)
        frozen_path = LAB / "fixtures/frozen-capture.json"
        frozen = json.loads(frozen_path.read_text(encoding="utf-8"))
        manifest = json.loads((LAB / "manifest.lab.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["frozen_capture_sha256"], hashlib.sha256(frozen_path.read_bytes()).hexdigest())
        self.assertLessEqual(frozen["accepted_point_count"], 4096)
        self.assertLessEqual(frozen["duration_cap_ms"], 180000)
        self.assertEqual(frozen["logical_width"], 120)
        self.assertEqual(frozen["logical_height"], 120)
        self.assertEqual(frozen["capture_queue_capacity"], 16)
        payload = self.run_case("hashpayload")
        self.assertEqual(payload.returncode, 0, payload.stderr)
        self.assertEqual(payload.stdout.strip(), frozen["payload_sha256"])

    def test_lab11_scripted_against_fixture_keeps_acceptance_file(self):
        path = LAB / "fixtures/acceptance.json"
        before = path.read_bytes()
        completed = self.run_case("scripted", {"LAB11_PORT": str(self.port)})
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(path.read_bytes(), before)
        self.assertIn("accept-on-archive-commit", json.loads(before)["condition"])
        self.assertNotIn("sent_approved", completed.stdout)
        self.assertIn("scenario=D-one", completed.stdout)
        self.assertIn("chunk_bytes=4096", completed.stdout)
        self.assertIn("provenance=unmeasured/blocked", completed.stdout)
        self.assertIn("full_payload_duplicate_bytes=0", completed.stdout)
