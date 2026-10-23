"""Focused HTTP regressions for product server persistence and authorization."""
from __future__ import annotations

import hashlib
import json
import tempfile
import threading
import time
import unittest
import uuid
from pathlib import Path

from scripts.tests.test_lab_p1_host import ACCOUNTS, DESK, _Client, _running_server, server


class ProductServerHardeningTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="product-server-hardening-")
        self.previous = (server.RUNTIME_DIR, server.TOMBSTONE_PATH,
                         server.LAST_VIEWED_PATH, server.STORE)
        server.RUNTIME_DIR = Path(self.temp.name)
        server.TOMBSTONE_PATH = server.RUNTIME_DIR / "accepted-tombstones.json"
        server.LAST_VIEWED_PATH = server.RUNTIME_DIR / "last-viewed.json"
        server.STORE = server.Store([])

    def tearDown(self):
        (server.RUNTIME_DIR, server.TOMBSTONE_PATH,
         server.LAST_VIEWED_PATH, server.STORE) = self.previous
        self.temp.cleanup()

    def _login(self, client: _Client, user_index: int) -> str:
        user = ACCOUNTS["users"][user_index]
        status, body = client.json_request("POST", "/v2/session/login", {
            "schema": "fl.session-login/2",
            "operation_id": str(uuid.uuid4()),
            "user_id": user["user_id"],
            "pin": user["pin"],
        }, bearer=DESK)
        self.assertEqual(status, 200)
        self.assertEqual(body["outcome"], "authenticated")
        return body["session"]

    def _login_with_operation(self, client: _Client, user_index: int):
        user = ACCOUNTS["users"][user_index]
        operation_id = str(uuid.uuid4())
        status, body = client.json_request("POST", "/v2/session/login", {
            "schema": "fl.session-login/2",
            "operation_id": operation_id,
            "user_id": user["user_id"],
            "pin": user["pin"],
        }, bearer=DESK)
        self.assertEqual(status, 200)
        self.assertEqual(body["outcome"], "authenticated")
        return operation_id, body["session"]

    def _open(self, client: _Client, session: str, duration_ms: int = 1000):
        client_id = str(uuid.uuid4())
        status, body = client.json_request("POST", "/v2/outgoing", {
            "schema": "fl.message-create/2",
            "client_message_id": client_id,
            "recipient_user_ids": ["user-jordan"],
            "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                      "channels": 1, "duration_ms": duration_ms},
        }, bearer=session)
        self.assertEqual(status, 200)
        return client_id, body["message_id"]

    def _upload(self, client: _Client, session: str, message_id: str, pcm: bytes):
        for sequence, offset in enumerate(range(0, len(pcm), server.PART_SIZE)):
            status, _, _ = client.request(
                "PUT", f"/v2/outgoing/{message_id}/parts/audio/{sequence}",
                body=pcm[offset:offset + server.PART_SIZE], bearer=session,
            )
            self.assertEqual(status, 200)

    def _complete(self, client: _Client, session: str, client_id: str,
                  message_id: str, pcm: bytes, duration_ms: int = 1000):
        return client.json_request("POST", f"/v2/outgoing/{message_id}/complete", {
            "schema": "fl.message-complete/2",
            "client_message_id": client_id,
            "audio": {"parts": (len(pcm) + server.PART_SIZE - 1) // server.PART_SIZE,
                      "byte_count": len(pcm),
                      "sha256": hashlib.sha256(pcm).hexdigest(),
                      "duration_ms": duration_ms},
        }, bearer=session)

    def test_audio_duration_and_accepted_receipt_authorization(self):
        with _running_server("hardening-audio") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            jordan = self._login(client, 1)
            short_id, short_message = self._open(client, maya)
            self._upload(client, maya, short_message, b"\0\0")
            status, _ = self._complete(client, maya, short_id, short_message, b"\0\0")
            self.assertEqual(status, 413)
            self.assertNotIn(short_id, server.STORE.accepted)

            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            status, receipt = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))
            status, _ = self._complete(client, jordan, client_id, message_id, pcm)
            self.assertEqual(status, 403)

    def test_control_reset_keeps_accepted_delivery(self):
        with _running_server("hardening-reset") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            status, _ = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual(status, 200)
            status, _ = client.json_request("POST", "/v1/control", {"reset_runtime": True})
            self.assertEqual(status, 401)
            status, _ = client.json_request("POST", "/v1/control",
                                            {"reset_runtime": True}, bearer=DESK)
            self.assertEqual(status, 200)
            self.assertIn(client_id, server.STORE.accepted)
            self.assertIn(message_id, server.STORE.media)
            self.assertTrue(any(item["message_id"] == message_id for item in
                                server.STORE.inboxes["user-jordan"]["items"]))

    def test_last_viewed_survives_restart_and_replay_expires(self):
        with _running_server("hardening-viewed") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            jordan = self._login(client, 1)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            status, _ = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual(status, 200)
            operation_id = str(uuid.uuid4())
            request = {"schema": "fl.last-viewed-update/2",
                       "operation_id": operation_id, "message_id": message_id}
            status, _ = client.json_request("PUT", "/v2/inbox/last-viewed",
                                            request, bearer=jordan)
            self.assertEqual(status, 200)

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            self.assertEqual(server.STORE.inboxes["user-jordan"]["last_viewed_message_id"],
                             message_id)
            jordan = self._login(client, 1)
            status, body = client.json_request("GET", "/v2/inbox", bearer=jordan)
            self.assertEqual((status, body["last_viewed_message_id"]), (200, message_id))
            server.STORE.last_viewed_ops[operation_id]["accepted_at"] -= 61
            status, _ = client.json_request("PUT", "/v2/inbox/last-viewed",
                                            request, bearer=jordan)
            self.assertEqual(status, 410)

    def test_invalidate_requires_operation_and_cannot_revoke_other_session(self):
        with _running_server("hardening-invalidate") as base:
            client = _Client(base)
            maya_operation, maya = self._login_with_operation(client, 0)
            jordan_operation, jordan = self._login_with_operation(client, 1)

            status, body = client.json_request(
                "POST", "/v2/session/invalidate", {"schema": "fl.session-invalidate/2"},
                bearer=DESK)
            self.assertEqual(status, 400)
            self.assertEqual(body["code"], "invalid_request")

            status, body = client.json_request(
                "POST", "/v2/session/invalidate", {
                    "schema": "fl.session-invalidate/2",
                    "operation_id": maya_operation,
                    "session": jordan,
                }, bearer=DESK)
            self.assertEqual(status, 409)
            self.assertEqual(body["code"], "identity_conflict")
            self.assertEqual(client.json_request("GET", "/v2/inbox", bearer=jordan)[0], 200)

            status, body = client.json_request(
                "POST", "/v2/session/invalidate", {
                    "schema": "fl.session-invalidate/2",
                    "operation_id": maya_operation,
                    "session": maya,
                }, bearer=DESK)
            self.assertEqual((status, body["revoked"]), (200, True))
            self.assertIn(client.json_request("GET", "/v2/inbox", bearer=maya)[0], (401, 403))
            self.assertEqual(client.json_request("GET", "/v2/inbox", bearer=jordan)[0], 200)

            # Repeating the same invalidate is idempotent and remains scoped.
            status, body = client.json_request(
                "POST", "/v2/session/invalidate", {
                    "schema": "fl.session-invalidate/2",
                    "operation_id": maya_operation,
                }, bearer=DESK)
            self.assertEqual((status, body["revoked"]), (200, True))
            self.assertEqual(client.json_request("GET", "/v2/inbox", bearer=jordan)[0], 200)
            self.assertNotEqual(maya_operation, jordan_operation)

    def test_login_replay_does_not_extend_expired_session(self):
        with _running_server("hardening-login-replay-ttl") as base:
            client = _Client(base)
            operation_id, session = self._login_with_operation(client, 0)
            with server.STORE.lock:
                server.STORE.sessions[session]["last_refresh_at"] = (
                    time.time() - server.SESSION_TTL_S - 1
                )

            status, replay = client.json_request(
                "POST", "/v2/session/login", {
                    "schema": "fl.session-login/2",
                    "operation_id": operation_id,
                    "user_id": ACCOUNTS["users"][0]["user_id"],
                    "pin": ACCOUNTS["users"][0]["pin"],
                }, bearer=DESK)
            self.assertEqual(status, 200)
            self.assertEqual(replay["session"], session)
            status, body = client.json_request("GET", "/v2/inbox", bearer=session)
            self.assertEqual((status, body["code"]), (403, "session_expired"))

    def test_login_replay_preserves_expired_session_after_reload(self):
        with _running_server("hardening-login-replay-reload") as base:
            client = _Client(base)
            operation_id, session = self._login_with_operation(client, 0)
            old = time.time() - server.SESSION_TTL_S - 1
            key = server._pin_record_key(
                server.Handler._endpoint_binding(DESK), operation_id
            )
            with server.STORE.lock:
                server.STORE.pin_records[key].session_issued_at = old
                server.STORE.pin_records[key].session_last_refresh_at = old
                server._save_pin_records(server.STORE.pin_records)
            server.STORE = server.Store([])

            status, replay = client.json_request(
                "POST", "/v2/session/login", {
                    "schema": "fl.session-login/2",
                    "operation_id": operation_id,
                    "user_id": ACCOUNTS["users"][0]["user_id"],
                    "pin": ACCOUNTS["users"][0]["pin"],
                }, bearer=DESK)
            self.assertEqual(status, 200)
            self.assertEqual(replay["session"], session)
            status, body = client.json_request("GET", "/v2/inbox", bearer=session)
            self.assertEqual((status, body["code"]), (403, "session_expired"))

    def test_login_replay_preserves_invalidation(self):
        with _running_server("hardening-login-replay-invalidate") as base:
            client = _Client(base)
            operation_id, session = self._login_with_operation(client, 0)
            invalidate = {
                "schema": "fl.session-invalidate/2",
                "operation_id": operation_id,
                "session": session,
            }
            status, body = client.json_request(
                "POST", "/v2/session/invalidate", invalidate, bearer=DESK
            )
            self.assertEqual((status, body["revoked"]), (200, True))

            # Reconstruct runtime state from the durable login record before
            # replaying the exact login operation.
            server.STORE = server.Store([])
            status, replay = client.json_request(
                "POST", "/v2/session/login", {
                    "schema": "fl.session-login/2",
                    "operation_id": operation_id,
                    "user_id": ACCOUNTS["users"][0]["user_id"],
                    "pin": ACCOUNTS["users"][0]["pin"],
                }, bearer=DESK)
            self.assertEqual(status, 200)
            self.assertEqual(replay["session"], session)
            status, body = client.json_request("GET", "/v2/inbox", bearer=session)
            self.assertEqual((status, body["code"]), (401, "unauthenticated"))

    def test_session_endpoint_binding_is_checked(self):
        with _running_server("hardening-session-binding") as base:
            client = _Client(base)
            _, session = self._login_with_operation(client, 0)
            with server.STORE.lock:
                server.STORE.sessions[session]["endpoint_binding"] = "other-endpoint"
            status, body = client.json_request("GET", "/v2/inbox", bearer=session)
            self.assertEqual(status, 401)
            self.assertEqual(body["code"], "unauthenticated")

    def test_production_server_requires_tls_configuration(self):
        with self.assertRaisesRegex(ValueError, "production mode requires"):
            server.create_server("127.0.0.1", 0, "tls-required", production=True)
        with self.assertRaisesRegex(ValueError, "supplied together"):
            server.create_server("127.0.0.1", 0, "tls-pair", tls_cert="cert.pem")

    def test_failed_receipt_write_does_not_publish(self):
        with _running_server("hardening-storage") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            original = server._save_tombstones
            try:
                def unavailable(_records):
                    raise OSError("simulated storage failure")
                server._save_tombstones = unavailable
                status, _ = self._complete(client, maya, client_id, message_id, pcm)
            finally:
                server._save_tombstones = original
            self.assertEqual(status, 500)
            self.assertNotIn(client_id, server.STORE.accepted)
            self.assertNotIn(message_id, server.STORE.media)
            self.assertNotIn("user-jordan", server.STORE.inboxes)

    def test_parallel_completes_persist_all_receipts(self):
        with _running_server("hardening-parallel") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            sends = []
            for _ in range(3):
                client_id, message_id = self._open(client, maya)
                self._upload(client, maya, message_id, pcm)
                sends.append((client_id, message_id))
            start = threading.Barrier(len(sends))
            results = []

            def complete(item):
                start.wait()
                status, body = self._complete(client, maya, *item, pcm)
                results.append((status, body.get("state")))

            threads = [threading.Thread(target=complete, args=(item,)) for item in sends]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=5)
            self.assertFalse(any(thread.is_alive() for thread in threads))
            self.assertEqual(results, [(200, "accepted")] * len(sends))
            disk = json.loads(server.TOMBSTONE_PATH.read_text(encoding="utf-8"))
            self.assertEqual({row["client_message_id"] for row in disk["records"]},
                             {client_id for client_id, _ in sends})


if __name__ == "__main__":
    unittest.main()
