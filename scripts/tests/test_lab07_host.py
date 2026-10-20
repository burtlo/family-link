"""LAB-07 parser, fixture identity, and adapter checks."""
from __future__ import annotations

import importlib.util
import json
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
    "parse_lab07_trace", ROOT / "client/labs/lab-07-readiness-roster/tools/parse_lab_trace.py")
parser = importlib.util.module_from_spec(_PARSER_SPEC)
sys.modules[_PARSER_SPEC.name] = parser
_PARSER_SPEC.loader.exec_module(parser)

_SERVER_SPEC = importlib.util.spec_from_file_location(
    "lab07_fixture_server", ROOT / "server/labs/lab-07-readiness-roster/server.py")
server = importlib.util.module_from_spec(_SERVER_SPEC)
sys.modules[_SERVER_SPEC.name] = server
_SERVER_SPEC.loader.exec_module(server)

from lab_adapter_lab07 import (  # noqa: E402
    DEADLINES_SECONDS,
    Adapter,
    OwnedServer,
    classify_capture,
    _failed,
    select_variant,
    server_identity,
)
from server_log import redact_line  # noqa: E402

LAB = ROOT / "client/labs/lab-07-readiness-roster"
FIXTURES = ROOT / "server/labs/lab-07-readiness-roster/fixtures"


def _emit() -> str:
    binary = Path(tempfile.mkdtemp(prefix="lab07-emit-")) / "emit.exe"
    compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    subprocess.run([
        compiler, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-I", str(LAB / "components/lab07_session/include"),
        str(LAB / "components/lab07_session/session.cpp"),
        str(LAB / "components/lab07_session/pollers.cpp"),
        str(LAB / "tests/model_test.cpp"),
        "-o", str(binary),
    ], check=True, cwd=ROOT)
    completed = subprocess.run([str(binary), "emit"], check=True, cwd=ROOT, capture_output=True, text=True)
    return completed.stdout


