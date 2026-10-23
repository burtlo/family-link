"""LAB-11 parser, fixture identity, and adapter checks."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.request
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

_PARSER_SPEC = importlib.util.spec_from_file_location(
    "parse_lab11_trace", ROOT / "client/labs/lab-11-send-receipt/tools/parse_lab_trace.py")
parser = importlib.util.module_from_spec(_PARSER_SPEC)
sys.modules[_PARSER_SPEC.name] = parser
_PARSER_SPEC.loader.exec_module(parser)

_SERVER_SPEC = importlib.util.spec_from_file_location(
    "lab11_fixture_server", ROOT / "server/labs/lab-11-send-receipt/server.py")
server = importlib.util.module_from_spec(_SERVER_SPEC)
sys.modules[_SERVER_SPEC.name] = server
_SERVER_SPEC.loader.exec_module(server)

from lab_adapter_lab11 import (  # noqa: E402
    DEADLINES_SECONDS,
    PORT,
    Adapter,
    OwnedServer,
    classify_capture,
    select_variant,
    server_identity,
)
from project_config import ProjectConfigError  # noqa: E402
from server_log import redact_line  # noqa: E402

LAB = ROOT / "client/labs/lab-11-send-receipt"
FIXTURES = ROOT / "server/labs/lab-11-send-receipt/fixtures"


def _compiler() -> str:
    for name in ("gcc", "cc", "clang"):
        found = shutil.which(name)
        if found:
            return found
    raise unittest.SkipTest("no host C compiler")


class Lab11HostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary_dir = tempfile.TemporaryDirectory(prefix="lab11-host-")
        cls.temp = binary_dir
        binary = Path(binary_dir.name) / "lab11_model_test.exe"
        compile_cmd = [
            _compiler(), "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(LAB / "components/lab11_capture/include"),
            "-I", str(LAB / "components/lab11_send/include"),
            str(LAB / "components/lab11_capture/lab11_capture.c"),
            str(LAB / "components/lab11_capture/lab11_sha256.c"),
            str(LAB / "components/lab11_send/lab11_send.c"),
            str(LAB / "tests/model_test.c"),
            "-o", str(binary),
        ]
        if sys.platform == "win32":
            compile_cmd.append("-lws2_32")
        subprocess.run(compile_cmd, check=True, cwd=ROOT)
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        cls.httpd = server.create_server("127.0.0.1", port, "lab11-host-trace")
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        cls.thread.start()
        env = os.environ.copy()
        env["LAB11_PORT"] = str(port)
        completed = subprocess.run([str(binary), "scripted"], check=True, cwd=ROOT, env=env,
                                   capture_output=True, text=True)
        cls.serial = completed.stdout

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.thread.join(timeout=2)
        cls.temp.cleanup()

    def test_s02_manifest_and_s03_parser(self):
        project = json.loads((LAB / "manifest.lab.json").read_text(encoding="utf-8"))
        for key in ("document", "lab_id", "firmware_path", "server_path", "fixture_schema",
                    "fixture_revision", "protocol", "trace_schema"):
            self.assertIn(key, project)
        self.assertEqual(project["document"], "family-link.lab-manifest/v1")
        self.assertEqual(project["lab_id"], "LAB-11")
        self.assertEqual(project["trace_schema"], "lab11-trace/v1")
        trace = json.loads((LAB / "trace-schema.v1.json").read_text(encoding="utf-8"))
        self.assertEqual(trace["schema"], project["trace_schema"])
        result = parser.parse(self.serial, "run-lab11")
        self.assertEqual(result["document"], "family-link.lab-metrics/v2")
        self.assertEqual(result["run_id"], "run-lab11")
        self.assertEqual(result["schema_version"], "lab11-trace/v1")

    def test_lab11_experiment_inputs_and_open_sent(self):
        result = parser.parse(self.serial, "run-lab11", mode="scripted")
        metrics = result["lab_metrics"]
        self.assertEqual(metrics["experiment_inputs"], [
            "D-one", "D-drop-ack", "D-drop-receipt", "D-full", "D-cancel",
        ])
        self.assertEqual(metrics["experiment_verdict"], "PASS")
        self.assertEqual(metrics["performance_verdict"], "UNMEASURED")
        self.assertNotIn("sent_approved", json.dumps(result))
        for scenario_id in ("D-many", "D-invalid-recipient", "D-offline", "D-push-fail", "D-restart"):
            self.assertNotIn(scenario_id, metrics["experiment_inputs"])
            self.assertEqual(metrics["scenarios"][scenario_id]["class"], "exploratory")
            self.assertIn("terminal", metrics["scenarios"][scenario_id]["observation"])
        self.assertEqual(metrics["scenarios"]["D-offline"]["observation"]["work"], "discarded")
        self.assertEqual(metrics["scenarios"]["D-push-fail"]["observation"]["push_result"], "failed")
        self.assertEqual(metrics["scenarios"]["D-push-fail"]["observation"]["accepted"], 1)
        self.assertEqual(metrics["scenarios"]["D-restart"]["observation"]["restart_status"], "open")
        cancel = metrics["scenarios"]["D-cancel"]["samples"]
        self.assertEqual(len(cancel), 4)
        self.assertEqual(len({row["operation_id"] for row in cancel}), 4)
        self.assertTrue(all(row["terminal_count"] == "1" for row in cancel))
        chunks = {row["chunk_bytes"] for row in metrics["resource_accounts"]}
        self.assertEqual(chunks, {"1024", "4096"})
        self.assertTrue(all(row["full_payload_duplicate_bytes"] == "0" for row in metrics["resource_accounts"]))
        ledger = metrics["resource_ledgers"][0]
        self.assertEqual(ledger["provenance"], "unmeasured/blocked")
        self.assertIsNone(ledger["p50"])
        self.assertIsNone(ledger["largest_free_block_bytes"])
        self.assertNotEqual(ledger["provenance"], "hardware")
        workflow_pass = {"workflow_status": "PASS"}
        self.assertNotIn("experiment_verdict", workflow_pass)

    def test_s22_exploratory_rows_do_not_decide_the_verdict(self):
        failing = self.serial.replace(
            "scenario=D-one class=decided terminal=success",
            "scenario=D-one class=decided terminal=failure",
            1,
        )
        failed = parser.parse(failing, "run-fail")
        self.assertEqual(failed["lab_metrics"]["experiment_verdict"], "FAIL")
        self.assertEqual(failed["lab_metrics"]["scenarios"]["D-many"]["observation"]["terminal"], "success")
        exploratory_fail = self.serial.replace(
            "scenario=D-many class=exploratory terminal=success",
            "scenario=D-many class=exploratory terminal=failure",
            1,
        )
        passed = parser.parse(exploratory_fail, "run-pass")
        self.assertEqual(passed["lab_metrics"]["experiment_verdict"], "PASS")
        self.assertEqual(passed["lab_metrics"]["scenarios"]["D-many"]["observation"]["terminal"], "failure")
        empty = parser.parse(self.serial, "run-empty", experiment_inputs=[])
        self.assertEqual(empty["lab_metrics"]["experiment_inputs"], [])
        self.assertEqual(empty["lab_metrics"]["experiment_verdict"], "UNMEASURED")

    def test_parser_malformed_missing_duplicate_overflow_and_mode(self):
        with self.assertRaises(parser.ParseError):
            parser.parse("LAB11_TRACE this-is-malformed\n", "run-bad")
        with self.assertRaises(parser.ParseError):
            parser.parse(
                "LAB11_TRACE schema=lab11-trace/v1 event=scenario_terminal scenario=D-one class=decided "
                "terminal=success terminal_count=1 mode=scripted\n",
                "run-missing",
            )
        with self.assertRaises(parser.ParseError):
            parser.parse(
                "LAB11_TRACE schema=lab11-trace/v1 event=scenario_terminal operation_id=1 scenario=D-one "
                "class=decided terminal=success terminal_count=1 mode=scripted\n"
                "LAB11_TRACE schema=lab11-trace/v1 event=scenario_terminal operation_id=1 scenario=D-one "
                "class=decided terminal=success terminal_count=1 mode=scripted\n",
                "run-dup",
            )
        with self.assertRaises(parser.ParseError):
            parser.parse(
                "LAB11_TRACE schema=lab11-trace/v1 event=part_overflow operation_id=9 queue_count=8 "
                "overflow_count=1 dropped=1 mode=scripted\n",
                "run-drop",
            )
        with self.assertRaises(parser.ParseError):
            parser.parse(self.serial, "run-mode", mode="manual")
        with self.assertRaises(parser.ParseError):
            parser.parse("pin=1234\n" + self.serial, "run-pin")

    def test_s07_pre_reset_missing_harness_and_s14_cleanup(self):
        pre = "LAB11_HARNESS_DONE status=complete\nLAB11_READY build=lab11-fw-scripted-v1 mode=scripted fixture=lab11-send/v1 harness=1\n"
        post = "LAB11_READY build=lab11-fw-scripted-v1 mode=scripted fixture=lab11-send/v1 harness=1\n"
        rejected = classify_capture(pre_reset=pre, post_reset=post, parser_verdict="PASS", cleanup_ok=True)
        self.assertNotEqual(rejected["workflow_status"], "PASS")
        self.assertEqual(rejected["experiment_verdict"], "UNMEASURED")
        self.assertEqual(rejected["terminal_reason"], "missing_harness_marker")
        missing = classify_capture(pre_reset="", post_reset=post, parser_verdict="PASS", cleanup_ok=True)
        self.assertIn(missing["workflow_status"], {"FAIL", "INCOMPLETE"})
        cleaned = classify_capture(
            pre_reset="",
            post_reset=post + "LAB11_HARNESS_DONE status=complete\n",
            parser_verdict="PASS",
            cleanup_ok=False,
        )
        self.assertEqual(cleaned["workflow_status"], "FAIL")
        self.assertFalse(cleaned["cleanup_succeeded"])
        self.assertIn("experiment_verdict", cleaned)
        manual = classify_capture(
            pre_reset="",
            post_reset="LAB11_READY build=lab11-fw-manual-v1 mode=manual fixture=lab11-send/v1 harness=0\n",
            parser_verdict="UNMEASURED",
            cleanup_ok=True,
            mode="manual",
        )
        self.assertEqual(manual["workflow_status"], "PASS")

    def test_s21_scripted_variant_and_local_backend(self):
        spec = SimpleNamespace(variants={"manual": object(), "scripted": object()}, default_variant="manual")
        self.assertEqual(select_variant(spec, SimpleNamespace(variant=None), "scripted"), "scripted")
        self.assertEqual(DEADLINES_SECONDS, {
            "startup": 10, "health": 10, "seed": 10, "readiness": 15, "action": 5, "cleanup": 5,
        })
        self.assertEqual(PORT, 8770)
        adapter = Adapter()
        code = adapter.run(None, None, spec, SimpleNamespace(backend="local", variant=None, mode="scripted"),
                           mode="scripted")
        self.assertNotEqual(code, 0)
        self.assertNotEqual(adapter.last_workflow, "PASS")
        self.assertEqual(adapter.bound_ports, [])
        with self.assertRaises(ProjectConfigError):
            adapter.run(None, None, spec, SimpleNamespace(backend="managed-http", variant="nope", mode="manual"),
                        mode="manual")

    def test_s10_redact_and_s11_health_identity(self):
        raw = b"url=https://example.test/a password=hunter2 Bearer abcdefghijkl\n"
        value = redact_line(raw).decode("utf-8")
        self.assertIn("[REDACTED_URL]", value)
        self.assertIn("password=[REDACTED]", value)
        self.assertIn("Bearer [REDACTED]", value)
        self.assertNotIn("hunter2", value)
        identity = server_identity(ROOT)
        httpd = server.create_server("127.0.0.1", 0, "lab11-health-1")
        port = httpd.server_address[1]
        thread = threading.Thread(target=httpd.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        thread.start()
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=2) as response:
                health = json.loads(response.read().decode("utf-8"))
        finally:
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=2)
        self.assertEqual(health["run_id"], "lab11-health-1")
        self.assertEqual(health["fixture_sha256"], identity["fixture_sha256"])
        self.assertEqual(health["fixture_version"], "lab11-send/v1")
        self.assertEqual(health["server_version"], "lab11-server/v1")
        self.assertEqual(identity["revision"], "lab11-server/v1")
        with tempfile.TemporaryDirectory() as temporary:
            copied = Path(temporary) / "fixtures"
            shutil.copytree(FIXTURES, copied)
            target = copied / "restart-probe.json"
            target.write_bytes(target.read_bytes().replace(b"open", b"gone", 1))
            with self.assertRaises(server.IdentityError):
                server.verify_fixture_tree(copied)

    def test_owned_server_stops_and_readme_budget(self):
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        with tempfile.TemporaryDirectory() as temporary:
            owned = OwnedServer()
            owned.start(ROOT, "reap-11", Path(temporary) / "server.log", port)
            proc = owned.proc
            self.assertTrue(owned.stop())
            self.assertIsNone(owned.proc)
            self.assertIsNotNone(proc.poll())
        readme = (LAB / "README.md").read_text(encoding="utf-8")
        self.assertIn("ResourceBudget", readme)
        self.assertIn("provisional lab probe", readme)
        for field in ("max_live_nodes", "max_transient_bytes", "max_persistent_bytes",
                      "max_concurrent_operations", "max_stack_or_call_depth", "maximum_input_size"):
            self.assertIn(field, readme)
        self.assertIn("COM-OPEN-001", readme)

    def test_lab11_one_accept(self):
        metrics = parser.parse(self.serial, "run-one")["lab_metrics"]
        row = metrics["scenarios"]["D-drop-receipt"]["observation"]
        self.assertEqual(row["terminal"], "success")
        self.assertEqual(row["accepted"], 1)
        self.assertEqual(row["lookup_same"], 1)
        self.assertEqual(row["complete_posts"], 1)
        self.assertEqual(row["remote_rollback"], 0)

    def test_lab11_part_retry(self):
        samples = parser.parse(self.serial, "run-retry")["lab_metrics"]["scenarios"]["D-drop-ack"]["samples"]
        row = samples[-1]
        self.assertEqual(row["identity_conflict"], "1")
        self.assertGreaterEqual(int(row["retry_same_sequence"]), 1)
        self.assertEqual(row["durable_parts"], row["unique_sequences"])
        self.assertEqual(row["accepted"], "1")
        self.assertEqual(row["terminal"], "success")

    def test_lab11_progress_level(self):
        for line in self.serial.splitlines():
            if "event=scenario_terminal" not in line:
                continue
            self.assertIn("scene_generation=1", line)
            self.assertIn("progress_level=0", line)

    def test_lab11_terminal(self):
        seen = set()
        for line in self.serial.splitlines():
            if "event=scenario_terminal" not in line:
                continue
            fields = dict(part.split("=", 1) for part in line.split() if "=" in part)
            self.assertIn(fields["terminal"], {"success", "failure", "cancelled", "stale", "timed_out"})
            self.assertEqual(fields["terminal_count"], "1")
            self.assertNotIn(fields["operation_id"], seen)
            seen.add(fields["operation_id"])
            self.assertNotEqual(fields.get("phase"), "checking")

    def test_lab11_storage_full(self):
        samples = parser.parse(self.serial, "run-full")["lab_metrics"]["scenarios"]["D-full"]["samples"]
        self.assertEqual(len(samples), 1)
        row = samples[0]
        self.assertEqual(row["terminal"], "failure")
        self.assertEqual(row["reason"], "storage")
        self.assertEqual(row["terminal_count"], "1")
        self.assertEqual(row["live_buffers"], "0")
        self.assertEqual(row["sketch_len"], "0")
        self.assertEqual(row["pcm_len"], "0")
        self.assertEqual(row["accepted"], "0")
        self.assertEqual(row["work"], "discarded")

    def test_lab11_cancel_phases(self):
        samples = parser.parse(self.serial, "run-cancel")["lab_metrics"]["scenarios"]["D-cancel"]["samples"]
        phases = {row["cancel_phase"]: row for row in samples}
        self.assertEqual(set(phases), {"before_byte", "mid_upload", "after_accept", "during_lookup"})
        self.assertEqual(len({row["operation_id"] for row in samples}), 4)
        self.assertTrue(all(row["terminal_count"] == "1" for row in samples))
        self.assertEqual(phases["before_byte"]["terminal"], "cancelled")
        self.assertEqual(phases["before_byte"]["accepted"], "0")
        self.assertEqual(phases["mid_upload"]["terminal"], "cancelled")
        self.assertEqual(phases["mid_upload"]["accepted"], "0")
        self.assertEqual(phases["after_accept"]["terminal"], "success")
        self.assertEqual(phases["after_accept"]["accepted"], "1")
        self.assertEqual(phases["after_accept"]["remote_rollback"], "0")
        self.assertEqual(phases["during_lookup"]["terminal"], "cancelled")
        self.assertEqual(phases["during_lookup"]["accepted"], "1")
        self.assertEqual(phases["during_lookup"]["remote_rollback"], "0")

    def test_missing_decided_sample_is_not_pass(self):
        partial = "\n".join(line for line in self.serial.splitlines() if "scenario=D-full" not in line)
        result = parser.parse(partial, "run-partial")
        self.assertEqual(result["lab_metrics"]["experiment_verdict"], "INCOMPLETE")
        self.assertNotEqual(result["lab_metrics"]["experiment_verdict"], "PASS")
