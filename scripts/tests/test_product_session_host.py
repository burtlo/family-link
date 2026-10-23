"""L2: P1 fixture — PIN after mute and inbox fence via wire classify."""
from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import unittest
import uuid
from pathlib import Path

from scripts.tests.test_lab_p1_host import FIXTURES, _Client, _running_server

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
    LOGIN / "tests/product_session_model_test.cpp",
]
ACCOUNTS = json.loads((FIXTURES / "accounts.json").read_text(encoding="utf-8"))
DESK = ACCOUNTS["desk_token"]
MAYA = ACCOUNTS["users"][0]
JORDAN = ACCOUNTS["users"][1]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class ProductSessionHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-session-host-")
        cls.binary = Path(cls.temp.name) / "product_session_model_test"
        subprocess.run([
            find_cxx(), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(INCLUDE), *[str(path) for path in SOURCES], "-o", str(cls.binary),
        ], check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _write_json(self, payload: dict) -> Path:
        path = Path(self.temp.name) / f"body-{uuid.uuid4().hex}.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def _login(self, client: _Client, user: dict) -> str:
        operation_id = str(uuid.uuid4())
        status, body = client.json_request(
            "POST",
            "/v2/session/login",
            {
                "schema": "fl.session-login/2",
                "operation_id": operation_id,
                "user_id": user["user_id"],
                "pin": user["pin"],
            },
            bearer=DESK,
        )
        self.assertEqual(status, 200)
        self.assertEqual(body["outcome"], "authenticated")
        return str(body["session"])

    def test_p5_late_pin_after_mute_http(self):
        with _running_server("p5-late-pin-mute") as base:
            client = _Client(base)
            status, roster = client.json_request("GET", "/v2/hangout", bearer=DESK)
            self.assertEqual(status, 200)
            roster_path = self._write_json(roster)
            proc = subprocess.Popen(
                [
                    str(self.binary),
                    "host-p5-late-pin-mute",
                    str(roster_path),
                    MAYA["user_id"],
                    MAYA["pin"],
                ],
                cwd=ROOT,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            operation_line = proc.stdout.readline()
            self.assertIn("operation_id=", operation_line)
            operation_id = operation_line.strip().split("=", 1)[1]
            login_status, login_body = client.json_request(
                "POST",
                "/v2/session/login",
                {
                    "schema": "fl.session-login/2",
                    "operation_id": operation_id,
                    "user_id": MAYA["user_id"],
                    "pin": MAYA["pin"],
                },
                bearer=DESK,
            )
            self.assertEqual(login_status, 200)
            response_path = self._write_json(login_body)
            proc.stdin.write(f"{response_path}\n")
            proc.stdin.flush()
            proc.wait(timeout=10)
            self.assertEqual(proc.returncode, 0, proc.stderr.read())
            out = proc.stdout.read()
            self.assertIn("stage=muted", out)
            self.assertIn("credential_active=0", out)
            self.assertNotIn(MAYA["pin"], out)

    def test_p5_credential_lifecycle_http(self):
        wrong_bearer = "Z" * 43
        with _running_server("p5-credential-lifecycle") as base:
            client = _Client(base)
            status, roster = client.json_request("GET", "/v2/hangout", bearer=DESK)
            self.assertEqual(status, 200)
            roster_path = self._write_json(roster)
            proc = subprocess.Popen(
                [
                    str(self.binary),
                    "host-p5-mute-credential",
                    str(roster_path),
                    MAYA["user_id"],
                    MAYA["pin"],
                ],
                cwd=ROOT,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            operation_line = proc.stdout.readline()
            operation_id = operation_line.strip().split("=", 1)[1]
            login_status, login_body = client.json_request(
                "POST",
                "/v2/session/login",
                {
                    "schema": "fl.session-login/2",
                    "operation_id": operation_id,
                    "user_id": MAYA["user_id"],
                    "pin": MAYA["pin"],
                },
                bearer=DESK,
            )
            self.assertEqual(login_status, 200)
            session_token = str(login_body["session"])
            status, inbox = client.json_request("GET", "/v2/inbox", bearer=session_token)
            self.assertEqual(status, 200)
            self.assertGreater(inbox.get("total_count", 0), 0)
            refresh_status, refresh_body = client.json_request(
                "POST", "/v2/session/refresh", {}, bearer=session_token,
            )
            self.assertEqual(refresh_status, 200)
            self.assertEqual(refresh_body, {})
            status_ext, _ = client.json_request("GET", "/v2/inbox", bearer=session_token)
            self.assertEqual(status_ext, 200)
            status_bad, _ = client.json_request("GET", "/v2/inbox", bearer=wrong_bearer)
            self.assertIn(status_bad, {401, 403})
            proc.stdin.write(f"{self._write_json(login_body)}\n")
            proc.stdin.flush()
            proc.wait(timeout=10)
            self.assertEqual(proc.returncode, 0, proc.stderr.read())
            out = proc.stdout.read()
            self.assertIn("credential_active=0", out)
            self.assertIn("invalidate_active=1", out)
            self.assertNotIn(MAYA["pin"], out)

    def test_p5_inbox_stale_prior_user_http(self):
        with _running_server("p5-inbox-stale-user") as base:
            client = _Client(base)
            status, roster = client.json_request("GET", "/v2/hangout", bearer=DESK)
            self.assertEqual(status, 200)
            roster_path = self._write_json(roster)
            operation_id = str(uuid.uuid4())
            login_status, login_body = client.json_request(
                "POST",
                "/v2/session/login",
                {
                    "schema": "fl.session-login/2",
                    "operation_id": operation_id,
                    "user_id": MAYA["user_id"],
                    "pin": MAYA["pin"],
                },
                bearer=DESK,
            )
            self.assertEqual(login_status, 200)
            login_path = self._write_json(login_body)
            completed = subprocess.run(
                [
                    str(self.binary),
                    "host-p5-inbox-stale",
                    str(roster_path),
                    MAYA["user_id"],
                    MAYA["pin"],
                    str(login_path),
                    JORDAN["user_id"],
                ],
                check=True,
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertIn("stale_terminal=stale", completed.stdout)
            self.assertIn("scoped_applied=0", completed.stdout)

    def test_p5_inbox_fence_after_switch_http(self):
        with _running_server("p5-inbox-fence") as base:
            client = _Client(base)
            status, roster = client.json_request("GET", "/v2/hangout", bearer=DESK)
            self.assertEqual(status, 200)
            roster_path = self._write_json(roster)
            subprocess.run(
                [str(self.binary), "inbox-fence"],
                check=True,
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            maya_token = self._login(client, MAYA)
            jordan_token = self._login(client, JORDAN)
            status_maya, maya_inbox = client.json_request("GET", "/v2/inbox", bearer=maya_token)
            status_jordan, jordan_inbox = client.json_request("GET", "/v2/inbox", bearer=jordan_token)
            self.assertEqual(status_maya, 200)
            self.assertEqual(status_jordan, 200)
            self.assertGreater(maya_inbox.get("total_count", 0), 0)
            self.assertEqual(jordan_inbox.get("total_count", 0), 0)

    def test_p5_inbox_actor_from_bearer(self):
        with _running_server("p5-inbox-actor") as base:
            client = _Client(base)
            maya_token = self._login(client, MAYA)
            status, maya_inbox = client.json_request("GET", "/v2/inbox", bearer=maya_token)
            self.assertEqual(status, 200)
            self.assertGreater(maya_inbox.get("total_count", 0), 0)
            status_bad, _ = client.json_request("GET", "/v2/inbox", bearer="not-a-valid-session-token")
            self.assertIn(status_bad, {401, 403})
            client.json_request(
                "POST",
                "/v2/session/invalidate",
                {
                    "schema": "fl.session-invalidate/2",
                    "operation_id": str(uuid.uuid4()),
                    "session": maya_token,
                },
                bearer=DESK,
            )
            status_revoked, _ = client.json_request("GET", "/v2/inbox", bearer=maya_token)
            self.assertIn(status_revoked, {401, 403})

    def test_p5_l1_regression_bundle(self):
        subprocess.run([str(self.binary), "all"], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
