"""L2: P1 fixture full send path via host-driven outbox (P8)."""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
import uuid
from pathlib import Path

from scripts.tests.test_lab_p1_host import FIXTURES, _Client, _running_server

ROOT = Path(__file__).resolve().parents[2]
SEND = ROOT / "client/shared/message_delivery"
INCLUDE = SEND / "fl_product_send/include"
SOURCES = [
    SEND / "fl_product_send/classify.cpp",
    SEND / "fl_product_send/journey.cpp",
    SEND / "fl_product_send/send_stage.cpp",
    SEND / "fl_product_send/outbox.cpp",
    SEND / "fl_product_send/p1_wire.cpp",
    SEND / "fl_product_send/picker.cpp",
    SEND / "fl_product_send/sha256.cpp",
    SEND / "fl_product_send/tests/product_send_model_test.cpp",
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


class ProductSendHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-send-host-")
        cls.binary = Path(cls.temp.name) / "product_send_model_test"
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

    def _drive_send(self, client: _Client, session: str, client_id: str, pcm: bytes) -> None:
        proc = subprocess.Popen(
            [str(self.binary), "host-p8-drive"],
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=0,
        )
        self.assertEqual(proc.stdout.readline().strip(), "READY")
        proc.stdin.write(f"BIND {client_id} {len(pcm)}\n")
        proc.stdin.flush()
        self.assertEqual(proc.stdout.readline().strip(), "BOUND")
        for _ in range(40):
            proc.stdin.write("PUMP\n")
            proc.stdin.flush()
            line = proc.stdout.readline()
            if not line:
                break
            line = line.strip()
            if line.startswith("IDLE"):
                continue
            if line.startswith("PHASE"):
                if " sent" in line or line.endswith("sent terminal=1"):
                    break
                continue
            if not line.startswith("REQUEST "):
                continue
            _, method, path, length_s = line.split(" ", 3)
            length = int(length_s)
            if method == "PUT" and length > 0:
                body = pcm[:length]
            elif method == "POST" and length > 0:
                body = proc.stdout.readline().encode("latin1")
            else:
                body = None
            status, raw, _ = client.request(
                method,
                path,
                body=body,
                bearer=session,
                headers={"Content-Type": "application/json" if method == "POST" else "application/octet-stream"},
            )
            raw_bytes = raw if isinstance(raw, bytes) else raw.encode("utf-8")
            proc.stdin.write(f"RESULT {status} {len(raw_bytes)} 0\n")
            if raw_bytes:
                proc.stdin.write(raw_bytes.decode("latin1"))
                proc.stdin.write("\n")
            proc.stdin.flush()
            phase_line = proc.stdout.readline().strip()
            if phase_line.startswith("PHASE") and " sent" in phase_line:
                break
        else:
            self.fail("send drive did not reach sent phase")
        proc.stdin.write("QUIT\n")
        proc.stdin.flush()
        proc.wait(timeout=30)

    def test_p8_l2_full_send_on_p1(self):
        client_id = str(uuid.uuid4())
        pcm = bytes([0x5A]) * 16000
        with _running_server("p8-send") as base:
            client = _Client(base)
            session = self._login(client)
            self._drive_send(client, session, client_id, pcm)
            status, body = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=session
            )
            self.assertEqual(status, 200)
            self.assertEqual(body["state"], "accepted")

    def test_p8_l2_tombstone_restart(self):
        client_id = str(uuid.uuid4())
        pcm = bytes([0x5A]) * 16000
        create = {
            "schema": "fl.message-create/2",
            "client_message_id": client_id,
            "recipient_user_ids": ["user-jordan"],
            "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000, "channels": 1, "duration_ms": 500},
        }
        with _running_server("p8-tomb-1") as base:
            client = _Client(base)
            session = self._login(client)
            self._drive_send(client, session, client_id, pcm)
        with _running_server("p8-tomb-2", reload_tombstones=True) as base:
            client = _Client(base)
            session = self._login(client)
            status, body = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=session
            )
            self.assertEqual(status, 200)
            self.assertEqual(body["schema"], "fl.send-receipt/2")
            recreate_status, recreate = client.json_request("POST", "/v2/outgoing", create, bearer=session)
            self.assertEqual(recreate_status, 200)
            self.assertEqual(recreate["schema"], "fl.send-receipt/2")


if __name__ == "__main__":
    unittest.main()
