"""LAB-08 parser, fixture server, and adapter checks."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

_PARSER_SPEC = importlib.util.spec_from_file_location(
    "parse_lab08_trace", ROOT / "client/labs/lab-08-pin-cooldown/tools/parse_lab_trace.py")
parser = importlib.util.module_from_spec(_PARSER_SPEC)
sys.modules[_PARSER_SPEC.name] = parser
_PARSER_SPEC.loader.exec_module(parser)

_SERVER_SPEC = importlib.util.spec_from_file_location(
    "lab08_fixture_server", ROOT / "server/labs/lab-08-pin-cooldown/server.py")
server = importlib.util.module_from_spec(_SERVER_SPEC)
sys.modules[_SERVER_SPEC.name] = server
_SERVER_SPEC.loader.exec_module(server)

from lab_adapter_lab08 import (  # noqa: E402
    DEADLINES_SECONDS,
    PORT,
    Adapter,
    classify_capture,
    select_variant,
    server_identity,
    _failed,
)
from project_config import ProjectConfigError  # noqa: E402

LAB = ROOT / "client/labs/lab-08-pin-cooldown"
FIXTURES = ROOT / "server/labs/lab-08-pin-cooldown/fixtures"
DECIDED = [
    "K-ok", "K-wrong", "K-five", "K-switch-user", "K-timeouts", "K-lost",
    "K-mute-local", "K-boot", "K-idle",
]
EXPLORATORY = ["K-concurrent", "K-mismatch", "K-mute-server"]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


def _bounded(text: str, token: str) -> bool:
    import re
    return re.search(rf"(?<![0-9a-fA-F]){re.escape(token)}(?![0-9a-fA-F])", text) is not None


class Lab08HostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab08-host-")
        cls.binary = Path(cls.temp.name) / "model_test.exe"
        includes = [
            ROOT / "client/labs/lab-07-readiness-roster/components/lab07_session/include",
            LAB / "components/lab08_pin/include",
        ]
        sources = [
            ROOT / "client/labs/lab-07-readiness-roster/components/lab07_session/session.cpp",
            ROOT / "client/labs/lab-07-readiness-roster/components/lab07_session/pin_session.cpp",
            ROOT / "client/labs/lab-07-readiness-roster/components/lab07_session/scoped_session.cpp",
            LAB / "components/lab08_pin/pin_http.cpp",
            LAB / "components/lab08_pin/pin_catalog.cpp",
            LAB / "tests/model_test.cpp",
        ]
        command = [find_cxx(), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        for include in includes:
            command.extend(["-I", str(include)])
        command.extend(str(path) for path in sources)
        command.extend(["-o", str(cls.binary)])
        subprocess.run(command, check=True, cwd=ROOT)
        cls.catalog = subprocess.run([str(cls.binary), "catalog"], check=True, cwd=ROOT,
                                     capture_output=True, text=True).stdout

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _serve(self, port: int, run_id: str, log_path: Path | None = None):
        httpd = server.create_server("127.0.0.1", port, run_id, str(log_path) if log_path else None)
        thread = threading.Thread(target=httpd.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        thread.start()
        return httpd, thread

    def _stop(self, httpd, thread) -> None:
        httpd.shutdown()
        httpd.server_close()
        thread.join(timeout=2)

    def _post(self, port: int, key: str, user: str, pin: str, drop: bool = False):
        payload = json.dumps({"key": key, "user": user, "pin": pin}).encode("utf-8")
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/pin", data=payload, method="POST",
            headers={"Content-Type": "application/json", **({"X-Lab-Drop": "1"} if drop else {})},
        )
        try:
            with urllib.request.urlopen(request, timeout=2) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()

    def test_s02_manifest_fields(self):
        manifest = json.loads((LAB / "manifest.lab.json").read_text(encoding="utf-8"))
        for field in ("document", "lab_id", "firmware_path", "server_path", "fixture_schema",
                      "fixture_revision", "protocol", "trace_schema"):
            self.assertIn(field, manifest)
        self.assertEqual(manifest["document"], "family-link.lab-manifest/v1")
        self.assertEqual(manifest["lab_id"], "LAB-08")
        self.assertEqual(manifest["trace_schema"], "lab08-trace/v1")
        self.assertNotEqual(manifest["server_path"], "none")

    def test_s03_and_lab08_experiment_inputs(self):
        metrics_path = Path(self.temp.name) / "metrics.json"
        serial_path = Path(self.temp.name) / "serial.txt"
        serial_path.write_text(self.catalog, encoding="utf-8")
        parsed = parser.parse_file(serial_path, metrics_path, expected_run_id="login-session-20261020")
        self.assertEqual(parsed["document"], "family-link.lab-metrics/v2")
        self.assertEqual(parsed["run_id"], "login-session-20261020")
        metrics = parsed["lab_metrics"]
        self.assertEqual(metrics["experiment_inputs"], DECIDED)
        self.assertEqual(metrics["experiment_verdict"], "PASS")
        self.assertEqual(metrics["performance_verdict"], "UNMEASURED")
        self.assertEqual(parsed["evidence_provenance"], "host")
        self.assertNotEqual(parsed["evidence_provenance"], "hardware")
        for scenario_id in DECIDED:
            self.assertEqual(metrics["scenarios"][scenario_id]["sample_count"], 1)
            self.assertEqual(metrics["scenarios"][scenario_id]["class"], "decided")
            self.assertEqual(metrics["scenarios"][scenario_id]["terminal_count"], 1)
        for scenario_id in EXPLORATORY:
            self.assertNotIn(scenario_id, metrics["experiment_inputs"])
            self.assertEqual(metrics["scenarios"][scenario_id]["class"], "exploratory")
            self.assertIn("terminal", metrics["scenarios"][scenario_id]["observation"])
            self.assertIn("side_effects", metrics["scenarios"][scenario_id]["observation"])
        callback = metrics["measurements"]["input_callback_us"]
        self.assertEqual(callback["sample_count"], 0)
        self.assertEqual(callback["provenance"], "unmeasured/blocked")
        self.assertIsNone(callback["p50"])
        workflow = classify_capture(
            pre_reset="",
            post_reset="LAB08_READY build=lab08-fw-v1-scripted mode=scripted fixture=lab08-pin/v1 harness=1\n"
                       "LAB08_HARNESS_DONE status=complete\n",
            parser_verdict="UNMEASURED", cleanup_ok=True)
        self.assertEqual(workflow["workflow_status"], "PASS")
        self.assertEqual(workflow["experiment_verdict"], "UNMEASURED")
        self.assertNotEqual(workflow["experiment_verdict"], "PASS")
        self.assertEqual(parsed["evidence"]["trace_loss"], "UNKNOWN")

    def test_trace_loss_row_and_input_callback_samples(self):
        zero = (
            "LAB08_TRACE schema=lab08-trace/v1 event=trace_loss "
            "dropped_events=0 oversized_events=0 malformed_rows=0\n"
            "LAB08_TRACE schema=lab08-trace/v1 event=input_callback elapsed_us=25\n"
            "LAB08_TRACE schema=lab08-trace/v1 event=resource_sample "
            "internal_largest_free_block_bytes=11 psram_largest_free_block_bytes=22 "
            "lvgl_largest_free_block_bytes=33\n"
        )
        parsed = parser.parse(zero, "run-zero", capture_context={"kind": "device_serial_capture"})
        self.assertEqual(parsed["evidence"]["trace_loss"], "NONE")
        self.assertEqual(parsed["evidence"]["firmware_trace_loss"]["dropped_events"], 0)
        self.assertEqual(parsed["lab_metrics"]["performance_verdict"], "UNMEASURED")
        self.assertEqual(parsed["lab_metrics"]["measurements"]["input_callback_us"]["max"], 25)
        self.assertEqual(parsed["lab_metrics"]["measurements"]["input_callback_us"]["provenance"], "hardware")
        observed = parser.parse(
            "LAB08_TRACE schema=lab08-trace/v1 event=trace_loss "
            "dropped_events=1 oversized_events=0 malformed_rows=0\n",
            "run-observed")
        self.assertEqual(observed["evidence"]["trace_loss"], "OBSERVED")

    def test_s22_exploratory_does_not_decide(self):
        failing = (
            "LAB08_TRACE schema=lab08-trace/v1 event=scenario_terminal seq=1 monotonic_us=0 "
            "session_id=s-boot user_id=su-01 operation_id=1 generation=1 key_generation=1 "
            "scenario=K-ok class=decided terminal=failure terminal_count=1 attempt=1 stage=roster "
            "digit_len=0 display_power=1 wrong_count=0 cooldown_remaining_ms=0 side_effects=1 "
            "delay_ms_0=0 delay_ms_1=0 delay_ms_2=0 attempts_used=1 boot_zero=0 boot_partial=0 "
            "boot_full=0 pin_copy=none reason=none key_fingerprint=" + ("ab" * 32) + " update_level=0"
        )
        exploratory = failing.replace("scenario=K-ok", "scenario=K-concurrent").replace(
            "terminal=failure", "terminal=success").replace("class=decided", "class=exploratory")
        failed = parser.parse(failing + "\n" + exploratory + "\n", "s22-fail")
        self.assertEqual(failed["lab_metrics"]["experiment_verdict"], "FAIL")
        self.assertEqual(failed["lab_metrics"]["scenarios"]["K-concurrent"]["class"], "exploratory")
        self.assertEqual(failed["lab_metrics"]["scenarios"]["K-concurrent"]["observation"]["terminal"], "success")
        self.assertNotIn("K-concurrent", failed["lab_metrics"]["experiment_inputs"])
        passed = parser.parse(self.catalog, "s22-pass")
        self.assertEqual(passed["lab_metrics"]["experiment_verdict"], "PASS")
        self.assertEqual(passed["lab_metrics"]["scenarios"]["K-mismatch"]["observation"]["terminal"], "failure")
        empty = parser.parse(self.catalog, "s22-empty", experiment_inputs=[])
        self.assertEqual(empty["lab_metrics"]["experiment_verdict"], "UNMEASURED")
        duplicated = parser.parse(self.catalog + "\n" + self.catalog, "s22-dup")
        self.assertEqual(duplicated["lab_metrics"]["experiment_verdict"], "FAIL")

    def test_s07_pre_reset_and_missing_marker(self):
        pre = "LAB08_HARNESS_DONE status=complete\n"
        post = "LAB08_READY build=lab08-fw-v1-scripted mode=scripted fixture=lab08-pin/v1 harness=1\n"
        rejected = classify_capture(pre_reset=pre, post_reset=post, parser_verdict="PASS", cleanup_ok=True)
        self.assertNotEqual(rejected["workflow_status"], "PASS")
        self.assertEqual(rejected["experiment_verdict"], "UNMEASURED")
        missing = classify_capture(pre_reset="", post_reset=post, parser_verdict="PASS", cleanup_ok=True)
        self.assertIn(missing["workflow_status"], {"FAIL", "INCOMPLETE"})
        self.assertEqual(missing["experiment_verdict"], "UNMEASURED")
        self.assertEqual(missing["terminal_reason"], "missing_harness_marker")
        manual = classify_capture(
            pre_reset="",
            post_reset="LAB08_READY build=lab08-fw-v1-manual mode=manual fixture=lab08-pin/v1 harness=0\n",
            parser_verdict="UNMEASURED", cleanup_ok=True, mode="manual")
        self.assertEqual(manual["workflow_status"], "PASS")
        self.assertEqual(manual["experiment_verdict"], "UNMEASURED")
        skipped = _failed("ProjectConfigError", True)
        self.assertEqual(skipped["terminal_reason"], "ProjectConfigError")
        self.assertNotEqual(skipped["terminal_reason"], "missing_harness_marker")

    def test_s10_redaction_and_pin_rejection(self):
        redacted = parser.redact_line("see https://example.test/pin password=secret Bearer abc.def")
        self.assertIn("[REDACTED_URL]", redacted)
        self.assertIn("password=[REDACTED]", redacted)
        self.assertIn("Bearer [REDACTED]", redacted)
        self.assertNotIn("secret", redacted)
        with self.assertRaises(parser.ParseError):
            parser.parse("pin=1234\n", "secret")
        for pin in ("1234", "5678", "9999", "1111"):
            self.assertFalse(_bounded(self.catalog, pin))
            self.assertNotIn(hashlib.sha256(pin.encode("ascii")).hexdigest(), self.catalog)
        evidence = ROOT / "docs/labs/evidence/lab-08"
        if evidence.exists():
            for path in evidence.rglob("*"):
                if path.is_file():
                    text = path.read_text(encoding="utf-8", errors="replace")
                    self.assertNotIn("password=", text)
                    self.assertNotIn("Bearer ", text)
                    for pin in ("1234", "5678", "9999", "1111"):
                        self.assertFalse(_bounded(text, pin))

    def test_s11_s14_health_and_cleanup_split(self):
        identity = server_identity(ROOT)
        httpd, thread = self._serve(0, "lab08-health-1")
        port = httpd.server_address[1]
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=2) as response:
                health = json.loads(response.read().decode("utf-8"))
        finally:
            self._stop(httpd, thread)
        self.assertEqual(health["run_id"], "lab08-health-1")
        self.assertEqual(health["fixture_sha256"], identity["fixture_sha256"])
        self.assertEqual(health["fixture_version"], "lab08-pin/v1")
        self.assertEqual(health["server_version"], "lab08-server/v1")
        self.assertEqual(identity["revision"], "lab08-server/v1")
        with tempfile.TemporaryDirectory() as temporary:
            copied = Path(temporary) / "fixtures"
            shutil.copytree(FIXTURES, copied)
            target = copied / "malformed.json"
            target.write_bytes(target.read_bytes().replace(b"{", b"[", 1))
            with self.assertRaises(server.IdentityError):
                server.verify_fixture_tree(copied)
        post = ("LAB08_READY build=lab08-fw-v1-scripted mode=scripted fixture=lab08-pin/v1 harness=1\n"
                "LAB08_HARNESS_DONE status=complete\n")
        failed_cleanup = classify_capture(pre_reset="", post_reset=post, parser_verdict="PASS", cleanup_ok=False)
        self.assertEqual(failed_cleanup["workflow_status"], "FAIL")
        self.assertFalse(failed_cleanup["cleanup_succeeded"])
        self.assertIn("experiment_verdict", failed_cleanup)

    def test_s12_not_applicable_because_server_exists(self):
        text = (ROOT / "config/labs.registry.yaml").read_text(encoding="utf-8")
        start = text.index("\n  lab08:\n")
        end = text.index("\n  lab06:\n", start)
        block = text[start:end]
        self.assertIn("adapter: lab08", block)
        self.assertNotIn("workflow:", block)
        manifest = json.loads((LAB / "manifest.lab.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["server_path"], "server/labs/lab-08-pin-cooldown")

    def test_s06_variant_sdkconfig_files(self):
        manual = (LAB / "sdkconfig.defaults").read_text(encoding="utf-8")
        scripted = (LAB / "sdkconfig.defaults.scripted").read_text(encoding="utf-8")
        registry = (ROOT / "config/labs.registry.yaml").read_text(encoding="utf-8")
        self.assertNotIn("CONFIG_LAB08_SCRIPTED", manual)
        self.assertIn("CONFIG_LAB08_SCRIPTED=y", scripted)
        self.assertIn("build/labs/lab-08-pin-cooldown-manual", registry)
        self.assertIn("build/labs/lab-08-pin-cooldown-scripted", registry)

    def test_s16_readme_budget(self):
        readme = (LAB / "README.md").read_text(encoding="utf-8")
        self.assertIn("stage_owner=lab07-session", readme)
        self.assertIn("ResourceBudget", readme)
        self.assertIn("provisional lab probe", readme)
        self.assertIn("capacity is 2", readme)
        self.assertIn("L1 virtual-clock only", readme)
        self.assertIn("65536", readme)
        for field in ("max_live_nodes", "max_transient_bytes", "max_persistent_bytes",
                      "max_concurrent_operations", "max_stack_or_call_depth", "maximum_input_size"):
            self.assertIn(field, readme)
        self.assertIn("checking...", readme)
        self.assertIn("wrong pin", readme)
        self.assertIn("Too many attempts", readme)

    def test_s21_scripted_variant_and_local_backend(self):
        spec = SimpleNamespace(variants={"manual": object(), "scripted": object()}, default_variant="manual")
        self.assertEqual(select_variant(spec, SimpleNamespace(variant=None), "scripted"), "scripted")
        self.assertEqual(DEADLINES_SECONDS["startup"], 10)
        self.assertEqual(DEADLINES_SECONDS["cleanup"], 5)
        adapter = Adapter()
        code = adapter.run(None, None, spec, SimpleNamespace(backend="local", variant=None, mode="scripted"),
                           mode="scripted")
        self.assertNotEqual(code, 0)
        self.assertNotEqual(adapter.last_workflow, "PASS")
        self.assertEqual(adapter.bound_ports, [])

    def test_registry_help(self):
        python = ROOT / ".venv/Scripts/python.exe"
        validate = subprocess.run([str(python), "scripts/lab_registry.py", "validate"], cwd=ROOT,
                                  capture_output=True, text=True)
        self.assertEqual(validate.returncode, 0, validate.stderr)
        help_run = subprocess.run([str(python), "scripts/lab_host.py", "help", "lab08"], cwd=ROOT,
                                  capture_output=True, text=True)
        self.assertEqual(help_run.returncode, 0, help_run.stderr)
        self.assertIn("LAB-08", help_run.stdout)
        self.assertIn("Manual", help_run.stdout)
        self.assertIn("Scripted", help_run.stdout)
        self.assertIn("90", help_run.stdout)

    def test_lab08_idempotent_and_live_body(self):
        accounts = json.loads((FIXTURES / "accounts.json").read_text(encoding="utf-8"))
        correct = next(item["pin"] for item in accounts["users"] if item["id"] == "su-01")
        other = next(item["pin"] for item in accounts["users"] if item["id"] == "su-02")
        key = hashlib.sha256(b"lab08-test-key").hexdigest()
        lost_key = hashlib.sha256(b"lab08-lost-key").hexdigest()
        mismatch_key = hashlib.sha256(b"lab08-mismatch-key").hexdigest()
        log_path = Path(self.temp.name) / "idempotent.log"
        log_text = ""
        httpd, thread = self._serve(0, "lab08-live", log_path)
        port = httpd.server_address[1]
        try:
            status, body = self._post(port, key, "su-01", correct)
            self.assertEqual(status, 200)
            self.assertEqual(json.loads(body)["outcome"], "ok")
            self.assertEqual(json.loads(body)["side_effects"], 1)
            again, replay = self._post(port, key, "su-01", correct)
            self.assertEqual(again, 200)
            self.assertEqual(replay, body)
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/observe", timeout=2) as response:
                observe = json.loads(response.read().decode("utf-8"))
            self.assertEqual(observe["side_effects"], 1)
            drop_status, drop_body = self._post(port, lost_key, "su-01", correct, drop=True)
            self.assertEqual(drop_status, 204)
            self.assertEqual(drop_body, b"")
            retry_status, retry_body = self._post(port, lost_key, "su-01", correct)
            self.assertEqual(retry_status, 200)
            self.assertEqual(json.loads(retry_body)["side_effects"], 1)
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/observe", timeout=2) as response:
                after_lost = json.loads(response.read().decode("utf-8"))
            self.assertEqual(after_lost["side_effects"], 2)
            mismatch_status, _mismatch_body = self._post(port, mismatch_key, "su-01", correct)
            self.assertEqual(mismatch_status, 200)
            changed, _changed_body = self._post(port, mismatch_key, "su-01", "0000")
            self.assertEqual(changed, 409)
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/observe", timeout=2) as response:
                after_mismatch = json.loads(response.read().decode("utf-8"))
            self.assertEqual(after_mismatch["side_effects"], 3)
            wrong_status, wrong_body = self._post(port, hashlib.sha256(b"lab08-wrong-key").hexdigest(), "su-02", "0000")
            self.assertEqual(wrong_status, 200)
            self.assertEqual(json.loads(wrong_body)["outcome"], "wrong")
            malformed = urllib.request.urlopen(f"http://127.0.0.1:{port}/malformed", timeout=2).read()
            ok_path = Path(self.temp.name) / "ok.json"
            wrong_path = Path(self.temp.name) / "wrong.json"
            bad_path = Path(self.temp.name) / "bad.json"
            ok_path.write_bytes(body)
            wrong_path.write_bytes(wrong_body)
            bad_path.write_bytes(malformed)
            ok_apply = subprocess.run([str(self.binary), "http-apply", "1", "200", str(ok_path)],
                                      check=True, cwd=ROOT, capture_output=True, text=True).stdout
            wrong_apply = subprocess.run([str(self.binary), "http-apply", "1", "200", str(wrong_path)],
                                         check=True, cwd=ROOT, capture_output=True, text=True).stdout
            bad_apply = subprocess.run([str(self.binary), "http-apply", "1", "200", str(bad_path)],
                                       check=True, cwd=ROOT, capture_output=True, text=True).stdout
            closed_apply = subprocess.run([str(self.binary), "http-apply", "0", "0", str(ok_path)],
                                          check=True, cwd=ROOT, capture_output=True, text=True).stdout
            if httpd.log is not None:
                httpd.log._handle.flush()
                log_text = log_path.read_text(encoding="utf-8", errors="replace")
                httpd.log.close()
                httpd.log = None
        finally:
            self._stop(httpd, thread)
        self.assertIn("authenticated=1", ok_apply)
        self.assertIn("terminal=success", ok_apply)
        self.assertIn("side_effects=1", ok_apply)
        self.assertIn("authenticated=0", wrong_apply)
        self.assertIn("terminal=failure", wrong_apply)
        self.assertNotIn("terminal=success", bad_apply)
        self.assertIn("authenticated=0", bad_apply)
        self.assertIn("authenticated=0", closed_apply)
        self.assertNotIn("terminal=success", closed_apply)
        for text in (ok_apply, wrong_apply, bad_apply, closed_apply, log_text):
            for secret in (correct, other, "0000"):
                self.assertFalse(_bounded(text, secret))
        del other

    def test_cleanup_releases_8768_when_free(self):
        probe = socket.socket()
        try:
            probe.bind(("127.0.0.1", PORT))
        except OSError:
            probe.close()
            self.skipTest("port 8768 is already in use")
        finally:
            probe.close()
        httpd, thread = self._serve(PORT, "lab08-port")
        self._stop(httpd, thread)
        rebound = socket.socket()
        rebound.bind(("127.0.0.1", PORT))
        rebound.close()

    def test_concurrent_observation_counts_one_side_effect(self):
        accounts = json.loads((FIXTURES / "accounts.json").read_text(encoding="utf-8"))
        correct = next(item["pin"] for item in accounts["users"] if item["id"] == "su-01")
        key = hashlib.sha256(b"lab08-concurrent-key").hexdigest()
        httpd, thread = self._serve(0, "lab08-concurrent")
        port = httpd.server_address[1]
        results = []

        def post() -> None:
            results.append(self._post(port, key, "su-01", correct))

        try:
            first = threading.Thread(target=post)
            second = threading.Thread(target=post)
            first.start()
            second.start()
            first.join(timeout=3)
            second.join(timeout=3)
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/observe", timeout=2) as response:
                observe = json.loads(response.read().decode("utf-8"))
        finally:
            self._stop(httpd, thread)
        self.assertEqual(len(results), 2)
        self.assertEqual(observe["side_effects"], 1)
        self.assertTrue(all(status == 200 for status, _body in results))
