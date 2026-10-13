import json
import subprocess
import sys
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "server/labs/lab-02-paging/server.py"


class Lab02PagingServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proc = subprocess.Popen(
            [sys.executable, str(SERVER), "--port", "18765"],
            cwd=str(ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            try:
                urllib.request.urlopen("http://127.0.0.1:18765/lab/health", timeout=1)
                break
            except (urllib.error.URLError, TimeoutError):
                time.sleep(0.1)
        else:
            cls.proc.kill()
            raise RuntimeError("paging server did not start")

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=5)

    def test_inbox_page_v2_shape(self):
        with urllib.request.urlopen(
            "http://127.0.0.1:18765/v2/inbox?cursor=0&limit=16", timeout=5
        ) as resp:
            body = json.loads(resp.read().decode("utf-8"))
        self.assertEqual(body["schema"], "fl.inbox-page/2")
        self.assertEqual(len(body["items"]), 16)
        self.assertTrue(body["has_more"])
        self.assertEqual(body["items"][0]["message_id"], "msg-0000")

    def test_remove_script(self):
        health = json.loads(urllib.request.urlopen("http://127.0.0.1:18765/lab/health", timeout=5).read())
        run_id = health["run_id"]
        seed = urllib.request.Request("http://127.0.0.1:18765/lab/seed",
                                      data=json.dumps({"run_id": run_id}).encode(),
                                      headers={"Content-Type": "application/json"}, method="POST")
        urllib.request.urlopen(seed, timeout=5).read()
        req = urllib.request.Request(
            "http://127.0.0.1:18765/lab/script",
            data=json.dumps({"name": "remove_message", "viewed_message_id": "msg-0000",
                             "run_id": run_id, "action_id": "remove-test"}).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            self.assertEqual(resp.status, 200)
        with urllib.request.urlopen(
            "http://127.0.0.1:18765/v2/inbox?cursor=0&limit=16", timeout=5
        ) as resp:
            body = json.loads(resp.read().decode("utf-8"))
        self.assertEqual(body["total_count"], 63)
        self.assertEqual(body["items"][0]["message_id"], "msg-0001")

    def test_page_receipt_and_idempotent_identity_targeted_actions(self):
        with urllib.request.urlopen("http://127.0.0.1:18765/v2/inbox?cursor=0&limit=16", timeout=5) as resp:
            page = json.loads(resp.read())
        self.assertRegex(page["page_receipt_id"], r"^[0-9a-f]{32}$")
        self.assertLessEqual(page["page_started_ns"], page["page_completed_ns"])
        run_id = json.loads(urllib.request.urlopen("http://127.0.0.1:18765/lab/health", timeout=5).read())["run_id"]
        action = {"name": "push_item", "run_id": run_id, "action_id": "push-once",
                  "item": {"sender_name": "Lab", "preview": "pushed"}}
        def post(body):
            req = urllib.request.Request("http://127.0.0.1:18765/lab/script", data=json.dumps(body).encode(),
                                          headers={"Content-Type": "application/json"}, method="POST")
            with urllib.request.urlopen(req, timeout=5) as resp: return json.loads(resp.read())
        first = post(action)
        second = post(action)
        self.assertEqual(first, second)
        self.assertEqual(first["action_id"], "push-once")


if __name__ == "__main__":
    unittest.main()
