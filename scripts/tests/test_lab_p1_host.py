"""P1 core-journey wire fixture host conformance tests."""
from __future__ import annotations

import importlib.util
import json
import socket
import struct
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import uuid
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB_SERVER = ROOT / "server/labs/lab-p1-core-journey-wire"
FIXTURES = LAB_SERVER / "fixtures"
RUNTIME = LAB_SERVER / "runtime"

_SERVER_SPEC = importlib.util.spec_from_file_location(
    "p1_fixture_server", LAB_SERVER / "server.py")
server = importlib.util.module_from_spec(_SERVER_SPEC)
import sys

sys.modules[_SERVER_SPEC.name] = server
_SERVER_SPEC.loader.exec_module(server)

ACCOUNTS = json.loads((FIXTURES / "accounts.json").read_text(encoding="utf-8"))
DESK = ACCOUNTS["desk_token"]
SKETCH_VECTORS = json.loads((FIXTURES / "sketch-replay.json").read_text(encoding="utf-8"))


def _free_port() -> int:
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


@contextmanager
def _running_server(
    run_id: str,
    reload_tombstones: bool = False,
    log_path: str | None = None,
):
    port = _free_port()
    httpd = server.create_server(
        "127.0.0.1",
        port,
        run_id,
        log_path=log_path,
        reload_tombstones=reload_tombstones,
    )
    thread = threading.Thread(target=httpd.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{port}"
    finally:
        if httpd.log is not None:
            httpd.log.close()
        httpd.shutdown()
        httpd.server_close()
        thread.join(timeout=2)


class _Client:
    def __init__(self, base: str):
        self.base = base

    def request(self, method: str, path: str, body: bytes | None = None,
                bearer: str | None = None, headers: dict | None = None,
                json_body: bool = False) -> tuple[int, bytes, dict]:
        url = self.base + path
        req = urllib.request.Request(url, data=body, method=method)
        if bearer is not None:
            req.add_header("Authorization", f"Bearer {bearer}")
        if body is not None and json_body:
            req.add_header("Content-Type", "application/json")
        if headers:
            for key, value in headers.items():
                req.add_header(key, value)
        try:
            with urllib.request.urlopen(req, timeout=3) as response:
                return response.status, response.read(), dict(response.headers)
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read(), dict(exc.headers)

    def json_request(self, method: str, path: str, payload: dict | None = None,
                     bearer: str | None = None) -> tuple[int, dict]:
        body = None if payload is None else json.dumps(payload).encode("utf-8")
        status, raw, _ = self.request(method, path, body=body, bearer=bearer, json_body=True)
        if not raw:
            return status, {}
        return status, json.loads(raw.decode("utf-8"))


class P1HostTests(unittest.TestCase):
    def setUp(self):
        if RUNTIME.exists():
            for child in RUNTIME.iterdir():
                if child.name != ".gitignore":
                    child.unlink()
        server.STORE.reset_runtime()
        server.STORE.accepted.clear()

    def _login(self, client: _Client) -> str:
        operation_id = str(uuid.uuid4())
        status, body = client.json_request(
            "POST",
            "/v2/session/login",
            {
                "schema": "fl.session-login/2",
                "operation_id": operation_id,
                "user_id": "user-maya",
                "pin": ACCOUNTS["users"][0]["pin"],
            },
            bearer=DESK,
        )
        self.assertEqual(status, 200)
        self.assertEqual(body["outcome"], "authenticated")
        self.assertEqual(body["schema"], "fl.session-login-result/2")
        return body["session"]

    def test_manifest_and_contract_revisions(self):
        manifest = json.loads((LAB_SERVER / "manifest.lab.json").read_text(encoding="utf-8"))
        fixture = json.loads((FIXTURES / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(fixture["contracts"]["login"], "0.13")
        self.assertEqual(fixture["contracts"]["envelope"], "0.10")
        self.assertEqual(fixture["contracts"]["inbox"], "0.9")
        self.assertEqual(fixture["contracts"]["delivery"], "0.12")
        self.assertEqual(fixture["contracts"]["profile"], "0.9")
        self.assertEqual(manifest["fixture_schema"], "p1-core-journey/v1")

    def test_unknown_schema_rejected(self):
        with _running_server("p1-schema") as base:
            client = _Client(base)
            status, body = client.json_request(
                "POST",
                "/v2/endpoint/health",
                {"schema": "fl.endpoint-health/99", "reason": "boot", "boot_id": "b", "muted": False},
                bearer=DESK,
            )
            self.assertEqual(status, 400)
            self.assertEqual(body["code"], "unsupported_schema")

    def test_health_success_and_lost_ack_advances_clock(self):
        with _running_server("p1-health") as base:
            client = _Client(base)
            health_body = {
                "schema": "fl.endpoint-health/2",
                "reason": "periodic",
                "boot_id": "boot-1",
                "muted": False,
            }
            status, body = client.json_request("POST", "/v2/endpoint/health", health_body, bearer=DESK)
            self.assertEqual(status, 200)
            self.assertEqual(body["schema"], "fl.endpoint-health-ack/2")
            self.assertTrue(body["accepted"])
            _, clock = client.json_request("GET", "/internal/health-clock")
            self.assertIsNotNone(clock["last_successful_beat_at"])

            client.json_request("POST", "/v1/control", {"drop_next_health_ack": 1}, bearer=DESK)
            _, before = client.json_request("GET", "/internal/health-clock")
            lost_status, lost_raw, _ = client.request(
                "POST", "/v2/endpoint/health", body=json.dumps(health_body).encode("utf-8"),
                bearer=DESK, json_body=True,
            )
            self.assertEqual(lost_status, 200)
            self.assertEqual(lost_raw, b"")
            _, after = client.json_request("GET", "/internal/health-clock")
            self.assertGreater(after["last_successful_beat_at"], before["last_successful_beat_at"])

    def test_get_hangout_roster_desk_bearer(self):
        roster_fixture = json.loads((FIXTURES / "roster.json").read_text(encoding="utf-8"))
        with _running_server("p1-hangout") as base:
            client = _Client(base)
            status, body = client.json_request("GET", "/v2/hangout", bearer=DESK)
            self.assertEqual(status, 200)
            self.assertEqual(body["schema"], "fl.hangout-roster/2")
            self.assertEqual(body["hangout_id"], roster_fixture["hangout_id"])
            self.assertEqual(body["revision"], roster_fixture["revision"])
            self.assertEqual(body["users"], roster_fixture["users"])

    def test_pin_operation_id_replay_returns_same_session(self):
        operation_id = str(uuid.uuid4())
        login_body = {
            "schema": "fl.session-login/2",
            "operation_id": operation_id,
            "user_id": "user-maya",
            "pin": ACCOUNTS["users"][0]["pin"],
        }
        with _running_server("p1-pin-replay") as base:
            client = _Client(base)
            status1, body1 = client.json_request("POST", "/v2/session/login", login_body, bearer=DESK)
            status2, body2 = client.json_request("POST", "/v2/session/login", login_body, bearer=DESK)
            self.assertEqual(status1, 200)
            self.assertEqual(status2, 200)
            self.assertEqual(body1["outcome"], "authenticated")
            self.assertEqual(body2["outcome"], "authenticated")
            self.assertGreater(len(body1["session"]), 20)
            self.assertEqual(body1["session"], body2["session"])
            other_status, other = client.json_request(
                "POST",
                "/v2/session/login",
                {**login_body, "operation_id": str(uuid.uuid4())},
                bearer=DESK,
            )
            self.assertEqual(other_status, 200)
            self.assertNotEqual(other["session"], body1["session"])

    def test_pin_digits_never_appear_in_server_log(self):
        pin_value = ACCOUNTS["users"][0]["pin"]
        with tempfile.TemporaryDirectory() as directory:
            log_path = Path(directory) / "server.log"
            with _running_server("p1-pin-log", log_path=str(log_path)) as base:
                client = _Client(base)
                session = self._login(client)
                self.assertGreater(len(session), 20)
            log_text = log_path.read_text(encoding="utf-8")
            self.assertNotIn(pin_value, log_text)

    def test_com_011_inbox_recovery_after_oversized_response(self):
        with _running_server("p1-inbox") as base:
            client = _Client(base)
            session = self._login(client)
            revision = json.loads((FIXTURES / "inbox-maya.json").read_text(encoding="utf-8"))["inbox_revision"]
            client.json_request("POST", "/v1/control", {"oversized_inbox_once": 1}, bearer=DESK)
            status, raw, _ = client.request("GET", "/v2/inbox", bearer=session)
            self.assertEqual(status, 200)
            self.assertGreater(len(raw), 262144)
            status2, body2 = client.json_request("GET", "/v2/inbox", bearer=session)
            self.assertEqual(status2, 200)
            self.assertEqual(body2["schema"], "fl.inbox-page/2")
            self.assertEqual(body2["inbox_revision"], revision)
            self.assertLessEqual(len(json.dumps(body2)), 262144)
            status3, body3 = client.json_request("GET", "/v2/inbox", bearer=session)
            self.assertEqual(status3, 200)
            self.assertEqual(body3["schema"], "fl.inbox-page/2")
            self.assertEqual(body3["inbox_revision"], revision)

    def test_send_open_part_empty_ack_and_receipt(self):
        with _running_server("p1-send") as base:
            client = _Client(base)
            session = self._login(client)
            client_id = str(uuid.uuid4())
            create = {
                "schema": "fl.message-create/2",
                "client_message_id": client_id,
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000, "channels": 1, "duration_ms": 1000},
            }
            status, open_body = client.json_request("POST", "/v2/outgoing", create, bearer=session)
            self.assertEqual(status, 200)
            self.assertEqual(open_body["schema"], "fl.send-open/2")
            self.assertEqual(open_body["audio"]["parts_acknowledged"], 0)
            message_id = open_body["message_id"]
            pcm = bytes(16000)
            part_status, part_raw, _ = client.request(
                "PUT",
                f"/v2/outgoing/{message_id}/parts/audio/0",
                body=pcm,
                bearer=session,
                headers={"Content-Length": str(len(pcm)), "Content-Type": "application/octet-stream"},
            )
            self.assertEqual(part_status, 200)
            self.assertEqual(part_raw, b"{}")
            complete = {
                "schema": "fl.message-complete/2",
                "client_message_id": client_id,
                "audio": {
                    "parts": 1,
                    "byte_count": len(pcm),
                    "sha256": __import__("hashlib").sha256(pcm).hexdigest(),
                    "duration_ms": 1000,
                },
            }
            status, receipt = client.json_request(
                "POST", f"/v2/outgoing/{message_id}/complete", complete, bearer=session)
            self.assertEqual(status, 200)
            self.assertEqual(receipt["schema"], "fl.send-receipt/2")
            self.assertEqual(receipt["state"], "accepted")
            lookup_status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=session)
            self.assertEqual(lookup_status, 200)
            self.assertEqual(lookup["schema"], "fl.send-receipt/2")

    def test_accepted_tombstone_survives_restart(self):
        client_id = str(uuid.uuid4())
        with _running_server("p1-tomb-1") as base:
            client = _Client(base)
            session = self._login(client)
            create = {
                "schema": "fl.message-create/2",
                "client_message_id": client_id,
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000, "channels": 1, "duration_ms": 500},
            }
            _, open_body = client.json_request("POST", "/v2/outgoing", create, bearer=session)
            message_id = open_body["message_id"]
            pcm = bytes(16000)
            client.request(
                "PUT", f"/v2/outgoing/{message_id}/parts/audio/0", body=pcm, bearer=session,
                headers={"Content-Length": str(len(pcm)), "Content-Type": "application/octet-stream"},
            )
            complete = {
                "schema": "fl.message-complete/2",
                "client_message_id": client_id,
                "audio": {
                    "parts": 1,
                    "byte_count": len(pcm),
                    "sha256": __import__("hashlib").sha256(pcm).hexdigest(),
                    "duration_ms": 500,
                },
            }
            client.json_request("POST", f"/v2/outgoing/{message_id}/complete", complete, bearer=session)

        with _running_server("p1-tomb-2", reload_tombstones=True) as base:
            client = _Client(base)
            session = self._login(client)
            status, body = client.json_request("GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=session)
            self.assertEqual(status, 200)
            self.assertEqual(body["state"], "accepted")
            recreate_status, recreate = client.json_request("POST", "/v2/outgoing", create, bearer=session)
            self.assertEqual(recreate_status, 200)
            self.assertEqual(recreate["schema"], "fl.send-receipt/2")

    def test_sketch_replay_vectors_com_scn_019(self):
        vectors = {row["id"]: bytes.fromhex(row["hex"]) for row in SKETCH_VECTORS["vectors"]}
        two = server.parse_flsk2(vectors["two_strokes"])
        painted = server.replay_pixels(two)
        self.assertEqual(len(two), 4)
        gap_pair = (two[1][0], two[1][1]), (two[2][0], two[2][1])
        self.assertGreater(server.chebyshev(gap_pair[0], gap_pair[1]), 1)
        mid = ((gap_pair[0][0] + gap_pair[1][0]) // 2, (gap_pair[0][1] + gap_pair[1][1]) // 2)
        self.assertNotIn(mid, painted)

        tap = server.parse_flsk2(vectors["tap"])
        self.assertEqual(len(tap), 1)
        self.assertEqual(len(server.replay_pixels(tap)), 1)

        diagonal = server.parse_flsk2(vectors["diagonal"])
        coords = [(p[0], p[1]) for p in diagonal]
        for left, right in zip(coords, coords[1:]):
            self.assertLessEqual(server.chebyshev(left, right), 1)
        self.assertEqual(len(server.replay_pixels(diagonal)), len(diagonal))

    def test_refresh_empty_body(self):
        with _running_server("p1-refresh") as base:
            client = _Client(base)
            session = self._login(client)
            status, body = client.json_request("POST", "/v2/session/refresh", {}, bearer=session)
            self.assertEqual(status, 200)
            self.assertEqual(body, {})

class P1FixtureGuard(unittest.TestCase):
    def test_fixture_bytes_require_revision_bump(self):
        import shutil
        import tempfile

        directory = tempfile.mkdtemp(prefix="p1-fixture-guard-")
        copied = Path(directory) / "fixtures"
        shutil.copytree(FIXTURES, copied)
        target = copied / "roster.json"
        target.write_bytes(target.read_bytes().replace(b'"revision": 3', b'"revision": 4', 1))
        with self.assertRaises(server.IdentityError):
            server.verify_fixture_tree(copied)
        shutil.rmtree(directory)


if __name__ == "__main__":
    unittest.main()