class Lab07HostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.serial = _emit()

    def test_s02_manifest_fields(self):
        project = json.loads((LAB / "manifest.lab.json").read_text(encoding="utf-8"))
        for key in ("document", "lab_id", "lab_revision", "firmware_path", "server_path",
                    "fixture_schema", "fixture_revision", "protocol", "trace_schema"):
            self.assertIn(key, project)
        self.assertEqual(project["document"], "family-link.lab-manifest/v1")
        self.assertEqual(project["lab_id"], "LAB-07")
        self.assertEqual(project["firmware_path"], "client/labs/lab-07-readiness-roster")
        self.assertEqual(project["server_path"], "server/labs/lab-07-readiness-roster")
        self.assertEqual(project["fixture_schema"], "lab07-health-roster/v1")
        self.assertEqual(project["fixture_revision"], "lab07-fixture/v1")
        self.assertEqual(project["trace_schema"], "lab07-trace/v1")
        trace = json.loads((LAB / "trace-schema.json").read_text(encoding="utf-8"))
        self.assertEqual(trace["schema"], project["trace_schema"])

    def test_s03_parser_document_and_run_id(self):
        result = parser.parse(self.serial, "run-lab07")
        self.assertEqual(result["document"], "family-link.lab-metrics/v2")
        self.assertEqual(result["run_id"], "run-lab07")
        self.assertEqual(result["schema_version"], "lab07-trace/v1")

    def test_lab07_experiment_inputs(self):
        result = parser.parse(self.serial, "run-lab07", mode="scripted")
        metrics = result["lab_metrics"]
        self.assertEqual(metrics["experiment_inputs"], [
            "R-wifi-absent", "R-invalid-200", "R-empty", "R-eight", "R-retry",
            "R-retry-fail", "R-idle", "R-stale-beat", "R-mute", "R-timeout-ui",
        ])
        self.assertEqual(metrics["experiment_verdict"], "PASS")
        self.assertEqual(metrics["performance_verdict"], "UNMEASURED")
        self.assertNotIn("R-wire", metrics["experiment_inputs"])
        self.assertNotIn("R-stale-clock", metrics["experiment_inputs"])
        self.assertEqual(metrics["scenarios"]["R-wire"]["class"], "exploratory")
        self.assertIn("parsed_keys", metrics["scenarios"]["R-wire"]["observation"])
        self.assertEqual(metrics["scenarios"]["R-stale-clock"]["observation"]["grace_ms"], 0)
        self.assertEqual(metrics["scenarios"]["R-stale-clock"]["observation"]["clear_stale"], 0)
        self.assertEqual(metrics["scenarios"]["R-eight"]["samples"][0]["user_count"], 8)
        self.assertEqual(metrics["scenarios"]["R-retry"]["samples"][0]["attempt"], 2)
        self.assertEqual(metrics["scenarios"]["R-mute"]["samples"][0]["terminal"], "stale")
        self.assertEqual(metrics["scenarios"]["R-timeout-ui"]["samples"][0]["touch_during_wait"], 1)
        self.assertNotIn("wrong_pin", metrics["scenarios"]["R-timeout-ui"]["samples"][0])
        workflow_pass = {"workflow_status": "PASS"}
        self.assertNotIn("experiment_verdict", workflow_pass)

    def test_s22_exploratory_rows_do_not_decide_the_verdict(self):
        failing = parser.parse(
            "LAB07_TRACE schema=lab07-trace/v1 event=scenario_terminal seq=1 monotonic_us=1 "
            "session_id=s-boot user_id=none operation_id=1 generation=1 scenario=R-wifi-absent "
            "class=decided terminal=failure terminal_count=1 attempt=1 stage=connecting user_count=0 "
            "selectable_cards=1 focus_id=none focus_stable=0 stale=0 missed_beats=0 inflight_requests=0 "
            "touch_during_wait=0 queue_count=0 roster_period_ms=60000 heartbeat_period_ms=60000 valid=1 "
            "stage_unchanged=0 parsed_keys=none grace_ms=0 clear_stale=0\n"
            "LAB07_TRACE schema=lab07-trace/v1 event=scenario_terminal seq=2 monotonic_us=2 "
            "session_id=s-boot user_id=none operation_id=2 generation=1 scenario=R-wire "
            "class=exploratory terminal=success terminal_count=1 attempt=1 stage=roster user_count=0 "
            "selectable_cards=0 focus_id=none focus_stable=0 stale=0 missed_beats=0 inflight_requests=0 "
            "touch_during_wait=0 queue_count=0 roster_period_ms=60000 heartbeat_period_ms=60000 valid=1 "
            "stage_unchanged=1 parsed_keys=run_id|fixture_version grace_ms=0 clear_stale=0\n",
            "run-fail",
        )
        self.assertEqual(failing["lab_metrics"]["experiment_verdict"], "FAIL")
        self.assertEqual(failing["lab_metrics"]["scenarios"]["R-wire"]["observation"]["terminal"], "success")
        text = self.serial.replace(
            "scenario=R-wire class=exploratory terminal=success",
            "scenario=R-wire class=exploratory terminal=failure",
            1,
        )
        passed = parser.parse(text, "run-pass")
        self.assertEqual(passed["lab_metrics"]["experiment_verdict"], "PASS")
        self.assertEqual(passed["lab_metrics"]["scenarios"]["R-wire"]["observation"]["terminal"], "failure")
        empty = parser.parse(self.serial, "run-empty", experiment_inputs=[])
        self.assertEqual(empty["lab_metrics"]["experiment_inputs"], [])
        self.assertEqual(empty["lab_metrics"]["experiment_verdict"], "UNMEASURED")

    def test_percentiles_and_empty_series(self):
        samples = list(range(1, 21))
        self.assertEqual(parser.nearest_rank(samples, 50), 10)
        self.assertEqual(parser.nearest_rank(samples, 95), 19)
        self.assertEqual(parser.nearest_rank(samples, 99), 20)
        series = parser._series([], "ms", "host")
        self.assertEqual(series["sample_count"], 0)
        self.assertEqual([series[key] for key in ("p50", "p95", "p99", "max")], [None, None, None, None])
        self.assertEqual(series["missing_reason"], "no eligible samples in trace")
        self.assertNotEqual(series["provenance"], "hardware")

    def test_s10_redact_line_and_pin_rejection(self):
        raw = b"url=https://example.test/a password=hunter2 Bearer abcdefghijkl\n"
        value = redact_line(raw).decode("utf-8")
        self.assertIn("[REDACTED_URL]", value)
        self.assertIn("password=[REDACTED]", value)
        self.assertIn("Bearer [REDACTED]", value)
        self.assertNotIn("hunter2", value)
        self.assertNotIn("abcdefghijkl", value)
        with self.assertRaises(parser.ParseError):
            parser.parse("pin=1234\n", "run-pin")

    def test_s13_host_provenance_is_not_hardware(self):
        serial = self.serial + (
            "LAB07_TRACE schema=lab07-trace/v1 event=resource_sample seq=1 monotonic_us=10 "
            "session_id=s-boot operation_id=0 generation=1 lvgl_high_water_bytes=100 "
            "lvgl_largest_free_block_bytes=200 pixel_high_water_bytes=30 "
            "pixel_largest_free_block_bytes=40\n"
        )
        result = parser.parse(serial, "run-host")
        self.assertEqual(result["evidence_provenance"], "host")
        measurement = result["lab_metrics"]["measurements"]["lvgl_largest_free_block_bytes"]
        self.assertEqual(measurement["provenance"], "host")
        self.assertEqual(measurement["max"], 200)
        self.assertNotEqual(measurement["provenance"], "hardware")

    def test_s07_pre_reset_and_missing_marker(self):
        pre = "LAB07_HARNESS_DONE status=complete\n"
        post = "LAB07_READY build=lab07-fw-v1-scripted mode=scripted fixture=lab07-health-roster/v1 harness=1\n"
        rejected = classify_capture(pre_reset=pre, post_reset=post, parser_verdict="PASS", cleanup_ok=True)
        self.assertNotEqual(rejected["workflow_status"], "PASS")
        self.assertEqual(rejected["experiment_verdict"], "UNMEASURED")
        missing = classify_capture(pre_reset="", post_reset=post, parser_verdict="PASS", cleanup_ok=True)
        self.assertIn(missing["workflow_status"], {"FAIL", "INCOMPLETE"})
        self.assertEqual(missing["experiment_verdict"], "UNMEASURED")
        self.assertEqual(missing["terminal_reason"], "missing_harness_marker")
        manual = classify_capture(
            pre_reset="", post_reset="LAB07_READY build=lab07-fw-v1-manual mode=manual fixture=lab07-health-roster/v1 harness=0\n",
            parser_verdict="UNMEASURED", cleanup_ok=True, mode="manual")
        self.assertEqual(manual["workflow_status"], "PASS")
        self.assertEqual(manual["experiment_verdict"], "UNMEASURED")
        skipped = _failed("ProjectConfigError", True)
        self.assertEqual(skipped["terminal_reason"], "ProjectConfigError")
        self.assertNotEqual(skipped["terminal_reason"], "missing_harness_marker")

    def test_s14_parser_success_with_failed_cleanup(self):
        post = "LAB07_READY build=lab07-fw-v1-scripted mode=scripted fixture=lab07-health-roster/v1 harness=1\nLAB07_HARNESS_DONE status=complete\n"
        result = classify_capture(pre_reset="", post_reset=post, parser_verdict="PASS", cleanup_ok=False)
        self.assertEqual(result["workflow_status"], "FAIL")
        self.assertFalse(result["cleanup_succeeded"])
        self.assertIn("workflow_status", result)
        self.assertIn("experiment_verdict", result)

    def test_s21_scripted_variant_and_local_backend(self):
        spec = SimpleNamespace(variants={"manual": object(), "scripted": object()}, default_variant="manual")
        self.assertEqual(select_variant(spec, SimpleNamespace(variant=None), "scripted"), "scripted")
        self.assertEqual(DEADLINES_SECONDS["startup"], 10)
        self.assertEqual(DEADLINES_SECONDS["health"], 10)
        self.assertEqual(DEADLINES_SECONDS["seed"], 10)
        self.assertEqual(DEADLINES_SECONDS["readiness"], 15)
        self.assertEqual(DEADLINES_SECONDS["action"], 5)
        self.assertEqual(DEADLINES_SECONDS["cleanup"], 5)
        adapter = Adapter()
        code = adapter.run(None, None, spec, SimpleNamespace(backend="local", variant=None, mode="scripted"),
                           mode="scripted")
        self.assertNotEqual(code, 0)
        self.assertNotEqual(adapter.last_workflow, "PASS")
        self.assertEqual(adapter.bound_ports, [])

    def test_cleanup_retries_once_and_does_not_orphan(self):
        class Late:
            def __init__(self):
                self.pid = 4242
                self.returncode = None
                self.kills = 0
                self.waits = 0

            def poll(self):
                return self.returncode

            def terminate(self):
                return None

            def kill(self):
                self.kills += 1

            def wait(self, timeout=None):
                self.waits += 1
                if self.waits < 2:
                    raise subprocess.TimeoutExpired("lab07", timeout or 1)
                self.returncode = 0
                return 0

        owned = OwnedServer()
        late = Late()
        owned.proc = late
        self.assertTrue(owned.stop())
        self.assertIsNone(owned.proc)
        self.assertEqual(late.kills, 1)
        self.assertEqual(late.waits, 2)

        class Stuck(Late):
            def wait(self, timeout=None):
                raise subprocess.TimeoutExpired("lab07", timeout or 1)

        owned = OwnedServer()
        stuck = Stuck()
        owned.proc = stuck
        self.assertFalse(owned.stop())
        self.assertIs(owned.proc, stuck)
        self.assertEqual(stuck.pid, 4242)

    def test_s11_health_identity_and_revision_bump(self):
        identity = server_identity(ROOT)
        httpd = server.create_server("127.0.0.1", 0, "lab07-health-1", 0)
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
        self.assertEqual(health["run_id"], "lab07-health-1")
        self.assertEqual(health["fixture_sha256"], identity["fixture_sha256"])
        self.assertEqual(health["fixture_version"], "lab07-health-roster/v1")
        self.assertEqual(health["server_version"], "lab07-server/v1")
        self.assertNotIn("user_id", health)
        self.assertEqual(identity["revision"], "lab07-server/v1")
        server_manifest = json.loads((ROOT / "server/labs/lab-07-readiness-roster/manifest.lab.json").read_text(encoding="utf-8"))
        self.assertEqual(identity["revision"], server_manifest["lab_revision"])
        with tempfile.TemporaryDirectory() as temporary:
            copied = Path(temporary) / "fixtures"
            shutil.copytree(FIXTURES, copied)
            target = copied / "roster-empty.json"
            target.write_bytes(target.read_bytes().replace(b"1", b"2", 1))
            with self.assertRaises(server.IdentityError):
                server.verify_fixture_tree(copied)

    def test_owned_server_reaps_child(self):
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        with tempfile.TemporaryDirectory() as temporary:
            owned = OwnedServer()
            owned.start(ROOT, "reap-1", Path(temporary) / "server.log", port, delay_ms=0)
            proc = owned.proc
            self.assertTrue(owned.stop())
            self.assertIsNone(owned.proc)
            self.assertIsNotNone(proc.poll())

    def test_s16_and_stage_owner_readme(self):
        readme = (LAB / "README.md").read_text(encoding="utf-8")
        self.assertIn("stage_owner=lab07-local", readme)
        self.assertIn("ResourceBudget", readme)
        for field in ("max_live_nodes", "max_transient_bytes", "max_persistent_bytes",
                      "max_concurrent_operations", "max_stack_or_call_depth", "maximum_input_size"):
            self.assertIn(field, readme)
        self.assertIn("provisional lab probe", readme)
        self.assertIn("capacity is 4", readme)
        self.assertIn("L1 virtual-clock only", readme)
        self.assertIn("65536", readme)

    def test_fixture_valid_flag_is_not_a_product_schema(self):
        body = json.loads((FIXTURES / "roster-invalid.json").read_text(encoding="utf-8"))
        self.assertEqual(body["valid"], 0)
        self.assertEqual(body["fixture_schema"] if "fixture_schema" in body else "lab07-health-roster/v1",
                         "lab07-health-roster/v1")
