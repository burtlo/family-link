"""LAB-09 parser, fixture server, and adapter checks."""
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
import urllib.error
import urllib.request
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

_PARSER_SPEC = importlib.util.spec_from_file_location(
    "parse_lab09_trace", ROOT / "client/labs/lab-09-session-isolation/tools/parse_lab_trace.py")
parser = importlib.util.module_from_spec(_PARSER_SPEC)
sys.modules[_PARSER_SPEC.name] = parser
_PARSER_SPEC.loader.exec_module(parser)

_SERVER_SPEC = importlib.util.spec_from_file_location(
    "lab09_fixture_server", ROOT / "server/labs/lab-09-session-isolation/server.py")
server = importlib.util.module_from_spec(_SERVER_SPEC)
sys.modules[_SERVER_SPEC.name] = server
_SERVER_SPEC.loader.exec_module(server)

from lab_adapter_lab09 import (  # noqa: E402
    DEADLINES_SECONDS,
    PORT,
    Adapter,
    classify_capture,
    select_variant,
    server_identity,
    _failed,
)
from project_config import ProjectConfigError  # noqa: E402

LAB = ROOT / "client/labs/lab-09-session-isolation"
LAB07 = ROOT / "client/labs/lab-07-readiness-roster"
FIXTURES = ROOT / "server/labs/lab-09-session-isolation/fixtures"
DECIDED = ["I-mute", "I-signout", "I-switch", "I-push", "I-expire-client"]
EXPLORATORY = ["I-confused", "I-revoke-branches", "I-expire-http"]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class Lab09HostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab09-host-")
        cls.binary = Path(cls.temp.name) / "model_test.exe"
        includes = [
            LAB07 / "components/lab07_session/include",
            LAB / "components/lab09_probe/include",
        ]
        sources = [
            LAB07 / "components/lab07_session/session.cpp",
            LAB07 / "components/lab07_session/pin_session.cpp",
            LAB07 / "components/lab07_session/scoped_session.cpp",
            LAB / "components/lab09_probe/probe.cpp",
            LAB / "tests/model_test.cpp",
        ]
        command = [find_cxx(), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        for include in includes:
            command.extend(["-I", str(include)])
        command.extend(str(path) for path in sources)
        command.extend(["-o", str(cls.binary)])
        subprocess.run(command, check=True, cwd=ROOT)

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
        thread.join(timeout=2)
        httpd.server_close()
        if httpd.log is not None:
            httpd.log.close()
            httpd.log = None

    def _request(self, port: int, method: str, path: str, body: bytes | None = None, handle: str | None = None):
        request = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=body, method=method)
        if handle:
            request.add_header("Authorization", f"Bearer {handle}")
        if body is not None:
            request.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(request, timeout=2) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()

    def _sign_in(self, port: int, user: str) -> str:
        status, body = self._request(port, "POST", "/session/sign-in", json.dumps({"user": user}).encode("utf-8"))
        self.assertEqual(status, 200, body)
        payload = json.loads(body.decode("utf-8"))
        self.assertEqual(payload["actor"], user)
        self.assertTrue(payload["ok"])
        return str(payload["handle"])

    def test_s02_manifest_fields(self):
        manifest = json.loads((LAB / "manifest.lab.json").read_text(encoding="utf-8"))
        for field in ("document", "lab_id", "firmware_path", "server_path", "fixture_schema",
                      "fixture_revision", "protocol", "trace_schema"):
            self.assertIn(field, manifest)
        self.assertEqual(manifest["document"], "family-link.lab-manifest/v1")
        self.assertEqual(manifest["lab_id"], "LAB-09")
        self.assertEqual(manifest["trace_schema"], "lab09-trace/v1")
        self.assertEqual(manifest["fixture_schema"], "lab09-session/v1")
        self.assertNotEqual(manifest["server_path"], "none")

    def test_lab09_actor_observation_and_experiment_inputs(self):
        work = Path(self.temp.name) / "live"
        work.mkdir()
        log_path = Path(self.temp.name) / "server.log"
        httpd, thread = self._serve(0, "lab09-live", log_path)
        port = httpd.server_address[1]
        handle = ""
        try:
            status, sign_body = self._request(port, "POST", "/session/sign-in", b'{"user":"user-a"}')
            self.assertEqual(status, 200)
            sign_path = work / "sign-in.body"
            sign_path.write_bytes(sign_body)
            (work / "sign-in.status").write_text(f"{status}\n", encoding="utf-8")
            checked = subprocess.run(
                [str(self.binary), "sign-in-check", "1", "200", str(sign_path), "user-a"],
                check=True, cwd=ROOT, capture_output=True, text=True)
            self.assertIn("accepted=1", checked.stdout)
            self.assertNotIn("accepted=0", checked.stdout)
            missing = subprocess.run(
                [str(self.binary), "sign-in-check", "0", "0", str(sign_path), "user-a"],
                check=True, cwd=ROOT, capture_output=True, text=True)
            self.assertIn("accepted=0", missing.stdout)
            malformed_path = work / "malformed.body"
            malformed_path.write_bytes(urllib.request.urlopen(f"http://127.0.0.1:{port}/malformed", timeout=2).read())
            malformed = subprocess.run(
                [str(self.binary), "sign-in-check", "1", "200", str(malformed_path), "user-a"],
                check=True, cwd=ROOT, capture_output=True, text=True)
            self.assertIn("accepted=0", malformed.stdout)
            handle = json.loads(sign_body.decode("utf-8"))["handle"]
            saved = {
                "mute-inbox": ("/inbox", b"{}"),
                "mute-playback": ("/playback", b"{}"),
                "mute-profile": ("/profile", b"{}"),
                "signout": ("/profile", b"{}"),
                "push": ("/inbox", b"{}"),
            }
            for name, (path, body) in saved.items():
                op_status, op_body = self._request(port, "POST", path, body, handle)
                self.assertEqual(op_status, 200, name)
                (work / f"{name}.status").write_text(f"{op_status}\n", encoding="utf-8")
                (work / f"{name}.body").write_bytes(op_body)
            media_status, media_body = self._request(port, "POST", "/media", b"{}", handle)
            self.assertEqual(media_status, 200)
            self.assertEqual(len(media_body), 28800)
            (work / "mute-media.status").write_text("200\n", encoding="utf-8")
            (work / "mute-media.body").write_bytes(media_body)
            (work / "switch.status").write_text("200\n", encoding="utf-8")
            (work / "switch.body").write_bytes(media_body)
            confused_status, _confused_body = self._request(
                port, "POST", "/profile", b'{"user_id":"user-b"}', handle)
            self.assertEqual(confused_status, 200)
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/observe", timeout=2) as response:
                observed = json.loads(response.read().decode("utf-8"))
            b_count = int(observed["counts"]["user-b"])
            (work / "confused.txt").write_text(f"{b_count}\n", encoding="utf-8")
            expire_handle = self._sign_in(port, "user-a")
            expire_status, expire_body = self._request(port, "POST", "/session/expire", b"{}", expire_handle)
            (work / "expire.status").write_text(f"{expire_status}\n", encoding="utf-8")
            (work / "expire.body").write_bytes(expire_body)
            revoke_handle = self._sign_in(port, "user-a")
            self._request(port, "POST", "/session/mute", b'{"mode":"revoke-on-mute"}', revoke_handle)
            follow_revoke, _body = self._request(port, "POST", "/inbox", b"{}", revoke_handle)
            (work / "revoke.txt").write_text("1\n" if follow_revoke == 401 else "0\n", encoding="utf-8")
            leave_handle = self._sign_in(port, "user-a")
            self._request(port, "POST", "/session/mute", b'{"mode":"leave-server-session"}', leave_handle)
            follow_leave, _body = self._request(port, "POST", "/inbox", b"{}", leave_handle)
            (work / "leave.txt").write_text("0\n" if follow_leave == 200 else "1\n", encoding="utf-8")
            if httpd.log is not None:
                httpd.log._handle.flush()
        finally:
            self._stop(httpd, thread)
        catalog = subprocess.run([str(self.binary), "live-catalog", str(work)], check=True, cwd=ROOT,
                                 capture_output=True, text=True)
        self.assertNotIn(handle, catalog.stdout)
        self.assertNotIn(handle, catalog.stderr)
        metrics_path = work / "metrics.json"
        serial_path = work / "serial.txt"
        serial_path.write_text(catalog.stdout, encoding="utf-8")
        parsed = parser.parse_file(serial_path, metrics_path, expected_run_id="login-session-20261020")
        metrics = parsed["lab_metrics"]
        self.assertEqual(parsed["document"], "family-link.lab-metrics/v2")
        self.assertEqual(parsed["run_id"], "login-session-20261020")
        self.assertEqual(metrics["experiment_inputs"], DECIDED)
        self.assertEqual(metrics["experiment_verdict"], "PASS")
        self.assertEqual(metrics["performance_verdict"], "UNMEASURED")
        self.assertEqual(parsed["evidence_provenance"], "host")
        self.assertNotEqual(parsed["evidence_provenance"], "hardware")
        for scenario_id in DECIDED:
            self.assertEqual(metrics["scenarios"][scenario_id]["class"], "decided")
            self.assertGreaterEqual(metrics["scenarios"][scenario_id]["sample_count"], 1)
        mute_rows = metrics["scenarios"]["I-mute"]["samples"]
        self.assertEqual(len(mute_rows), 4)
        for row in mute_rows:
            self.assertEqual(row["terminal_count"], 1)
            self.assertIn(row["terminal"], {"stale", "cancelled"})
            self.assertEqual(row["applied"], 0)
            self.assertEqual(row["image_bytes"], 0)
        confused = metrics["scenarios"]["I-confused"]
        self.assertEqual(confused["class"], "exploratory")
        self.assertIsInstance(confused["observation"]["b_side_effects"], int)
        self.assertEqual(confused["observation"]["b_side_effects"], b_count)
        self.assertNotIn("I-confused", metrics["experiment_inputs"])
        modes = metrics["scenarios"]["I-revoke-branches"]["observation"]["modes"]
        self.assertIn("revoke-on-mute", modes)
        self.assertIn("leave-server-session", modes)
        self.assertIsInstance(metrics["scenarios"]["I-expire-http"]["observation"]["http_status"], int)
        self.assertEqual(metrics["scenarios"]["I-expire-http"]["observation"]["http_status"], expire_status)
        for scenario_id in EXPLORATORY:
            self.assertNotIn(scenario_id, metrics["experiment_inputs"])
            self.assertEqual(metrics["scenarios"][scenario_id]["class"], "exploratory")
            self.assertIn("terminal", metrics["scenarios"][scenario_id]["observation"])
        cancel = metrics["measurements"]["mute_cancel_us"]
        self.assertEqual(cancel["sample_count"], 0)
        self.assertEqual(cancel["provenance"], "unmeasured/blocked")
        self.assertIsNone(cancel["p50"])
        workflow = classify_capture(
            pre_reset="",
            post_reset="LAB09_READY build=lab09-fw-v1-scripted mode=scripted fixture=lab09-session/v1 harness=1\n"
                       "LAB09_HARNESS_DONE status=complete\n",
            parser_verdict="UNMEASURED", cleanup_ok=True)
        self.assertEqual(workflow["workflow_status"], "PASS")
        self.assertEqual(workflow["experiment_verdict"], "UNMEASURED")
        self.assertNotEqual(metrics["experiment_verdict"], workflow["experiment_verdict"])
        log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
        self.assertNotIn(handle, log_text)
        self.assertNotIn("Bearer ", log_text)
        sign_path.unlink()

    def test_s22_exploratory_does_not_decide(self):
        failing = (
            "LAB09_TRACE schema=lab09-trace/v1 event=scenario_terminal seq=1 monotonic_us=0 "
            "session_id=s1 user_id=user-a operation_id=1 generation=1 session_generation=2 "
            "scenario=I-signout class=decided kind=profile terminal=success terminal_count=1 stage=signed_out "
            "applied=1 image_bytes=0 card_count=0 duplicate_cards=0 display_power=1 http_status=200 "
            "b_side_effects=0 stage_restored=0 reason=ok update_level=0"
        )
        exploratory = (
            "LAB09_TRACE schema=lab09-trace/v1 event=observation scenario=I-confused class=exploratory "
            "terminal=recorded mode=none http_status=0 b_side_effects=0 revoked=0"
        )
        failed = parser.parse(failing + "\n" + exploratory + "\n", "s22-fail")
        self.assertEqual(failed["lab_metrics"]["experiment_verdict"], "FAIL")
        self.assertEqual(failed["lab_metrics"]["scenarios"]["I-confused"]["class"], "exploratory")
        self.assertEqual(failed["lab_metrics"]["scenarios"]["I-confused"]["observation"]["terminal"], "recorded")
        self.assertNotIn("I-confused", failed["lab_metrics"]["experiment_inputs"])
        empty = parser.parse(exploratory + "\n", "s22-empty", experiment_inputs=[])
        self.assertEqual(empty["lab_metrics"]["experiment_verdict"], "UNMEASURED")
        only_exploratory = parser.parse(exploratory + "\n", "s22-explore")
        self.assertNotEqual(only_exploratory["lab_metrics"]["experiment_verdict"], "PASS")

    def test_s07_pre_reset_and_missing_marker(self):
        pre = "LAB09_HARNESS_DONE status=complete\n"
        post = "LAB09_READY build=lab09-fw-v1-scripted mode=scripted fixture=lab09-session/v1 harness=1\n"
        rejected = classify_capture(pre_reset=pre, post_reset=post, parser_verdict="PASS", cleanup_ok=True)
        self.assertNotEqual(rejected["workflow_status"], "PASS")
        self.assertEqual(rejected["experiment_verdict"], "UNMEASURED")
        missing = classify_capture(pre_reset="", post_reset=post, parser_verdict="PASS", cleanup_ok=True)
        self.assertIn(missing["workflow_status"], {"FAIL", "INCOMPLETE"})
        self.assertEqual(missing["experiment_verdict"], "UNMEASURED")
        self.assertEqual(missing["terminal_reason"], "missing_harness_marker")

    def test_s10_and_l09_06_redaction(self):
        redacted = parser.redact_line("see https://fixture.example/inbox password=secret Bearer abc.def")
        self.assertIn("[REDACTED_URL]", redacted)
        self.assertIn("password=[REDACTED]", redacted)
        self.assertIn("Bearer [REDACTED]", redacted)
        self.assertNotIn("secret", redacted)
        with self.assertRaises(parser.ParseError):
            parser.parse("Authorization: Bearer raw-credential-value\n", "secret")
        sample = ROOT / "docs/labs/evidence/lab-09/samples/server.log.redacted"
        sample.parent.mkdir(parents=True, exist_ok=True)
        sample.write_text(redacted + "\nhealth status=200\nsign-in status=200 user=user-a\n", encoding="utf-8")
        for path in list(FIXTURES.rglob("*")) + [sample]:
            if path.is_file():
                text = path.read_text(encoding="utf-8", errors="replace")
                self.assertNotRegex(text, r"Bearer\s+[A-Za-z0-9._~+/=-]{8,}")
                self.assertNotRegex(text, r"password=(?!\[REDACTED\])")
                self.assertNotIn("\"handle\":", text)

    def test_s11_s14_health_cleanup_and_port(self):
        identity = server_identity(ROOT)
        httpd, thread = self._serve(0, "lab09-health-1")
        port = httpd.server_address[1]
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=2) as response:
                health = json.loads(response.read().decode("utf-8"))
        finally:
            self._stop(httpd, thread)
        self.assertEqual(health["run_id"], "lab09-health-1")
        self.assertEqual(health["fixture_sha256"], identity["fixture_sha256"])
        self.assertEqual(health["fixture_version"], "lab09-session/v1")
        self.assertEqual(health["server_version"], "lab09-server/v1")
        self.assertEqual(identity["revision"], "lab09-server/v1")
        with tempfile.TemporaryDirectory() as temporary:
            copied = Path(temporary) / "fixtures"
            shutil.copytree(FIXTURES, copied)
            target = copied / "malformed.json"
            target.write_bytes(target.read_bytes().replace(b"{", b"[", 1))
            with self.assertRaises(server.IdentityError):
                server.verify_fixture_tree(copied)
        post = ("LAB09_READY build=lab09-fw-v1-scripted mode=scripted fixture=lab09-session/v1 harness=1\n"
                "LAB09_HARNESS_DONE status=complete\n")
        failed_cleanup = classify_capture(pre_reset="", post_reset=post, parser_verdict="PASS", cleanup_ok=False)
        self.assertEqual(failed_cleanup["workflow_status"], "FAIL")
        self.assertFalse(failed_cleanup["cleanup_succeeded"])
        self.assertIn("experiment_verdict", failed_cleanup)
        probe = socket.socket()
        try:
            probe.bind(("127.0.0.1", PORT))
        except OSError:
            probe.close()
            self.skipTest("port 8769 is already in use")
        finally:
            probe.close()
        held, held_thread = self._serve(PORT, "lab09-port")
        self._stop(held, held_thread)
        rebound = socket.socket()
        rebound.bind(("127.0.0.1", PORT))
        rebound.close()

    def test_s12_registry_has_no_workflow_key(self):
        text = (ROOT / "config/labs.registry.yaml").read_text(encoding="utf-8")
        start = text.index("\n  lab09:\n")
        end = text.index("\n  lab06:\n", start)
        block = text[start:end]
        self.assertIn("adapter: lab09", block)
        self.assertNotIn("workflow:", block)
        self.assertNotIn("\n  lab07:\n", block)
        self.assertNotIn("\n  lab08:\n", block)

    def test_s06_variant_dirs(self):
        manual = (LAB / "sdkconfig.defaults").read_text(encoding="utf-8")
        scripted = (LAB / "sdkconfig.defaults.scripted").read_text(encoding="utf-8")
        registry = (ROOT / "config/labs.registry.yaml").read_text(encoding="utf-8")
        self.assertNotIn("CONFIG_LAB09_SCRIPTED", manual)
        self.assertIn("CONFIG_LAB09_SCRIPTED=y", scripted)
        self.assertIn("build/labs/lab-09-session-isolation-manual", registry)
        self.assertIn("build/labs/lab-09-session-isolation-scripted", registry)

    def test_s16_readme_budget(self):
        readme = (LAB / "README.md").read_text(encoding="utf-8")
        self.assertIn("stage_owner=lab07-session", readme)
        self.assertIn("ResourceBudget", readme)
        self.assertIn("provisional lab probe", readme)
        self.assertIn("capacity is 8", readme)
        self.assertIn("28800", readme)
        self.assertIn("COM-OPEN-001: OPEN", readme)
        self.assertIn("COM-OPEN-003: OPEN", readme)
        for field in ("max_live_nodes", "max_transient_bytes", "max_persistent_bytes",
                      "max_concurrent_operations", "max_stack_or_call_depth", "maximum_input_size"):
            self.assertIn(field, readme)

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

    def test_trace_loss_and_cancel_samples_stay_out_of_the_experiment_verdict(self):
        absent = parser.parse("LAB09_READY\n", "run-absent")
        self.assertEqual(absent["evidence"]["trace_loss"], "UNKNOWN")
        self.assertIsNone(absent["evidence"]["firmware_trace_loss"]["dropped_events"])
        zero = (
            "LAB09_TRACE schema=lab09-trace/v1 event=trace_loss "
            "dropped_events=0 oversized_events=0 malformed_rows=0\n"
            "LAB09_TRACE schema=lab09-trace/v1 event=cancel_sample elapsed_us=40\n"
            "LAB09_TRACE schema=lab09-trace/v1 event=resource_sample "
            "internal_largest_free_block_bytes=10 psram_largest_free_block_bytes=20 "
            "lvgl_largest_free_block_bytes=30\n"
        )
        parsed = parser.parse(zero, "run-zero", capture_context={"kind": "device_serial_capture"})
        self.assertEqual(parsed["evidence"]["trace_loss"], "NONE")
        self.assertEqual(parsed["evidence"]["firmware_trace_loss"]["status"], "NONE")
        self.assertEqual(parsed["evidence"]["firmware_trace_loss"]["dropped_events"], 0)
        self.assertEqual(parsed["lab_metrics"]["performance_verdict"], "UNMEASURED")
        self.assertEqual(parsed["lab_metrics"]["measurements"]["mute_cancel_us"]["p50"], 40)
        self.assertEqual(parsed["lab_metrics"]["measurements"]["mute_cancel_us"]["provenance"], "hardware")
        observed = parser.parse(
            "LAB09_TRACE schema=lab09-trace/v1 event=trace_loss "
            "dropped_events=2 oversized_events=0 malformed_rows=1\n",
            "run-observed")
        self.assertEqual(observed["evidence"]["trace_loss"], "OBSERVED")
        self.assertEqual(observed["evidence"]["firmware_trace_loss"]["dropped_events"], 2)
        with self.assertRaises(parser.ParseError):
            parser.parse(zero + zero, "run-duplicate")

    def test_registry_help(self):
        python = ROOT / ".venv/Scripts/python.exe"
        validate = subprocess.run([str(python), "scripts/lab_registry.py", "validate"], cwd=ROOT,
                                  capture_output=True, text=True)
        self.assertEqual(validate.returncode, 0, validate.stderr)
        help_run = subprocess.run([str(python), "scripts/lab_host.py", "help", "lab09"], cwd=ROOT,
                                  capture_output=True, text=True)
        self.assertEqual(help_run.returncode, 0, help_run.stderr)
        self.assertIn("LAB-09", help_run.stdout)
        self.assertIn("90", help_run.stdout)
