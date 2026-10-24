"""L2: P10 composed product journey on the P1 core-journey wire fixture."""
from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import unittest
import uuid
from pathlib import Path

from scripts.product_journey.run_l1 import build as build_journey_binary
from scripts.tests.test_lab_p1_host import FIXTURES, _Client, _running_server

ROOT = Path(__file__).resolve().parents[2]
LOGIN = ROOT / "client/shared/login_session"
LOGIN_INCLUDE = LOGIN / "fl_product_login/include"
LOGIN_SOURCES = [
    LOGIN / "fl_product_login/session.cpp",
    LOGIN / "fl_product_login/pin_session.cpp",
    LOGIN / "fl_product_login/scoped_session.cpp",
    LOGIN / "fl_product_login/credential_session.cpp",
    LOGIN / "fl_product_login/pollers.cpp",
    LOGIN / "fl_product_login/p1_wire.cpp",
    LOGIN / "fl_product_login/workers.cpp",
    LOGIN / "tests/product_login_model_test.cpp",
]
INBOX = ROOT / "client/shared/inbox_carousel"
INBOX_INCLUDE = INBOX / "fl_product_inbox/include"
INBOX_SOURCES = [
    INBOX / "fl_product_inbox/carousel.cpp",
    INBOX / "fl_product_inbox/p1_wire.cpp",
    INBOX / "fl_product_inbox/presentation.cpp",
    INBOX / "fl_product_inbox/flsk2.cpp",
    INBOX / "fl_product_inbox/inbox_wire.cpp",
    INBOX / "tests/product_inbox_model_test.cpp",
]
SEND = ROOT / "client/shared/message_delivery"
SEND_INCLUDE = SEND / "fl_product_send/include"
SEND_SOURCES = [
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


class ProductJourneyHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-journey-host-")
        cls.journey_binary = Path(cls.temp.name) / "product_journey_scripted_test"
        build_journey_binary(cls.journey_binary)

        cls.login_binary = Path(cls.temp.name) / "product_login_model_test"
        subprocess.run(
            [
                find_cxx(),
                "-std=c++14",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(LOGIN_INCLUDE),
                *[str(path) for path in LOGIN_SOURCES],
                "-o",
                str(cls.login_binary),
            ],
            check=True,
            cwd=ROOT,
        )

        cls.inbox_binary = Path(cls.temp.name) / "product_inbox_model_test"
        subprocess.run(
            [
                find_cxx(),
                "-std=c++14",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(INBOX_INCLUDE),
                *[str(path) for path in INBOX_SOURCES],
                "-o",
                str(cls.inbox_binary),
            ],
            check=True,
            cwd=ROOT,
        )

        cls.send_binary = Path(cls.temp.name) / "product_send_model_test"
        subprocess.run(
            [
                find_cxx(),
                "-std=c++14",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(SEND_INCLUDE),
                *[str(path) for path in SEND_SOURCES],
                "-o",
                str(cls.send_binary),
            ],
            check=True,
            cwd=ROOT,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _login_http(self, client: _Client) -> str:
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
        self.assertEqual(body["outcome"], "authenticated")
        return str(body["session"])

    def test_p10_l1_overlap_battery(self):
        completed = subprocess.run(
            [str(self.journey_binary), "all"],
            check=True,
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertIn("scenario=boot-volume-mute", completed.stdout)

    def test_p10_l2_roster_pin_on_p1(self):
        roster_fixture = json.loads((FIXTURES / "roster.json").read_text(encoding="utf-8"))
        with _running_server("p10-roster-pin") as base:
            client = _Client(base)
            status, body = client.json_request("GET", "/v2/hangout", bearer=DESK)
            self.assertEqual(status, 200)
            path = Path(self.temp.name) / "roster.json"
            path.write_text(json.dumps(body), encoding="utf-8")
            completed = subprocess.run(
                [str(self.login_binary), "http-apply-ok", str(path)],
                check=True,
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertIn("terminal=success", completed.stdout)
            self.assertEqual(body["schema"], roster_fixture["schema"])
            session = self._login_http(client)
            self.assertTrue(session.startswith("sess-") or len(session) >= 8)

    def test_p10_l2_inbox_play_prefix_on_p1(self):
        with _running_server("p10-inbox-play") as base:
            client = _Client(base)
            session = self._login_http(client)
            status, body = client.json_request("GET", "/v2/inbox", bearer=session)
            self.assertEqual(status, 200)
            inbox_path = Path(self.temp.name) / "inbox.json"
            inbox_path.write_text(json.dumps(body), encoding="utf-8")
            completed = subprocess.run(
                [str(self.inbox_binary), "host-p6-inbox", str(inbox_path)],
                check=True,
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertIn("message_count=", completed.stdout)
            self.assertGreater(int(completed.stdout.split("message_count=")[1].split()[0]), 0)
            message_id = body["items"][0]["message_id"]
            status, raw, headers = client.request(
                "GET",
                f"/v2/messages/{message_id}/media/audio",
                bearer=session,
                headers={"Range": "bytes=0-4095"},
            )
            self.assertEqual(status, 206)
            self.assertTrue(int(headers.get("Content-Length", "0")) > 0)

    def _drive_send(self, client: _Client, session: str, client_id: str, pcm: bytes) -> None:
        proc = subprocess.Popen(
            [str(self.send_binary), "host-p8-drive"],
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
            if line.startswith("PHASE") and " sent" in line:
                break
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

    def test_p10_l2_send_and_receipt_on_p1(self):
        client_id = str(uuid.uuid4())
        pcm = bytes([0x5A]) * 16000
        with _running_server("p10-send") as base:
            client = _Client(base)
            session = self._login_http(client)
            self._drive_send(client, session, client_id, pcm)
            status, body = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=session
            )
            self.assertEqual(status, 200)
            self.assertEqual(body["state"], "accepted")


if __name__ == "__main__":
    unittest.main()
