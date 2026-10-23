"""Focused HTTP regressions for product server persistence and authorization."""
from __future__ import annotations

import hashlib
import json
import tempfile
import threading
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
