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
        req = urllib.request.Request(
            "http://127.0.0.1:18765/lab/script",
            data=json.dumps({"name": "remove_at", "index": 0}).encode("utf-8"),
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


if __name__ == "__main__":
    unittest.main()
