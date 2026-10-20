import socket
import signal
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from io import BytesIO
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lab02_server import ManagedLab02Server, ServerLifecycleError, select_fixture
from lab02_workflow_config import Lab02WorkflowConfig

ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "server/labs/lab-02-paging/server.py"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def config(port, server=SERVER, **deadlines):
    bounds = {"startup": 2, "health": 2, "seed": 2, "readiness": 2,
              "action": 1, "cleanup": 1}
    bounds.update(deadlines)
    return Lab02WorkflowConfig(Path(server), "127.0.0.1", port, 64, "nominal", bounds,
                               f"http://127.0.0.1:{port}")


class ManagedLab02ServerTests(unittest.TestCase):
    def test_owned_child_log_is_bounded_redacted_and_reports_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child_script = root / "diagnostics.py"
            child_script.write_text(
                "import sys\n"
                "sys.stdout.buffer.write(b'password=top'); sys.stdout.flush()\n"
                "sys.stdout.buffer.write(b'secret-value\\nhttps://private.example/path\\n')\n"
                "sys.stdout.flush()\n"
                "sys.stderr.write('failure detail\\n'); sys.stderr.flush()\n"
                "for _ in range(1000): print('x' * 100)\n"
                "raise SystemExit(7)\n", encoding="utf-8")
            log_path = root / "run-123" / "server.log"
            events = []
            manager = ManagedLab02Server(config(12345), run_id="run-123",
                                         event_sink=events.append, log_path=log_path,
                                         max_log_bytes=1024)
            child = manager._spawn_owned([sys.executable, str(child_script)],
                                         time.monotonic() + 2)
            manager._process = child
            self.assertEqual(child.wait(timeout=2), 7)
            manager.close()

            raw = log_path.read_bytes()
            self.assertLessEqual(len(raw), 1024)
            self.assertIn(b"password=[REDACTED]", raw)
            self.assertNotIn(b"topsecret-value", raw)
            self.assertNotIn(b"private.example", raw)
            self.assertTrue(any(event["action_id"] == "server_process_exit"
                                and event.get("exit_code") == 7 for event in events))
            self.assertTrue(any(event["action_id"] == "server_log"
                                and event.get("artifact_path", "").endswith("server.log")
                                and event.get("truncated") is True for event in events))
            self.assertTrue(any(event["action_id"] == "server_cleanup"
                                and event["outcome"] == "success" for event in events))

    def test_log_redacts_credentials_split_across_read_chunks(self):
        with tempfile.TemporaryDirectory() as directory:
            from server_log import BoundedRedactingLog

            log_path = Path(directory) / "run" / "server.log"
            sink = BoundedRedactingLog(log_path)
            sink.feed("stdout", b"token=split-")
            sink.feed("stdout", b"across-chunks\n")
            sink.close()
            text = log_path.read_text(encoding="utf-8")
            self.assertIn("token=[REDACTED]", text)
            self.assertNotIn("split-across-chunks", text)

    def test_real_owned_child_health_seed_and_idempotent_cleanup(self):
        events = []
        manager = ManagedLab02Server(config(free_port()), run_id="run-test-1", event_sink=events.append)
        fixture = manager.start()
        child = manager._process
        self.assertEqual(fixture.size, 64)
        self.assertTrue(child and child.poll() is None)
        page = manager._request("/v2/inbox?cursor=0&limit=16")
        self.assertEqual(page.get("run_id"), manager.run_id)
        self.assertEqual(len(page.get("page_receipt_id", "")), 32)
        action = manager.post_action("paired-action", {"name": "push_item", "item": {"preview": "proof"}})
        self.assertEqual(action.get("run_id"), manager.run_id)
        self.assertEqual(action.get("action_id"), "paired-action")
        self.assertTrue(action.get("ok"))
        self.assertGreater(action.get("inbox_revision", 0), page.get("inbox_revision", 0))
        self.assertGreater(action.get("action_completed_ns", 0), 0)
        self.assertEqual({e["action_id"] for e in events} & {"server_bind", "server_start", "server_health", "server_seed"},
                         {"server_bind", "server_start", "server_health", "server_seed"})
        seed = next(e for e in events if e["action_id"] == "server_seed")
        self.assertEqual(seed["outcome"], "success")
        manager.close()
        manager.close()
        self.assertIsNotNone(child.poll())
        self.assertEqual(sum(e["action_id"] == "server_cleanup" for e in events), 1)
        self.assertEqual(fixture.sha256, select_fixture(64).sha256)

    def test_foreign_listener_is_never_stopped(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        port = listener.getsockname()[1]
        events = []
        manager = ManagedLab02Server(config(port), event_sink=events.append)
        try:
            with self.assertRaisesRegex(ServerLifecycleError, "port_occupied"):
                manager.start()
            self.assertIsNone(manager._process)
            self.assertGreaterEqual(listener.fileno(), 0)
            self.assertTrue(any(e["action_id"] == "server_bind" and e["outcome"] == "failure" for e in events))
        finally:
            manager.close()
            listener.close()

    def test_wrong_health_identity_times_out_and_owned_child_is_cleaned(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = Path(directory) / "wrong_health.py"
            fake.write_text("""from http.server import BaseHTTPRequestHandler,HTTPServer
import argparse,json
ap=argparse.ArgumentParser();ap.add_argument('--host');ap.add_argument('--port',type=int);a,_=ap.parse_known_args()
class H(BaseHTTPRequestHandler):
 def do_GET(self):
  b=json.dumps({'ok':True,'run_id':'foreign'}).encode();self.send_response(200);self.end_headers();self.wfile.write(b)
HTTPServer((a.host,a.port),H).serve_forever()
""", encoding="utf-8")
            events = []
            manager = ManagedLab02Server(config(free_port(), fake, health=1), event_sink=events.append)
            with self.assertRaisesRegex(ServerLifecycleError, "health_timeout"):
                manager.start()
            self.assertIsNone(manager._process)
            self.assertTrue(any(e["action_id"] == "server_health" and e["outcome"] == "failure" for e in events))

    def test_startup_exit_has_terminal_health_and_cleanup_events(self):
        with tempfile.TemporaryDirectory() as directory:
            exits = Path(directory) / "exit.py"
            exits.write_text("raise SystemExit(7)\n", encoding="utf-8")
            events = []
            manager = ManagedLab02Server(config(free_port(), exits), event_sink=events.append)
            with self.assertRaises(ServerLifecycleError):
                manager.start()
            self.assertEqual(sum(e["action_id"] == "server_health" for e in events), 1)
            self.assertEqual(sum(e["action_id"] == "server_cleanup" for e in events), 1)

    def test_seed_timeout_cleans_owned_child_with_one_terminal_seed_event(self):
        events = []
        manager = ManagedLab02Server(config(free_port(), seed=1), event_sink=events.append)
        original_request = manager._request

        def fail_seed(path, **kwargs):
            if path == "/lab/seed":
                raise TimeoutError("simulated seed timeout")
            return original_request(path, **kwargs)

        with patch.object(manager, "_request", side_effect=fail_seed):
            with self.assertRaisesRegex(ServerLifecycleError, "seed_failed"):
                manager.start()
        self.assertIsNone(manager._process)
        seed_events = [e for e in events if e["action_id"] == "server_seed"]
        self.assertEqual(len(seed_events), 1)
        self.assertEqual(seed_events[0]["outcome"], "failure")
        self.assertEqual(sum(e["action_id"] == "server_cleanup" for e in events), 1)

    def test_action_timeout_and_cancel_each_emit_one_terminal(self):
        events = []
        manager = ManagedLab02Server(config(free_port()), event_sink=events.append)
        with patch.object(manager, "_request", side_effect=TimeoutError):
            with self.assertRaisesRegex(ServerLifecycleError, "action_failed"):
                manager.post_action("act-timeout", {"name": "delay_ms"})
        cancelled = threading.Event()
        cancelled.set()
        with self.assertRaisesRegex(ServerLifecycleError, "action_cancelled"):
            manager.post_action("act-cancel", {}, cancelled=cancelled)
        self.assertEqual(sum(e["action_id"] == "act-timeout" for e in events), 1)
        self.assertEqual(sum(e["action_id"] == "act-cancel" for e in events), 1)

    def test_startup_deadline_cancels_late_spawn_and_reaps_child(self):
        with tempfile.TemporaryDirectory() as directory:
            events = []
            log_path = Path(directory) / "run" / "server.log"
            manager = ManagedLab02Server(config(12345, startup=1), event_sink=events.append,
                                         log_path=log_path)
            manager.config.deadlines_seconds["startup"] = 0.01
            class SlowChild:
                def __init__(self):
                    self.terminated = False
                    self.stdout = BytesIO(b"owned server diagnostic\n")
                    self.stderr = BytesIO(b"")
                def terminate(self): self.terminated = True
                def wait(self, timeout=None): return 0
                def poll(self): return 0 if self.terminated else None
            child = SlowChild()
            def slow_popen(*args, **kwargs):
                time.sleep(0.04)
                return child
            with patch.object(manager, "_listener_occupied", return_value=False), \
                    patch("lab02_server.subprocess.Popen", side_effect=slow_popen):
                with self.assertRaisesRegex(ServerLifecycleError, "startup_timeout"):
                    manager.start()
            self.assertTrue(child.terminated)
            self.assertIsNone(manager._spawn_thread)
            self.assertEqual(sum(e["action_id"] == "server_start" and e["outcome"] == "failure" for e in events), 1)
            self.assertTrue(log_path.is_file())
            self.assertIn(b"owned server diagnostic", log_path.read_bytes())
            self.assertTrue(any(e["action_id"] == "server_process_exit" and e.get("exit_code") == 0
                                for e in events))
            self.assertTrue(any(e["action_id"] == "server_log" for e in events))
            cleanup = [e for e in events if e["action_id"] == "server_cleanup"]
            self.assertEqual(len(cleanup), 1)
            self.assertEqual(cleanup[0]["outcome"], "success")

    def test_late_spawn_reap_failure_retains_child_for_close_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            events = []
            manager = ManagedLab02Server(config(12345, startup=1), event_sink=events.append,
                                         log_path=Path(directory) / "run" / "server.log")
            manager.config.deadlines_seconds["startup"] = 0.01
            class SlowChild:
                def __init__(self):
                    self.wait_calls = 0
                    self.returncode = None
                    self.stdout = BytesIO(b"late server diagnostic\n")
                    self.stderr = BytesIO(b"")
                def terminate(self): pass
                def kill(self): pass
                def wait(self, timeout=None):
                    self.wait_calls += 1
                    if self.wait_calls == 1:
                        raise OSError("simulated wait failure")
                    self.returncode = 0
                    return self.returncode
                def poll(self): return self.returncode
            child = SlowChild()
            def slow_popen(*args, **kwargs):
                time.sleep(0.04)
                return child
            with patch.object(manager, "_listener_occupied", return_value=False), \
                    patch("lab02_server.subprocess.Popen", side_effect=slow_popen):
                with self.assertRaisesRegex(ServerLifecycleError, "startup_timeout"):
                    manager.start()
            self.assertIs(manager._process, child)
            initial_cleanup = [event for event in events if event["action_id"] == "server_cleanup"]
            self.assertEqual(len(initial_cleanup), 1)
            self.assertEqual(initial_cleanup[0]["outcome"], "failure")

            manager.close()
            self.assertIsNone(manager._process)
            cleanup_events = [event for event in events if event["action_id"] == "server_cleanup"]
            self.assertEqual([event["outcome"] for event in cleanup_events], ["failure", "success"])
            self.assertTrue(any(event["action_id"] == "server_process_exit"
                                and event.get("exit_code") == 0 for event in events))

    def test_seed_interrupt_has_one_cancelled_terminal(self):
        events = []
        manager = ManagedLab02Server(config(free_port()), event_sink=events.append)
        manager.fixture = select_fixture(64)
        with patch.object(manager, "_request", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                manager._seed()
        seed = [e for e in events if e["action_id"] == "server_seed"]
        self.assertEqual(len(seed), 1)
        self.assertEqual(seed[0]["outcome"], "cancelled")

    def test_seed_rejects_non_ok_receipt(self):
        events = []
        manager = ManagedLab02Server(config(free_port()), event_sink=events.append)
        manager.fixture = select_fixture(64)
        class AliveChild:
            def poll(self): return None
        manager._process = AliveChild()
        with patch.object(manager, "_request", return_value={"ok": False, "run_id": manager.run_id,
                                                                  "fixture_sha256": manager.fixture.sha256,
                                                                  "count": manager.fixture.size}):
            with self.assertRaisesRegex(ServerLifecycleError, "seed_receipt_mismatch"):
                manager._seed()
        seed = [e for e in events if e["action_id"] == "server_seed"]
        self.assertEqual(len(seed), 1)
        self.assertEqual(seed[0]["outcome"], "failure")

    def test_action_rejection_and_interrupt_each_have_one_terminal(self):
        events = []
        manager = ManagedLab02Server(config(free_port()), event_sink=events.append)
        with patch.object(manager, "_request", return_value={"ok": False, "error": "rejected"}):
            with self.assertRaisesRegex(ServerLifecycleError, "action_failed"):
                manager.post_action("act-reject", {"name": "remove_at", "index": 999})
        with patch.object(manager, "_request", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                manager.post_action("act-interrupt", {"name": "push_item"})
        for action_id, outcome in (("act-reject", "failure"), ("act-interrupt", "cancelled")):
            matching = [e for e in events if e["action_id"] == action_id]
            self.assertEqual(len(matching), 1)
            self.assertEqual(matching[0]["outcome"], outcome)

    def test_cleanup_failure_keeps_owned_handle_and_can_retry(self):
        events = []
        manager = ManagedLab02Server(config(free_port()), event_sink=events.append)
        class Child:
            def __init__(self): self.calls = 0
            def poll(self): return None
            def terminate(self): pass
            def kill(self): pass
            def wait(self, timeout=None):
                self.calls += 1
                if self.calls == 1: raise OSError("wait failed")
                return 0
        child = Child()
        manager._process = child
        with self.assertRaisesRegex(ServerLifecycleError, "cleanup_failed"):
            manager.close()
        self.assertIs(manager._process, child)
        manager.close()
        self.assertIsNone(manager._process)
        self.assertTrue(any(e["action_id"] == "server_cleanup" and e["outcome"] == "failure"
                            for e in events))
        self.assertTrue(any(e["action_id"] == "server_process_exit" and e.get("exit_code") == 0
                            for e in events))

    @unittest.skipUnless(
        os.name == "posix",
        f"Cannot run on os.name={os.name!r}: this platform does not stop a child "
        "with POSIX SIGTERM. Parallel coverage: "
        "test_non_posix_terminate_fallback_does_not_claim_signal_delivery.",
    )
    def test_close_records_deliberate_sigterm_as_expected_shutdown(self):
        events = []
        manager = ManagedLab02Server(config(12345), event_sink=events.append)
        class TerminatedByClose:
            pid = 12345
            def __init__(self): self.returncode = None
            def poll(self): return self.returncode
            def terminate(self): raise AssertionError("POSIX close should use os.kill")
            def wait(self, timeout=None): return self.returncode
        child = TerminatedByClose()
        def deliver_sigterm(pid, signum):
            self.assertEqual(pid, child.pid)
            self.assertEqual(signum, signal.SIGTERM)
            child.returncode = -signal.SIGTERM
        manager._process = child

        with patch("lab02_server.os.kill", side_effect=deliver_sigterm):
            manager.close()

        exit_event = next(e for e in events if e["action_id"] == "server_process_exit")
        self.assertEqual(exit_event["outcome"], "success")
        self.assertEqual(exit_event["reason"], "expected_shutdown")
        self.assertEqual(exit_event["exit_code"], -signal.SIGTERM)

    @unittest.skipUnless(
        os.name == "posix",
        f"Cannot run on os.name={os.name!r}: this platform does not stop a child "
        "with POSIX SIGTERM. Parallel coverage: "
        "test_non_posix_terminate_fallback_does_not_claim_signal_delivery.",
    )
    def test_close_does_not_attribute_sigterm_when_child_exits_before_signal_delivery(self):
        events = []
        manager = ManagedLab02Server(config(12345), event_sink=events.append)
        class ExitsInSignalWindow:
            pid = 12345
            def __init__(self): self.returncode = None
            def poll(self): return self.returncode
            def wait(self, timeout=None): return self.returncode
        child = ExitsInSignalWindow()
        manager._process = child
        def child_disappeared(pid, signum):
            self.assertEqual(pid, child.pid)
            self.assertEqual(signum, signal.SIGTERM)
            child.returncode = -signal.SIGTERM
            raise ProcessLookupError

        with patch("lab02_server.os.kill", side_effect=child_disappeared):
            manager.close()

        exit_event = next(e for e in events if e["action_id"] == "server_process_exit")
        self.assertEqual(exit_event["outcome"], "failure")
        self.assertEqual(exit_event["reason"], "nonzero_exit")
        self.assertEqual(exit_event["exit_code"], -signal.SIGTERM)

    def test_non_posix_terminate_fallback_does_not_claim_signal_delivery(self):
        """Windows parallel for the two POSIX SIGTERM close tests."""
        events = []
        manager = ManagedLab02Server(config(12345), event_sink=events.append)
        class FallbackChild:
            def __init__(self): self.returncode = None
            def poll(self): return self.returncode
            def terminate(self): self.returncode = -signal.SIGTERM
            def wait(self, timeout=None): return self.returncode
        manager._process = FallbackChild()

        with patch("lab02_server.os.name", "nt"):
            manager.close()

        exit_event = next(e for e in events if e["action_id"] == "server_process_exit")
        self.assertEqual(exit_event["outcome"], "failure")
        self.assertEqual(exit_event["reason"], "nonzero_exit")

    def test_close_keeps_early_exit_and_forced_kill_as_failures(self):
        early_events = []
        early = ManagedLab02Server(config(12345), event_sink=early_events.append)
        class EarlyExit:
            def poll(self): return -15
        early._process = EarlyExit()
        early.close()
        early_exit = next(e for e in early_events if e["action_id"] == "server_process_exit")
        self.assertEqual(early_exit["outcome"], "failure")

        forced_events = []
        forced = ManagedLab02Server(config(12345), event_sink=forced_events.append)
        class ForcedKill:
            def __init__(self): self.returncode = None; self.wait_calls = 0
            def poll(self): return self.returncode
            def terminate(self): pass
            def wait(self, timeout=None):
                self.wait_calls += 1
                if self.wait_calls == 1:
                    raise subprocess.TimeoutExpired("server", timeout)
                return self.returncode
            def kill(self): self.returncode = -9
        forced._process = ForcedKill()
        forced.close()
        forced_exit = next(e for e in forced_events if e["action_id"] == "server_process_exit")
        self.assertEqual(forced_exit["outcome"], "failure")
        self.assertEqual(forced_exit["exit_code"], -9)

    def test_real_rejected_http_action_is_failure(self):
        manager = ManagedLab02Server(config(free_port()), run_id="run-rejected")
        manager.start()
        try:
            with self.assertRaisesRegex(ServerLifecycleError, "action_failed"):
                manager.post_action("reject-real", {"name": "remove_at", "index": 99999})
        finally:
            manager.close()

    def test_request_deadline_bounds_slow_drip_body(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        port = listener.getsockname()[1]
        manager = ManagedLab02Server(config(port))
        def drip():
            conn, _ = listener.accept()
            with conn:
                conn.recv(4096)
                conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\n")
                for byte in b"true":
                    time.sleep(0.07)
                    try: conn.sendall(bytes((byte,)))
                    except OSError: break
            listener.close()
        worker = threading.Thread(target=drip)
        worker.start()
        started = time.monotonic()
        try:
            with self.assertRaises(TimeoutError):
                manager._request("/slow", timeout=0.15)
            self.assertLess(time.monotonic() - started, 0.27)
        finally:
            worker.join(timeout=1)


if __name__ == "__main__":
    unittest.main()
