"""L2: P1 fixture inbox + media for product carousel (P6)."""
from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import uuid
from pathlib import Path

from scripts.tests.test_lab_p1_host import FIXTURES, _Client, _running_server

_P1_SERVER_SPEC = importlib.util.spec_from_file_location(
    "p1_fixture_server_inbox_host",
    Path(__file__).resolve().parents[2] / "server/device/main/server.py",
)
_p1_server = importlib.util.module_from_spec(_P1_SERVER_SPEC)
sys.modules[_P1_SERVER_SPEC.name] = _p1_server
_P1_SERVER_SPEC.loader.exec_module(_p1_server)

ROOT = Path(__file__).resolve().parents[2]
INBOX = ROOT / "client/shared/inbox_carousel"
INCLUDE = INBOX / "fl_product_inbox/include"
SOURCES = [
    INBOX / "fl_product_inbox/carousel.cpp",
    INBOX / "fl_product_inbox/p1_wire.cpp",
    INBOX / "fl_product_inbox/presentation.cpp",
    INBOX / "fl_product_inbox/flsk2.cpp",
    INBOX / "tests/product_inbox_model_test.cpp",
]
ACCOUNTS = json.loads((FIXTURES / "accounts.json").read_text(encoding="utf-8"))
DESK = ACCOUNTS["desk_token"]
MAYA = ACCOUNTS["users"][0]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class ProductInboxHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-inbox-host-")
        cls.binary = Path(cls.temp.name) / "product_inbox_model_test"
        subprocess.run(
            [
                find_cxx(),
                "-std=c++14",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(INCLUDE),
                *[str(path) for path in SOURCES],
                "-o",
                str(cls.binary),
            ],
            check=True,
            cwd=ROOT,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _login(self, client: _Client) -> str:
        status, body = client.json_request(
            "POST",
            "/v2/session/login",
            {
                "schema": "fl.session-login/2",
                "operation_id": str(uuid.uuid4()),
                "user_id": MAYA["user_id"],
                "pin": MAYA["pin"],
            },
            bearer=DESK,
        )
        self.assertEqual(status, 200)
        return str(body["session"])

    def test_p6_p1_inbox_snapshot_apply(self):
        inbox_path = FIXTURES / "inbox-maya.json"
        with _running_server("p6-inbox") as base:
            client = _Client(base)
            session = self._login(client)
            status, body = client.json_request("GET", "/v2/inbox", bearer=session)
            self.assertEqual(status, 200)
            path = Path(self.temp.name) / "inbox.json"
            path.write_text(json.dumps(body), encoding="utf-8")
            completed = subprocess.run(
                [str(self.binary), "host-p6-inbox", str(path)],
                check=True,
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertIn("message_count=1", completed.stdout)
            self.assertIn("msg-p1-demo-001", completed.stdout)

    def test_p6_p1_media_prefix_bytes(self):
        manifest = json.loads((FIXTURES / "manifest.json").read_text(encoding="utf-8"))
        audio_bytes = manifest["files"]["media-audio.pcm"]["bytes"]
        self.assertEqual(audio_bytes, 32000)
        sketch_bytes = manifest["files"]["media-sketch.flsk2"]["bytes"]
        self.assertEqual(sketch_bytes, 32)
        expected_audio = (FIXTURES / "media-audio.pcm").read_bytes()
        expected_sketch = (FIXTURES / "media-sketch.flsk2").read_bytes()
        with _running_server("p6-media") as base:
            client = _Client(base)
            session = self._login(client)
            message_id = json.loads((FIXTURES / "inbox-maya.json").read_text(encoding="utf-8"))[
                "items"
            ][0]["message_id"]
            audio_path = f"/v2/messages/{message_id}/media/audio"
            sketch_path = f"/v2/messages/{message_id}/media/sketch"
            status, raw, headers = client.request("GET", audio_path, bearer=session)
            self.assertEqual(status, 200)
            self.assertEqual(raw, expected_audio)
            accept_ranges = headers.get("Accept-Ranges") or headers.get("Accept-ranges")
            self.assertEqual(accept_ranges, "bytes")
            for size in (4096, 8192, 16384, 32000):
                end = size - 1
                status, chunk, hdrs = client.request(
                    "GET",
                    audio_path,
                    bearer=session,
                    headers={"Range": f"bytes=0-{end}"},
                )
                self.assertEqual(status, 206)
                self.assertEqual(chunk, expected_audio[:size])
                self.assertIn("Content-Range", hdrs)
            status, sketch_full, _ = client.request("GET", sketch_path, bearer=session)
            self.assertEqual(status, 200)
            self.assertEqual(sketch_full, expected_sketch)
            for size in (8, 16, 32):
                end = size - 1
                status, chunk, _ = client.request(
                    "GET",
                    sketch_path,
                    bearer=session,
                    headers={"Range": f"bytes=0-{end}"},
                )
                self.assertEqual(status, 206)
                self.assertEqual(chunk, expected_sketch[:size])
            _p1_server.parse_flsk2(expected_sketch)


if __name__ == "__main__":
    unittest.main()
