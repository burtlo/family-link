"""Focused HTTP regressions for product server persistence and authorization."""
from __future__ import annotations

import hashlib
import io
import json
import socket
import tempfile
import threading
import time
import unittest
import urllib.error
import uuid
from unittest import mock
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

    def test_windows_host_skips_unsupported_directory_fsync(self):
        """Desk PIN persist must not treat Windows directory fsync as failure."""
        with mock.patch.object(server.os, "name", "nt"):
            with mock.patch.object(
                server.os, "open", side_effect=AssertionError("directory fsync is unsupported on Windows"),
            ):
                server._fsync_directory(Path(self.temp.name))

        with mock.patch.object(server.os, "name", "nt"):
            with _running_server("hardening-windows-pin-persist") as base:
                client = _Client(base)
                session = self._login(client, 0)
                self.assertTrue(session)
                self.assertTrue(server._pin_operations_path().is_file())

    def test_wrong_pin_retries_and_new_operation_can_authenticate(self):
        """A rejected PIN must not poison later independent login operations."""
        user = ACCOUNTS["users"][0]
        with _running_server("hardening-wrong-pin-retry") as base:
            client = _Client(base)
            for _ in range(5):
                operation_id = str(uuid.uuid4())
                status, result = client.json_request("POST", "/v2/session/login", {
                    "schema": "fl.session-login/2",
                    "operation_id": operation_id,
                    "user_id": user["user_id"],
                    "pin": "9999",
                }, bearer=DESK)
                self.assertEqual(status, 200)
                self.assertEqual(result["schema"], "fl.session-login-result/2")
                self.assertEqual(result["operation_id"], operation_id)
                self.assertEqual(result["outcome"], "wrong_pin")
                self.assertNotIn("session", result)

            # Wrong PINs do not create or revoke a user session server-side.
            self.assertEqual(server.STORE.sessions, {})

            # Reusing a rejected operation key with a different PIN is an
            # identity conflict by contract; a new submission must use a new
            # operation key.
            status, conflict = client.json_request("POST", "/v2/session/login", {
                "schema": "fl.session-login/2",
                "operation_id": operation_id,
                "user_id": user["user_id"],
                "pin": user["pin"],
            }, bearer=DESK)
            self.assertEqual(status, 409)
            self.assertEqual(conflict["code"], "identity_conflict")

            status, authenticated = client.json_request("POST", "/v2/session/login", {
                "schema": "fl.session-login/2",
                "operation_id": str(uuid.uuid4()),
                "user_id": user["user_id"],
                "pin": user["pin"],
            }, bearer=DESK)
            self.assertEqual(status, 200)
            self.assertEqual(authenticated["outcome"], "authenticated")
            self.assertEqual(authenticated["user_id"], user["user_id"])
            self.assertIn("session", authenticated)

            status, inbox = client.json_request("GET", "/v2/inbox", bearer=authenticated["session"])
            self.assertEqual(status, 200)
            self.assertEqual(inbox["schema"], "fl.inbox-page/2")

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
            self.assertFalse(server._accepted_media_path(message_id, "audio").exists())
            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            self.assertNotIn(client_id, server.STORE.accepted)
            self.assertFalse(server._accepted_media_path(message_id, "audio").exists())

    def test_post_replace_receipt_failure_reloads_complete_delivery(self):
        with _running_server("hardening-post-replace") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)

            original = server._save_tombstones
            try:
                def fail_after_replace(records):
                    original(records)
                    raise OSError("simulated post-replace failure")
                server._save_tombstones = fail_after_replace
                status, _ = self._complete(client, maya, client_id, message_id, pcm)
            finally:
                server._save_tombstones = original

            self.assertEqual(status, 500)
            self.assertNotIn(client_id, server.STORE.accepted)
            self.assertNotIn(message_id, server.STORE.media)
            self.assertNotIn("user-jordan", server.STORE.inboxes)
            self.assertEqual(server._accepted_media_path(message_id, "audio").read_bytes(), pcm)

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            self.assertIn(client_id, server.STORE.accepted)
            self.assertIn(message_id, server.STORE.media)
            self.assertEqual(
                sum(item["message_id"] == message_id
                    for item in server.STORE.inboxes["user-jordan"]["items"]),
                1,
            )
            resumed = self._login(client, 0)
            status, receipt = self._complete(client, resumed, client_id, message_id, pcm)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))

    def test_atomic_copy_failure_before_replace_is_reconstructible_after_reload(self):
        with _running_server("hardening-copy-before-replace") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            audio_path = server._accepted_media_path(message_id, "audio")
            original_replace = server.os.replace

            def fail_before_replace(source, destination):
                if destination == audio_path:
                    raise OSError("simulated pre-replace media failure")
                return original_replace(source, destination)

            with mock.patch.object(server.os, "replace", side_effect=fail_before_replace):
                status, _ = self._complete(client, maya, client_id, message_id, pcm)

            self.assertEqual(status, 500)
            self.assertFalse(audio_path.exists())
            self.assertFalse(server.TOMBSTONE_PATH.exists())
            self.assertFalse(list(server.RUNTIME_DIR.glob("*.tmp")))

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            self.assertNotIn(client_id, server.STORE.accepted)
            self.assertIn(client_id, server.STORE.opens)
            self.assertNotIn("user-jordan", server.STORE.inboxes)
            self.assertFalse(audio_path.exists())

            resumed = self._login(client, 0)
            status, receipt = self._complete(client, resumed, client_id, message_id, pcm)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))
            self.assertEqual(
                sum(item["message_id"] == message_id
                    for item in server.STORE.inboxes["user-jordan"]["items"]),
                1,
            )

    def test_atomic_copy_failure_after_replace_is_reconstructible_after_reload(self):
        with _running_server("hardening-copy-after-replace") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            audio_path = server._accepted_media_path(message_id, "audio")

            def fail_after_replace(_directory):
                raise OSError("simulated post-replace directory sync failure")

            with mock.patch.object(server, "_fsync_directory", side_effect=fail_after_replace):
                status, _ = self._complete(client, maya, client_id, message_id, pcm)

            self.assertEqual(status, 500)
            self.assertFalse(audio_path.exists())
            self.assertFalse(server.TOMBSTONE_PATH.exists())
            self.assertFalse(list(server.RUNTIME_DIR.glob("*.tmp")))

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            self.assertNotIn(client_id, server.STORE.accepted)
            self.assertIn(client_id, server.STORE.opens)
            self.assertNotIn("user-jordan", server.STORE.inboxes)
            self.assertFalse(audio_path.exists())

            resumed = self._login(client, 0)
            status, receipt = self._complete(client, resumed, client_id, message_id, pcm)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))
            self.assertEqual(
                sum(item["message_id"] == message_id
                    for item in server.STORE.inboxes["user-jordan"]["items"]),
                1,
            )

    def test_malformed_persisted_ids_are_ignored_and_runtime_paths_are_contained(self):
        client_id = str(uuid.uuid4())
        server.TOMBSTONE_PATH.write_text(json.dumps({
            "schema": "p1-accepted-tombstones/v1",
            "records": [{
                "client_message_id": client_id,
                "message_id": "../../outside",
                "sender_user_id": "user-maya",
                "accepted_at": "2026-10-23T00:00:00Z",
                "recipient_user_ids": ["user-jordan"],
                "delivery_item": None,
            }],
        }), encoding="utf-8")

        reloaded = server.Store(server._load_tombstones())
        self.assertNotIn(client_id, reloaded.accepted)
        with self.assertRaises(ValueError):
            server._accepted_media_path("../../outside", "audio")
        with self.assertRaises(ValueError):
            server._open_metadata_path("../../outside")
        safe_path = server._accepted_media_path("msg-safe", "audio")
        self.assertEqual(safe_path.resolve().parent, server.RUNTIME_DIR.resolve())

    def test_open_upload_and_per_kind_acknowledgements_survive_store_reload(self):
        with _running_server("hardening-open-reload") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            client_id = str(uuid.uuid4())
            create = {
                "schema": "fl.message-create/2",
                "client_message_id": client_id,
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 1000},
                "sketch": {"format_id": "flsk2", "present": True},
            }
            status, opened = client.json_request("POST", "/v2/outgoing", create, bearer=maya)
            self.assertEqual(status, 200)
            message_id = opened["message_id"]
            pcm = bytes(32000)
            sketch = b"\x01\x02\x00\x00\x00\x00\x00\x00"
            client.request("PUT", f"/v2/outgoing/{message_id}/parts/audio/0",
                           body=pcm[:server.PART_SIZE], bearer=maya)
            client.request("PUT", f"/v2/outgoing/{message_id}/parts/sketch/0",
                           body=sketch, bearer=maya)

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            resumed = self._login(client, 0)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=resumed)
            self.assertEqual(status, 200)
            self.assertEqual(lookup["message_id"], message_id)
            self.assertEqual(lookup["audio"]["parts_acknowledged"], 1)
            self.assertEqual(lookup["sketch"]["parts_acknowledged"], 1)

            client.request("PUT", f"/v2/outgoing/{message_id}/parts/audio/1",
                           body=pcm[server.PART_SIZE:], bearer=resumed)
            complete = {
                "schema": "fl.message-complete/2",
                "client_message_id": client_id,
                "audio": {"parts": 2, "byte_count": len(pcm),
                           "sha256": hashlib.sha256(pcm).hexdigest(), "duration_ms": 1000},
                "sketch": {"parts": 1, "byte_count": len(sketch),
                           "sha256": hashlib.sha256(sketch).hexdigest()},
            }
            status, receipt = client.json_request(
                "POST", f"/v2/outgoing/{message_id}/complete", complete, bearer=resumed)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))

    def test_accepted_receipt_inbox_and_media_survive_store_reload_without_base64(self):
        with _running_server("hardening-accepted-reload") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            jordan = self._login(client, 1)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            status, receipt = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))
            disk = json.loads(server.TOMBSTONE_PATH.read_text(encoding="utf-8"))
            row = next(item for item in disk["records"] if item["client_message_id"] == client_id)
            self.assertNotIn("audio_b64", row)
            media_path = server._accepted_media_path(message_id, "audio")
            self.assertEqual(media_path.read_bytes(), pcm)

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            resumed_maya = self._login(client, 0)
            resumed_jordan = self._login(client, 1)
            status, replay = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=resumed_maya)
            self.assertEqual((status, replay["state"]), (200, "accepted"))
            create = {
                "schema": "fl.message-create/2",
                "client_message_id": client_id,
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 1000},
            }
            status, recreate = client.json_request("POST", "/v2/outgoing", create,
                                                   bearer=resumed_maya)
            self.assertEqual((status, recreate["state"]), (200, "accepted"))
            status, conflict = client.json_request(
                "POST", "/v2/outgoing",
                {**create, "recipient_user_ids": ["user-sam"]}, bearer=resumed_maya)
            self.assertEqual((status, conflict["code"]), (409, "identity_conflict"))
            status, inbox = client.json_request("GET", "/v2/inbox", bearer=resumed_jordan)
            self.assertEqual(status, 200)
            self.assertEqual(sum(item["message_id"] == message_id for item in inbox["items"]), 1)
            status, raw, _ = client.request(
                "GET", f"/v2/messages/{message_id}/media/audio", bearer=resumed_jordan)
            self.assertEqual((status, raw), (200, pcm))

    def test_open_upload_expires_from_last_acknowledged_part(self):
        with _running_server("hardening-open-expiry") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            client_id, message_id = self._open(client, maya)
            part = bytes(server.PART_SIZE)
            self.assertEqual(
                client.request("PUT", f"/v2/outgoing/{message_id}/parts/audio/0",
                               body=part, bearer=maya)[0], 200)
            with server.STORE.lock:
                upload = server.STORE.opens[client_id]
                upload.last_acknowledged_at = time.time() - server.OPEN_UPLOAD_TTL_S - 1
                server._save_open_upload(upload)
            status, body = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya)
            self.assertEqual((status, body["code"]), (404, "not_found"))
            self.assertFalse(server._open_metadata_path(client_id).exists())
            self.assertFalse(server._open_part_path(client_id, "audio", 0).exists())

    def test_part_retry_conflict_and_acknowledgement_survive_reload(self):
        with _running_server("hardening-part-replay") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            client_id, message_id = self._open(client, maya)
            part = bytes(server.PART_SIZE)
            path = f"/v2/outgoing/{message_id}/parts/audio/0"
            self.assertEqual(client.request("PUT", path, body=part, bearer=maya)[0], 200)
            self.assertEqual(client.request("PUT", path, body=part, bearer=maya)[0], 200)
            self.assertEqual(client.request("PUT", path, body=b"x" * server.PART_SIZE,
                                            bearer=maya)[0], 409)

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            resumed = self._login(client, 0)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=resumed)
            self.assertEqual((status, lookup["audio"]["parts_acknowledged"]), (200, 1))
            self.assertEqual(client.request("PUT", path, body=b"x" * server.PART_SIZE,
                                             bearer=resumed)[0], 409)

    def test_part_storage_failure_does_not_ack_or_publish_part(self):
        with _running_server("hardening-part-storage") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            client_id, message_id = self._open(client, maya)
            original = server._save_open_upload
            try:
                server._save_open_upload = lambda _upload: (_ for _ in ()).throw(
                    OSError("simulated storage failure"))
                status, _, _ = client.request(
                    "PUT", f"/v2/outgoing/{message_id}/parts/audio/0",
                    body=bytes(server.PART_SIZE), bearer=maya)
            finally:
                server._save_open_upload = original
            self.assertEqual(status, 500)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya)
            self.assertEqual((status, lookup["audio"]["parts_acknowledged"]), (200, 0))
            self.assertFalse(server._open_part_path(client_id, "audio", 0).exists())

    def test_part_post_replace_failure_restores_metadata_and_reload_state(self):
        with _running_server("hardening-part-post-replace") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            client_id, message_id = self._open(client, maya)
            metadata_path = server._open_metadata_path(client_id)
            part_path = server._open_part_path(client_id, "audio", 0)
            prior_metadata = metadata_path.read_bytes()
            prior_acknowledged_at = server.STORE.opens[client_id].last_acknowledged_at
            original = server._save_open_upload
            try:
                def fail_after_replace(upload):
                    original(upload)
                    raise OSError("simulated post-replace storage failure")
                server._save_open_upload = fail_after_replace
                status, _, _ = client.request(
                    "PUT", f"/v2/outgoing/{message_id}/parts/audio/0",
                    body=bytes(server.PART_SIZE), bearer=maya)
            finally:
                server._save_open_upload = original

            self.assertEqual(status, 500)
            self.assertEqual(metadata_path.read_bytes(), prior_metadata)
            self.assertFalse(part_path.exists())
            self.assertEqual(
                server.STORE.opens[client_id].last_acknowledged_at,
                prior_acknowledged_at,
            )

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            reloaded = server.STORE.opens[client_id]
            self.assertEqual(server._open_metadata_path(client_id).read_bytes(), prior_metadata)
            self.assertFalse(server._open_part_path(client_id, "audio", 0).exists())
            self.assertEqual(reloaded.last_acknowledged_at, prior_acknowledged_at)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya)
            self.assertEqual((status, lookup["audio"]["parts_acknowledged"]), (200, 0))

    def test_process_style_restart_resumes_open_and_accepted_send(self):
        pcm = bytes(32000)
        with _running_server("hardening-process-open") as base:
            client = _Client(base)
            _, maya = self._login_with_operation(client, 0)
            client_id = str(uuid.uuid4())
            create = {
                "schema": "fl.message-create/2",
                "client_message_id": client_id,
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 1000},
            }
            status, opened = client.json_request("POST", "/v2/outgoing", create, bearer=maya)
            self.assertEqual(status, 200)
            message_id = opened["message_id"]
            self.assertEqual(
                client.request("PUT", f"/v2/outgoing/{message_id}/parts/audio/0",
                               body=pcm[:server.PART_SIZE], bearer=maya)[0],
                200,
            )

        # Rebuild the in-memory store as a new process would, while retaining
        # the isolated durable runtime directory.
        server.STORE = server.Store(server._load_tombstones())
        server._restore_persisted_deliveries()
        with _running_server("hardening-process-open-reload", reload_tombstones=True) as base:
            client = _Client(base)
            resumed = self._login(client, 0)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=resumed
            )
            self.assertEqual((status, lookup["state"]), (200, "open"))
            self.assertEqual(lookup["message_id"], message_id)
            self.assertEqual(lookup["audio"]["parts_acknowledged"], 1)
            self.assertEqual(
                client.request("PUT", f"/v2/outgoing/{message_id}/parts/audio/1",
                               body=pcm[server.PART_SIZE:], bearer=resumed)[0],
                200,
            )
            status, receipt = self._complete(
                client, resumed, client_id, message_id, pcm,
            )
            self.assertEqual((status, receipt["state"]), (200, "accepted"))
            self.assertEqual(receipt["message_id"], message_id)

        server.STORE = server.Store(server._load_tombstones())
        server._restore_persisted_deliveries()
        with _running_server("hardening-process-accepted-reload", reload_tombstones=True) as base:
            client = _Client(base)
            resumed = self._login(client, 0)
            status, replay = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=resumed
            )
            self.assertEqual((status, replay["state"]), (200, "accepted"))
            self.assertEqual(replay["message_id"], message_id)
            status, jordan = client.json_request(
                "POST", "/v2/session/login", {
                    "schema": "fl.session-login/2",
                    "operation_id": str(uuid.uuid4()),
                    "user_id": "user-jordan",
                    "pin": ACCOUNTS["users"][1]["pin"],
                }, bearer=DESK,
            )
            self.assertEqual(status, 200)
            status, inbox = client.json_request("GET", "/v2/inbox", bearer=jordan["session"])
            self.assertEqual(status, 200)
            self.assertEqual(sum(item["message_id"] == message_id for item in inbox["items"]), 1)

    def test_lost_response_replays_create_part_complete_lookup_refresh_and_invalidate(self):
        with _running_server("hardening-replay-all") as base:
            client = _Client(base)
            operation_id, maya = self._login_with_operation(client, 0)
            client_id = str(uuid.uuid4())
            create = {
                "schema": "fl.message-create/2",
                "client_message_id": client_id,
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 1000},
            }
            status, first_open = client.json_request("POST", "/v2/outgoing", create, bearer=maya)
            status2, replay_open = client.json_request("POST", "/v2/outgoing", create, bearer=maya)
            self.assertEqual((status, status2), (200, 200))
            self.assertEqual(first_open["message_id"], replay_open["message_id"])
            message_id = first_open["message_id"]
            pcm = bytes(32000)
            part_path = f"/v2/outgoing/{message_id}/parts/audio/0"
            part = pcm[:server.PART_SIZE]
            self.assertEqual(client.request("PUT", part_path, body=part, bearer=maya)[0], 200)
            self.assertEqual(client.request("PUT", part_path, body=part, bearer=maya)[0], 200)
            self.assertEqual(
                client.request("PUT", part_path, body=b"x" * server.PART_SIZE, bearer=maya)[0],
                409,
            )
            self.assertEqual(
                client.request("PUT", f"/v2/outgoing/{message_id}/parts/audio/1",
                               body=pcm[server.PART_SIZE:], bearer=maya)[0],
                200,
            )
            complete_path = f"/v2/outgoing/{message_id}/complete"
            complete = {
                "schema": "fl.message-complete/2",
                "client_message_id": client_id,
                "audio": {
                    "parts": 2,
                    "byte_count": len(pcm),
                    "sha256": hashlib.sha256(pcm).hexdigest(),
                    "duration_ms": 1000,
                },
            }
            original_send_raw = server.Handler._send_raw
            dropped = {"count": 0}

            def drop_first_committed_response(handler, status, body,
                                              content_type="application/json",
                                              extra_headers=None):
                if handler.path == complete_path and status == 200 and dropped["count"] == 0:
                    dropped["count"] += 1
                    handler.close_connection = True
                    try:
                        handler.connection.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    finally:
                        handler.wfile.close()
                        handler.wfile = io.BytesIO()
                        handler.connection.close()
                    return
                return original_send_raw(
                    handler, status, body, content_type, extra_headers,
                )

            with mock.patch.object(server.Handler, "_send_raw",
                                   new=drop_first_committed_response):
                # The server commits before the response is deliberately lost.
                # A normally consumed response must fail this assertion.
                with self.assertRaises(OSError):
                    client.json_request("POST", complete_path, complete, bearer=maya)

            self.assertEqual(dropped["count"], 1)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya,
            )
            self.assertEqual((status, lookup["state"]), (200, "accepted"))
            self.assertEqual(lookup["client_message_id"], client_id)
            self.assertEqual(lookup["message_id"], message_id)

            status2, replay_receipt = client.json_request(
                "POST", complete_path, complete, bearer=maya,
            )
            self.assertEqual((status2, replay_receipt["state"]), (200, "accepted"))
            self.assertEqual(replay_receipt["message_id"], message_id)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya
            )
            status2, lookup_replay = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya
            )
            self.assertEqual((status, status2), (200, 200))
            self.assertEqual(lookup, lookup_replay)
            self.assertEqual(client.json_request("POST", "/v2/session/refresh", {}, bearer=maya),
                             (200, {}))
            self.assertEqual(client.json_request("POST", "/v2/session/refresh", {}, bearer=maya),
                             (200, {}))
            invalidate = {
                "schema": "fl.session-invalidate/2",
                "operation_id": operation_id,
                "session": maya,
            }
            self.assertEqual(client.json_request("POST", "/v2/session/invalidate", invalidate,
                                                 bearer=DESK), (200, {
                                                     "schema": "fl.session-invalidate-result/2",
                                                     "operation_id": operation_id,
                                                     "revoked": True,
                                                 }))
            self.assertEqual(
                sum(item["message_id"] == message_id
                    for item in server.STORE.inboxes["user-jordan"]["items"]),
                1,
            )
            self.assertEqual(
                server._accepted_media_path(message_id, "audio").read_bytes(), pcm,
            )
            self.assertEqual(
                len(list(server.RUNTIME_DIR.glob(f"accepted-media-{message_id}.*.bin"))),
                1,
            )
            self.assertEqual(list(server.STORE.media), [message_id])
            self.assertEqual(client.json_request("POST", "/v2/session/invalidate", invalidate,
                                                 bearer=DESK), (200, {
                                                     "schema": "fl.session-invalidate-result/2",
                                                     "operation_id": operation_id,
                                                     "revoked": True,
                                                 }))
            self.assertEqual(
                sum(item["message_id"] == message_id
                    for item in server.STORE.inboxes["user-jordan"]["items"]),
                1,
            )

    def test_complete_retry_after_committed_response_is_dropped(self):
        with _running_server("hardening-transport-drop") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            complete_path = f"/v2/outgoing/{message_id}/complete"
            complete = {
                "schema": "fl.message-complete/2",
                "client_message_id": client_id,
                "audio": {
                    "parts": 2,
                    "byte_count": len(pcm),
                    "sha256": hashlib.sha256(pcm).hexdigest(),
                    "duration_ms": 1000,
                },
            }
            original_send_raw = server.Handler._send_raw
            dropped = {"count": 0}

            def drop_first_committed_response(handler, status, body,
                                              content_type="application/json",
                                              extra_headers=None):
                if handler.path == complete_path and status == 200 and dropped["count"] == 0:
                    dropped["count"] += 1
                    handler.close_connection = True
                    try:
                        handler.connection.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    finally:
                        handler.wfile.close()
                        handler.wfile = io.BytesIO()
                        handler.connection.close()
                    return
                return original_send_raw(
                    handler, status, body, content_type, extra_headers,
                )

            with mock.patch.object(server.Handler, "_send_raw",
                                   new=drop_first_committed_response):
                with self.assertRaises(OSError):
                    client.json_request("POST", complete_path, complete, bearer=maya)

            self.assertEqual(dropped["count"], 1)
            self.assertIn(client_id, server.STORE.accepted)
            status, receipt = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya,
            )
            self.assertEqual((status, receipt["state"]), (200, "accepted"))
            self.assertEqual(receipt["message_id"], message_id)
            self.assertEqual(
                sum(item["message_id"] == message_id
                    for item in server.STORE.inboxes["user-jordan"]["items"]),
                1,
            )
            self.assertEqual(
                server._accepted_media_path(message_id, "audio").read_bytes(), pcm,
            )
            self.assertEqual(
                len(list(server.RUNTIME_DIR.glob(f"accepted-media-{message_id}.*.bin"))),
                1,
            )

            server.STORE = server.Store(server._load_tombstones())
            server._restore_persisted_deliveries()
            self.assertIn(client_id, server.STORE.accepted)
            self.assertEqual(
                sum(item["message_id"] == message_id
                    for item in server.STORE.inboxes["user-jordan"]["items"]),
                1,
            )

    def test_concurrent_duplicate_complete_is_idempotent_and_conflict_is_rejected(self):
        with _running_server("hardening-duplicate-complete") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            start = threading.Barrier(2)
            results = []

            def complete_once():
                start.wait()
                results.append(self._complete(client, maya, client_id, message_id, pcm))

            threads = [threading.Thread(target=complete_once) for _ in range(2)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=5)
            self.assertFalse(any(thread.is_alive() for thread in threads))
            self.assertEqual([status for status, _ in results], [200, 200])
            self.assertEqual({body["message_id"] for _, body in results}, {message_id})
            self.assertEqual(
                sum(item["message_id"] == message_id
                    for item in server.STORE.inboxes["user-jordan"]["items"]),
                1,
            )
            conflicting = {
                "schema": "fl.message-complete/2",
                "client_message_id": client_id,
                "audio": {
                    "parts": 2,
                    "byte_count": len(pcm),
                    "sha256": hashlib.sha256(pcm).hexdigest(),
                    "duration_ms": 999,
                },
            }
            status, body = client.json_request(
                "POST", f"/v2/outgoing/{message_id}/complete", conflicting, bearer=maya
            )
            self.assertEqual((status, body["code"]), (409, "identity_conflict"))

    def test_out_of_order_parts_keep_acknowledgement_contiguous(self):
        with _running_server("hardening-contiguous-ack") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(48000)
            client_id, message_id = self._open(client, maya, duration_ms=1500)
            part0 = pcm[:server.PART_SIZE]
            part1 = pcm[server.PART_SIZE:2 * server.PART_SIZE]
            part2 = pcm[2 * server.PART_SIZE:]
            path = lambda sequence: f"/v2/outgoing/{message_id}/parts/audio/{sequence}"
            self.assertEqual(client.request("PUT", path(1), body=part1, bearer=maya)[0], 200)
            self.assertEqual(client.request("PUT", path(2), body=part2, bearer=maya)[0], 200)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya
            )
            self.assertEqual((status, lookup["audio"]["parts_acknowledged"]), (200, 0))
            self.assertEqual(client.request("PUT", path(0), body=part0, bearer=maya)[0], 200)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya
            )
            self.assertEqual((status, lookup["audio"]["parts_acknowledged"]), (200, 3))
            status, receipt = self._complete(client, maya, client_id, message_id, pcm, 1500)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))

    def test_malformed_requests_forbidden_actor_fields_and_invalid_paths_are_safe(self):
        with _running_server("hardening-malformed") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            status, raw, _ = client.request(
                "POST", "/v2/outgoing", body=b"{", bearer=maya, json_body=True
            )
            self.assertEqual((status, json.loads(raw)["code"]), (400, "unsupported_schema"))
            status, body = client.json_request(
                "POST", "/v2/session/refresh", {"unexpected": True}, bearer=maya
            )
            self.assertEqual((status, body["code"]), (400, "invalid_request"))
            status, body = client.json_request(
                "GET", "/v2/outgoing/not-a-uuid", bearer=maya
            )
            self.assertEqual((status, body["code"]), (404, "not_found"))
            status, body = client.json_request(
                "GET", "/v2/outgoing/not-a-uuid/unknown", bearer=maya
            )
            self.assertEqual((status, body["code"]), (404, "not_found"))
            status, body = client.json_request(
                "GET", "/v2/messages/not-valid!/media/audio", bearer=maya
            )
            self.assertEqual((status, body["code"]), (400, "invalid_request"))

            client_id = str(uuid.uuid4())
            create = {
                "schema": "fl.message-create/2",
                "client_message_id": client_id,
                "recipient_user_ids": ["user-jordan"],
                "sender_user_id": "user-jordan",
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 1000},
            }
            status, opened = client.json_request("POST", "/v2/outgoing", create, bearer=maya)
            self.assertEqual(status, 200)
            message_id = opened["message_id"]
            pcm = bytes(32000)
            self._upload(client, maya, message_id, pcm)
            status, receipt = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))
            jordan = self._login(client, 1)
            status, inbox = client.json_request("GET", "/v2/inbox", bearer=jordan)
            self.assertEqual(status, 200)
            item = next(row for row in inbox["items"] if row["message_id"] == message_id)
            self.assertEqual(item["sender_user_id"], "user-maya")

    def test_corrupt_open_metadata_and_missing_media_fail_closed_after_restart(self):
        with _running_server("hardening-corrupt-open") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            client_id, message_id = self._open(client, maya)
            part = bytes(server.PART_SIZE)
            self.assertEqual(
                client.request("PUT", f"/v2/outgoing/{message_id}/parts/audio/0",
                               body=part, bearer=maya)[0],
                200,
            )
            server._open_metadata_path(client_id).write_text("{", encoding="utf-8")

        server.STORE = server.Store(server._load_tombstones())
        server._restore_persisted_deliveries()
        with _running_server("hardening-corrupt-open-reload", reload_tombstones=True) as base:
            client = _Client(base)
            maya = self._login(client, 0)
            status, body = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya
            )
            self.assertEqual((status, body["code"]), (404, "not_found"))

            pcm = bytes(32000)
            accepted_id, accepted_message = self._open(client, maya)
            self._upload(client, maya, accepted_message, pcm)
            status, _ = self._complete(client, maya, accepted_id, accepted_message, pcm)
            self.assertEqual(status, 200)
            media_path = server._accepted_media_path(accepted_message, "audio")
            media_path.unlink()
            jordan = self._login(client, 1)
            status, body = client.json_request(
                "GET", f"/v2/messages/{accepted_message}/media/audio", bearer=jordan
            )
            self.assertEqual((status, body["code"], body["retryable"]),
                             (503, "storage_unavailable", True))

    def test_media_range_boundaries_and_all_or_nothing_recipient_refusal_survive_restart(self):
        # Use a valid four-user deployment for the first process, replacing
        # Casey with a pilot identity that is absent from the default YAML.
        # The second process then reloads the default deployment, making the
        # frozen recipient unavailable without mutating in-memory ROSTER.
        deployment = Path(self.temp.name) / "deployment-sam.yaml"
        deployment.write_text(
            server.DEFAULT_DEPLOYMENT_PATH.read_text(encoding="utf-8").replace(
                '  - id: user-casey\n    name: Casey\n    pin: "1357"\n',
                '  - id: user-sam\n    name: Sam\n    pin: "1357"\n',
            ),
            encoding="utf-8",
        )
        with _running_server("hardening-frozen-recipients", deployment_path=deployment) as base:
            client = _Client(base)
            maya = self._login(client, 0)
            client_id = str(uuid.uuid4())
            create = {
                "schema": "fl.message-create/2",
                "client_message_id": client_id,
                "recipient_user_ids": ["user-jordan", "user-sam"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 1000},
            }
            status, opened = client.json_request("POST", "/v2/outgoing", create, bearer=maya)
            self.assertEqual(status, 200)
            message_id = opened["message_id"]
            pcm = bytes(32000)
            self._upload(client, maya, message_id, pcm)

        server.STORE = server.Store(server._load_tombstones())
        server._restore_persisted_deliveries()
        with _running_server("hardening-frozen-recipients-reload", reload_tombstones=True) as base:
            client = _Client(base)
            maya = self._login(client, 0)
            status, body = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual((status, body["code"]), (422, "recipient_unavailable"))
            self.assertNotIn(client_id, server.STORE.accepted)
            self.assertNotIn("user-jordan", server.STORE.inboxes)
            self.assertNotIn("user-sam", server.STORE.inboxes)

            accepted_id, accepted_message = self._open(client, maya)
            self._upload(client, maya, accepted_message, pcm)
            status, _ = self._complete(client, maya, accepted_id, accepted_message, pcm)
            self.assertEqual(status, 200)
            jordan = self._login(client, 1)
            status, raw, headers = client.request(
                "GET", f"/v2/messages/{accepted_message}/media/audio", bearer=jordan,
                headers={"Range": "bytes=0-0"},
            )
            self.assertEqual((status, raw, headers["Content-Range"]),
                             (206, pcm[:1], f"bytes 0-0/{len(pcm)}"))
            status, raw, headers = client.request(
                "GET", f"/v2/messages/{accepted_message}/media/audio", bearer=jordan,
                headers={"Range": f"bytes={len(pcm) - 1}-"},
            )
            self.assertEqual((status, raw, headers["Content-Range"]),
                             (206, pcm[-1:], f"bytes {len(pcm) - 1}-{len(pcm) - 1}/{len(pcm)}"))
            status, raw, headers = client.request(
                "GET", f"/v2/messages/{accepted_message}/media/audio", bearer=jordan,
                headers={"Range": "bytes=-1"},
            )
            self.assertEqual((status, raw, headers["Content-Range"]),
                             (206, pcm[-1:], f"bytes {len(pcm) - 1}-{len(pcm) - 1}/{len(pcm)}"))

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

    def test_accepts_full_180_second_pcm_with_bounded_complete(self):
        with _running_server("hardening-180-seconds") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = b"\0\0" * (server.MAX_AUDIO_BYTES // 2)
            client_id, message_id = self._open(client, maya, duration_ms=180000)
            self._upload(client, maya, message_id, pcm)
            status, receipt = self._complete(
                client, maya, client_id, message_id, pcm, duration_ms=180000
            )
            self.assertEqual((status, receipt["state"]), (200, "accepted"))
            self.assertEqual(server._accepted_media_path(message_id, "audio").stat().st_size,
                             server.MAX_AUDIO_BYTES)

    def test_open_upload_concurrency_limit_is_retryable_capacity_error(self):
        with _running_server("hardening-open-capacity") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            for _ in range(server.MAX_OPEN_UPLOADS):
                self._open(client, maya)
            status, body = client.json_request(
                "POST", "/v2/outgoing", {
                    "schema": "fl.message-create/2",
                    "client_message_id": str(uuid.uuid4()),
                    "recipient_user_ids": ["user-jordan"],
                    "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                              "channels": 1, "duration_ms": 1000},
                }, bearer=maya,
            )
            self.assertEqual((status, body["code"], body["retryable"]),
                             (429, "resource_exhausted", True))

    def test_open_upload_byte_and_part_limits_are_bounded(self):
        with _running_server("hardening-open-bounds") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            old_total = server.MAX_TOTAL_OPEN_UPLOAD_BYTES
            try:
                server.MAX_TOTAL_OPEN_UPLOAD_BYTES = server.PART_SIZE
                client_id, message_id = self._open(client, maya)
                status, _, _ = client.request(
                    "PUT", f"/v2/outgoing/{message_id}/parts/audio/0",
                    body=bytes(server.PART_SIZE), bearer=maya,
                )
                self.assertEqual(status, 200)
                status, raw, _ = client.request(
                    "PUT", f"/v2/outgoing/{message_id}/parts/audio/1",
                    body=b"x", bearer=maya,
                )
                body = json.loads(raw.decode("utf-8"))
                self.assertEqual((status, body["code"], body["retryable"]),
                                 (429, "resource_exhausted", True))
            finally:
                server.MAX_TOTAL_OPEN_UPLOAD_BYTES = old_total

            client_id, message_id = self._open(client, maya, duration_ms=180000)
            status, raw, _ = client.request(
                "PUT", f"/v2/outgoing/{message_id}/parts/audio/{server.MAX_AUDIO_PARTS}",
                body=b"x", bearer=maya,
            )
            body = json.loads(raw.decode("utf-8"))
            self.assertEqual((status, body["code"], body["retryable"]),
                             (413, "media_too_large", False))
            self.assertIn(client_id, server.STORE.opens)

            sketch_id = str(uuid.uuid4())
            status, opened = client.json_request("POST", "/v2/outgoing", {
                "schema": "fl.message-create/2",
                "client_message_id": sketch_id,
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 1000},
                "sketch": {"format_id": "flsk2", "present": True},
            }, bearer=maya)
            self.assertEqual(status, 200)
            status, raw, _ = client.request(
                "PUT", f"/v2/outgoing/{opened['message_id']}/parts/sketch/{server.MAX_SKETCH_PARTS}",
                body=b"x", bearer=maya,
            )
            body = json.loads(raw.decode("utf-8"))
            self.assertEqual((status, body["code"], body["retryable"]),
                             (413, "media_too_large", False))

    def test_create_rejects_pcm_duration_above_contract_limit(self):
        with _running_server("hardening-create-limit") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            status, body = client.json_request("POST", "/v2/outgoing", {
                "schema": "fl.message-create/2",
                "client_message_id": str(uuid.uuid4()),
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 180001},
            }, bearer=maya)
            self.assertEqual((status, body["code"], body["retryable"]),
                             (413, "media_too_large", False))

    def test_inbox_overflow_is_truthful_and_does_not_truncate_identity(self):
        with _running_server("hardening-inbox-bounds") as base:
            client = _Client(base)
            jordan = self._login(client, 1)
            with server.STORE.lock:
                snapshot = server._inbox_for_user_locked("user-jordan")
                snapshot["items"].append({
                    "message_id": "msg-overflow",
                    "sender_user_id": "user-maya",
                    "sender_name": "M" * server.INBOX_MAX_BYTES,
                    "sender_avatar_id": None,
                    "sender_card_background_id": "card-bg-01",
                })
                snapshot["total_count"] = len(snapshot["items"])
            status, body = client.json_request("GET", "/v2/inbox", bearer=jordan)
            self.assertEqual((status, body["code"], body["retryable"]),
                             (503, "inbox_too_large", True))
            with server.STORE.lock:
                self.assertTrue(any(item["message_id"] == "msg-overflow"
                                    for item in server.STORE.inboxes["user-jordan"]["items"]))

    def test_accepted_item_projects_sender_presentation_with_fallbacks(self):
        # Bind invalid presentation values through startup so the test covers
        # runtime projection without mutating the loaded roster in place.
        with mock.patch.object(
            server, "_pilot_user_presentation", return_value=("", "card-bg-99")
        ):
            with _running_server("hardening-sender-presentation") as base:
                client = _Client(base)
                maya = self._login(client, 0)
                jordan = self._login(client, 1)
                pcm = bytes(32000)
                client_id, message_id = self._open(client, maya)
                self._upload(client, maya, message_id, pcm)
                status, _ = self._complete(client, maya, client_id, message_id, pcm)
                self.assertEqual(status, 200)
                status, inbox = client.json_request("GET", "/v2/inbox", bearer=jordan)
                self.assertEqual(status, 200)
                item = next(row for row in inbox["items"] if row["message_id"] == message_id)
                self.assertIn("sender_avatar_id", item)
                self.assertIn("sender_card_background_id", item)
                self.assertIsNone(item["sender_avatar_id"])
                self.assertEqual(item["sender_card_background_id"], "card-bg-01")

    def test_dynamic_media_range_and_storage_failure_are_bounded(self):
        with _running_server("hardening-media-range") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            jordan = self._login(client, 1)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            status, _ = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual(status, 200)
            status, chunk, headers = client.request(
                "GET", f"/v2/messages/{message_id}/media/audio", bearer=jordan,
                headers={"Range": "bytes=100-199"},
            )
            self.assertEqual((status, chunk), (206, pcm[100:200]))
            self.assertEqual(headers.get("Content-Range"),
                             f"bytes 100-199/{len(pcm)}")
            server._accepted_media_path(message_id, "audio").unlink()
            status, body = client.json_request(
                "GET", f"/v2/messages/{message_id}/media/audio", bearer=jordan
            )
            self.assertEqual((status, body["code"], body["retryable"]),
                             (503, "storage_unavailable", True))

    def test_removed_frozen_recipient_returns_unavailable_without_publication(self):
        deployment = Path(self.temp.name) / "deployment-jordan-removed.yaml"
        deployment.write_text(
            server.DEFAULT_DEPLOYMENT_PATH.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        with _running_server(
            "hardening-recipient-removal", deployment_path=deployment
        ) as base:
            client = _Client(base)
            maya = self._login(client, 0)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)

        deployment.write_text(
            deployment.read_text(encoding="utf-8").replace(
                '  - id: user-jordan\n    name: Jordan\n    pin: "5678"\n',
                '  - id: user-sam\n    name: Sam\n    pin: "5678"\n',
            ),
            encoding="utf-8",
        )
        with _running_server(
            "hardening-recipient-removal-restart",
            reload_tombstones=True,
            deployment_path=deployment,
        ) as base:
            client = _Client(base)
            maya = self._login(client, 0)
            status, body = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual((status, body["code"]), (422, "recipient_unavailable"))
            self.assertNotIn(client_id, server.STORE.accepted)
            self.assertNotIn(message_id, server.STORE.media)
            self.assertFalse(server._accepted_media_path(message_id, "audio").exists())
            self.assertNotIn("user-jordan", server.STORE.inboxes)

    def test_media_read_failure_returns_503_before_success_headers(self):
        with _running_server("hardening-media-read") as base:
            client = _Client(base)
            maya = self._login(client, 0)
            jordan = self._login(client, 1)
            pcm = bytes(32000)
            client_id, message_id = self._open(client, maya)
            self._upload(client, maya, message_id, pcm)
            status, _ = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual(status, 200)
            media_path = server._accepted_media_path(message_id, "audio")
            original_open = Path.open

            class FailingReader(io.BytesIO):
                def read(self, _size=-1):
                    raise OSError("simulated media read failure")

            def open_with_read_failure(path, *args, **kwargs):
                if path == media_path:
                    return FailingReader()
                return original_open(path, *args, **kwargs)

            with mock.patch.object(Path, "open", autospec=True,
                                   side_effect=open_with_read_failure):
                status, raw, headers = client.request(
                    "GET", f"/v2/messages/{message_id}/media/audio", bearer=jordan,
                    headers={"Range": "bytes=100-199"},
                )
            body = json.loads(raw.decode("utf-8"))
            self.assertEqual((status, body["code"], body["retryable"]),
                             (503, "storage_unavailable", True))
            self.assertNotEqual(status, 200)
            self.assertNotEqual(status, 206)
            self.assertNotIn("Content-Range", headers)

    def test_readme_documents_p3_operational_limits(self):
        readme = (Path(__file__).resolve().parents[2] / "server/device/README.md").read_text(
            encoding="utf-8"
        )
        for phrase in (
            "Server deployment/qualification limits",
            "6,022,144 bytes",
            "16 KiB",
            "352 audio parts",
            "16 sketch parts",
            "24,088,576 bytes",
            "4 concurrent open uploads",
            "32,768 `flsk2` points",
            "262,144 bytes",
            "retained on the server until later culling functionality",
        ):
            self.assertIn(phrase, readme)

    @staticmethod
    def _read_telemetry_events(path: Path) -> list[dict]:
        events = []
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("[stdout] "):
                events.append(json.loads(line[len("[stdout] "):]))
        return events

    def test_structured_telemetry_covers_routes_without_secrets_or_media(self):
        log_path = Path(self.temp.name) / "server.log"
        operation_id = str(uuid.uuid4())
        with _running_server("telemetry-run", log_path=str(log_path)) as base:
            client = _Client(base)
            status, _, _ = client.request("GET", "/health")
            self.assertEqual(status, 200)
            status, _, _ = client.request("GET", "/v2/hangout", bearer=DESK)
            self.assertEqual(status, 200)
            status, _ = client.json_request("POST", "/v2/endpoint/health", {
                "schema": "fl.endpoint-health/2",
                "reason": "boot",
                "boot_id": "boot-secret-must-not-be-logged",
                "muted": True,
            }, bearer=DESK)
            self.assertEqual(status, 200)

            user = ACCOUNTS["users"][0]
            login = {
                "schema": "fl.session-login/2",
                "operation_id": operation_id,
                "user_id": user["user_id"],
                "pin": user["pin"],
            }
            status, body = client.json_request("POST", "/v2/session/login", login, bearer=DESK)
            self.assertEqual(status, 200)
            maya = body["session"]
            status, replay = client.json_request("POST", "/v2/session/login", login, bearer=DESK)
            self.assertEqual((status, replay["session"]), (200, maya))
            status, _ = client.json_request("POST", "/v2/session/refresh", {}, bearer=maya)
            self.assertEqual(status, 200)

            client_id, message_id = self._open(client, maya)
            pcm = bytes(32000)
            self._upload(client, maya, message_id, pcm)
            status, lookup = client.json_request(
                "GET", f"/v2/outgoing/by-client-id/{client_id}", bearer=maya
            )
            self.assertEqual((status, lookup["state"]), (200, "open"))
            status, receipt = self._complete(client, maya, client_id, message_id, pcm)
            self.assertEqual((status, receipt["state"]), (200, "accepted"))

            jordan = self._login(client, 1)
            status, _ = client.json_request("GET", "/v2/inbox", bearer=jordan)
            self.assertEqual(status, 200)
            status, raw, _ = client.request(
                "GET", f"/v2/messages/{message_id}/media/audio", bearer=jordan,
                headers={"Range": "bytes=10-19"},
            )
            self.assertEqual((status, raw), (206, pcm[10:20]))
            status, _ = client.json_request(
                "POST", "/v2/session/invalidate", {
                    "schema": "fl.session-invalidate/2",
                    "operation_id": operation_id,
                    "session": maya,
                }, bearer=DESK,
            )
            self.assertEqual(status, 200)

            status, body = client.json_request("POST", "/v2/outgoing", {
                "schema": "fl.message-create/2",
                "client_message_id": str(uuid.uuid4()),
                "recipient_user_ids": ["user-maya"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 180001},
            }, bearer=jordan)
            self.assertEqual((status, body["code"]), (413, "media_too_large"))

        events = self._read_telemetry_events(log_path)
        self.assertGreaterEqual(len(events), 11)
        self.assertTrue(all(event["schema"] == server.TELEMETRY_SCHEMA for event in events))
        self.assertTrue(all(event["run_id"] == "telemetry-run" for event in events))
        self.assertTrue(all(event["fixture"] == server.FIXTURE_NAME for event in events))
        self.assertTrue(all(isinstance(event["elapsed_ms"], (int, float)) for event in events))
        self.assertTrue(all(event["attempt"] == 1 for event in events))
        self.assertTrue(all(event["route"] != "/unknown" for event in events))

        by_event = {event["event"]: event for event in events}
        for event_name in (
            "health", "roster", "endpoint_health", "pin_login", "session_refresh",
            "send_create", "send_part", "send_lookup", "send_complete", "inbox",
            "media_read", "session_invalidate",
        ):
            self.assertIn(event_name, by_event)
        self.assertEqual(by_event["send_part"]["sequence"], 1)
        self.assertEqual(by_event["media_read"]["range_start"], 10)
        self.assertEqual(by_event["media_read"]["range_end"], 19)
        self.assertEqual(by_event["send_complete"]["outcome"], "accepted")
        self.assertEqual(by_event["send_complete"]["client_message_id"], client_id.lower())
        pin_event = next(
            event for event in events
            if event["event"] == "pin_login" and event.get("operation_id") == operation_id.lower()
        )
        self.assertEqual(pin_event["operation_id"], operation_id.lower())

        log_text = log_path.read_text(encoding="utf-8")
        self.assertNotIn(user["pin"], log_text)
        self.assertNotIn(maya, log_text)
        self.assertNotIn("boot-secret-must-not-be-logged", log_text)
        self.assertNotIn(pcm.decode("latin1"), log_text)
        self.assertLessEqual(log_path.stat().st_size, 64 * 1024)

    def test_structured_telemetry_classifies_resource_and_storage_failures(self):
        log_path = Path(self.temp.name) / "failure-server.log"
        with _running_server("telemetry-failures", log_path=str(log_path)) as base:
            client = _Client(base)
            maya = self._login(client, 0)
            original = server._save_open_upload
            try:
                server._save_open_upload = lambda _upload: (_ for _ in ()).throw(
                    OSError("simulated storage failure with secret-token=must-not-log"))
                status, body = client.json_request("POST", "/v2/outgoing", {
                    "schema": "fl.message-create/2",
                    "client_message_id": str(uuid.uuid4()),
                    "recipient_user_ids": ["user-jordan"],
                    "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                              "channels": 1, "duration_ms": 1000},
                }, bearer=maya)
            finally:
                server._save_open_upload = original
            self.assertEqual((status, body["code"]), (500, "storage_unavailable"))

            for _ in range(server.MAX_OPEN_UPLOADS):
                self._open(client, maya)
            status, body = client.json_request("POST", "/v2/outgoing", {
                "schema": "fl.message-create/2",
                "client_message_id": str(uuid.uuid4()),
                "recipient_user_ids": ["user-jordan"],
                "audio": {"codec": "pcm_s16le", "sample_rate_hz": 16000,
                          "channels": 1, "duration_ms": 1000},
            }, bearer=maya)
            self.assertEqual((status, body["code"]), (429, "resource_exhausted"))

        events = self._read_telemetry_events(log_path)
        resource = next(event for event in events if event.get("error_code") == "resource_exhausted")
        storage = next(event for event in events if event.get("error_code") == "storage_unavailable")
        self.assertEqual(resource["status_class"], "resource")
        self.assertEqual(storage["status_class"], "storage")
        self.assertNotIn("secret-token", log_path.read_text(encoding="utf-8"))

    def test_existing_log_sink_remains_bounded_and_redacted(self):
        log_path = Path(self.temp.name) / "bounded.log"
        log = server.BoundedRedactingLog(log_path, max_bytes=160, max_line_bytes=32)
        try:
            log.feed("stdout", b'Authorization: Bearer bearer-secret\n')
            log.feed("stdout", b'password=1234\n')
            log.feed("stdout", b"x" * 64 + b"\n")
        finally:
            log.close()
        text = log_path.read_text(encoding="utf-8")
        self.assertLessEqual(log_path.stat().st_size, 160)
        self.assertNotIn("bearer-secret", text)
        self.assertNotIn("1234", text)
        self.assertIn("[server log", text)


if __name__ == "__main__":
    unittest.main()
