"""L2 scripted tests: product login HTTP client against the P1 fixture."""
from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import unittest
import uuid
from pathlib import Path

from scripts.tests.test_lab_p1_host import FIXTURES, LAB_SERVER, _Client, _running_server

ROOT = Path(__file__).resolve().parents[2]
LOGIN = ROOT / "client/shared/login_session"
INCLUDE = LOGIN / "fl_product_login/include"
SOURCES = [
    LOGIN / "fl_product_login/session.cpp",
    LOGIN / "fl_product_login/pin_session.cpp",
    LOGIN / "fl_product_login/scoped_session.cpp",
    LOGIN / "fl_product_login/credential_session.cpp",
    LOGIN / "fl_product_login/pollers.cpp",
    LOGIN / "fl_product_login/p1_wire.cpp",
    LOGIN / "fl_product_login/workers.cpp",
    LOGIN / "tests/product_login_model_test.cpp",
]
ACCOUNTS = json.loads((FIXTURES / "accounts.json").read_text(encoding="utf-8"))
DESK = ACCOUNTS["desk_token"]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class ProductLoginHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-login-host-")
        cls.binary = Path(cls.temp.name) / "product_login_model_test"
        subprocess.run([
            find_cxx(), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(INCLUDE), *[str(path) for path in SOURCES], "-o", str(cls.binary),
        ], check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _write_body(self, body: bytes) -> Path:
        body_path = Path(self.__class__.temp.name) / f"body-{id(body)}.json"
        body_path.write_bytes(body)
        return body_path

    def test_p1_roster_success_selectable(self):
        roster_fixture = json.loads((FIXTURES / "roster.json").read_text(encoding="utf-8"))
        with _running_server("p4-roster-ok") as base:
            client = _Client(base)
            status, body = client.json_request("GET", "/v2/hangout", bearer=DESK)
            self.assertEqual(status, 200)
            payload = json.dumps(body).encode("utf-8")
            body_path = self._write_body(payload)
            completed = subprocess.run(
                [str(self.binary), "http-apply-ok", str(body_path)],
                check=True, cwd=ROOT, capture_output=True, text=True,
            )
            out = completed.stdout
            self.assertNotIn(ACCOUNTS["users"][0]["pin"], out)
            self.assertIn("terminal=success", out)
            self.assertIn("selectable_cards=2", out)
            self.assertIn("stage=roster", out)
            self.assertEqual(body["schema"], roster_fixture["schema"])

    def test_p1_malformed_roster_not_selectable(self):
        malformed = (
            b'{"schema":"fl.hangout-roster/2","hangout_id":"h","revision":1,'
            b'"users":[{"user_id":"u1"}]}'
        )
        body_path = self._write_body(malformed)
        completed = subprocess.run(
            [str(self.binary), "http-apply", "1", "200", str(body_path)],
            check=True, cwd=ROOT, capture_output=True, text=True,
        )
        out = completed.stdout
        self.assertIn("terminal=failure", out)
        self.assertIn("selectable_cards=0", out)
        self.assertIn("stage=connecting", out)

    def test_p1_pin_classify_masks_secrets(self):
        pin = ACCOUNTS["users"][0]["pin"]
        body = (
            '{"schema":"fl.session-login-result/2","operation_id":"00000000-0000-4000-8000-000000000001",'
            '"outcome":"wrong_pin","user_id":"user-maya"}'
        ).encode("utf-8")
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as handle:
            handle.write(body.decode("utf-8"))
            path = handle.name
        completed = subprocess.run(
            [str(self.binary), "pin-classify", "1", "200", path, "user-maya"],
            check=True, cwd=ROOT, capture_output=True, text=True,
        )
        self.assertNotIn(pin, completed.stdout)

    def test_p1_pin_submit_operation_id_replay(self):
        operation_id = str(uuid.uuid4())
        login_body = {
            "schema": "fl.session-login/2",
            "operation_id": operation_id,
            "user_id": "user-maya",
            "pin": ACCOUNTS["users"][0]["pin"],
        }
        with _running_server("p4-pin-replay") as base:
            client = _Client(base)
            status1, body1 = client.json_request("POST", "/v2/session/login", login_body, bearer=DESK)
            status2, body2 = client.json_request("POST", "/v2/session/login", login_body, bearer=DESK)
            self.assertEqual(status1, 200)
            self.assertEqual(status2, 200)
            self.assertEqual(body1["outcome"], "authenticated")
            self.assertEqual(body2["outcome"], "authenticated")
            self.assertEqual(body1["session"], body2["session"])
            body_path = self._write_body(json.dumps(body1).encode("utf-8"))
            completed = subprocess.run(
                [str(self.binary), "pin-apply", "user-maya", str(body_path)],
                check=True, cwd=ROOT, capture_output=True, text=True,
            )
            out = completed.stdout
            self.assertNotIn(ACCOUNTS["users"][0]["pin"], out)
            self.assertIn("terminal=success", out)
            self.assertIn("terminal_count=1", out)
            self.assertIn("authenticated=1", out)


if __name__ == "__main__":
    unittest.main()
