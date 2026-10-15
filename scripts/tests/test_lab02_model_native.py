"""Compile and exercise the actual LAB-02 C page parser when IDF is installed."""

import configparser
import os
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-02-bounded-inbox"


class Lab02NativeModelTests(unittest.TestCase):
    def _compiler_inputs(self):
        cc = shutil.which("cc")
        config = configparser.ConfigParser()
        config.read(ROOT / "config/host.defaults.ini")
        idf = Path(os.environ.get("IDF_PATH", config.get("idf", "path"))).expanduser()
        cjson = idf / "components/json/cJSON"
        if not cc or not (cjson / "cJSON.c").exists():
            self.skipTest("host C compiler or ESP-IDF cJSON source unavailable")
        return cc, cjson

    def test_page_parse_and_replacement(self):
        cc, cjson = self._compiler_inputs()
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_page_test"
            subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(LAB / "components/lab02/include"), "-I", str(cjson),
                 str(LAB / "tests/test_page_merge_native.c"),
                 str(LAB / "components/lab02/inbox_model.c"), str(cjson / "cJSON.c"),
                 "-o", str(output)],
                check=True, capture_output=True, text=True,
            )
            subprocess.run([str(output)], check=True, capture_output=True, text=True)

    def test_trace_transport_native(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_trace_transport"
            stubs = LAB / "tests/trace_host_stubs"
            command = [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                       "-pthread", "-I", str(stubs), "-I", str(LAB / "components/lab02/include"),
                       str(LAB / "tests/test_trace_transport_native.c"),
                       str(LAB / "tests/trace_host_stubs/trace_host_stubs.c"),
                       str(LAB / "components/lab02/trace_emit.c"), "-o", str(output)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(output)], check=True, capture_output=True,
                                    text=True, timeout=5)
            trace_lines = [line for line in result.stdout.splitlines() if line.startswith("LAB_TRACE ")]
            self.assertGreaterEqual(len(trace_lines), 1)
            self.assertTrue(result.stdout.endswith("\n"))
            for line in trace_lines:
                self.assertNotIn("\r", line)
                self.assertEqual(line.count("LAB_TRACE "), 1)
                self.assertIn(" trace_schema=lab-trace/v1", line)
                self.assertNotIn("old_message", line)
                self.assertNotIn("old_extra", line)
                self.assertNotIn("mutated_", line)
            self.assertIn("event=after_reset", result.stdout)
            self.assertIn("event=whole_row", result.stdout)
            self.assertIn("msg=message_old", result.stdout)
            self.assertIn("session_id=7 revision=8 snap_id=9 op_id=10", result.stdout)
            self.assertIn("state=running outcome=success reason=completed", result.stdout)
            self.assertIn("elapsed_us=11", result.stdout)
            self.assertIn("page_receipt_id=receipt_old run_id=run_old", result.stdout)
            self.assertIn("bind_generation=12 card_slot=3", result.stdout)
            self.assertIn("publication_after_apply=true", result.stdout)
            self.assertIn("LAB_TRACE_LOSS status=OBSERVED", result.stdout)
            self.assertTrue(any("dropped_events=" in line and "dropped_events=0" not in line
                                for line in result.stdout.splitlines()
                                if line.startswith("LAB_TRACE_LOSS ")))
            self.assertIn("oversized_events=1", result.stdout)
            self.assertIn("TRACE_NATIVE_RESULT PASS", result.stdout)

    def test_terminal_admission_and_reconciliation(self):
        cc, cjson = self._compiler_inputs()
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_terminal_test"
            command = [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                       "-pthread", "-I", str(LAB / "tests/host_stubs"),
                       "-I", str(LAB / "components/lab02/include"), "-I", str(cjson),
                       str(LAB / "tests/test_terminal_native.c"), str(LAB / "tests/session_host_stubs.c"),
                       str(LAB / "components/lab02/lab02_session.c"), str(LAB / "components/lab02/inbox_model.c"),
                       str(LAB / "components/lab02/inbox_page_metadata.c"), str(cjson / "cJSON.c"), "-o", str(output)]
            artifact = os.environ.get("LAB02_TERMINAL_ARTIFACT_DIR")
            if artifact:
                Path(artifact).mkdir(parents=True, exist_ok=True)
                (Path(artifact) / "compile-command.json").write_text(json.dumps(command, indent=2))
            subprocess.run(command, check=True, capture_output=True, text=True)
            for scenario, terminal_count in (("focus-full", 2), ("focus-busy", 1), ("initial-saturation", 9), ("initial-timeout", 1), ("normal-saturation", 9),
                                             ("pending-mutation", 6), ("pending-session-reset", 4), ("newer-operation", 4), ("response-old-revision", 3), ("stale-session", 3), ("pending-boot-reset", 3), ("busy-reset", 2), ("terminal-contention-reset", 2)):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(output), scenario], capture_output=True, text=True, timeout=5)
                    if artifact:
                        (Path(artifact) / (scenario + ".log")).write_text(result.stdout + result.stderr)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    terminals = [line for line in result.stdout.splitlines() if "event=fetch_terminal " in line]
                    self.assertEqual(len(terminals), terminal_count, result.stdout)
                    identities = [(line.split("op_id=")[1].split()[0],
                                   line.split("session=")[1].split()[0]) for line in terminals]
                    self.assertEqual(len(identities), len(set(identities)), result.stdout)

    def test_http_worker_allocation_failure(self):
        cc, cjson = self._compiler_inputs()
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_http_startup"
            command = [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-DLAB02_REAL_HTTP_PROVIDER",
                       "-Wall", "-Wextra", "-Werror", "-pthread",
                       "-I", str(LAB / "tests/http_host_stubs"), "-I", str(LAB / "tests/host_stubs"),
                       "-I", str(LAB / "components/lab02/include"),
                       "-I", str(LAB / "components/lab02_platform/include"), "-I", str(cjson),
                       str(LAB / "tests/test_http_startup_native.c"), str(LAB / "tests/session_host_stubs.c"),
                       str(LAB / "components/lab02/page_provider_http.c"),
                       str(LAB / "components/lab02/lab02_session.c"), str(LAB / "components/lab02/inbox_model.c"),
                       str(LAB / "components/lab02/inbox_page_metadata.c"), str(cjson / "cJSON.c"), "-o", str(output)]
            artifact = os.environ.get("LAB02_HTTP_STARTUP_ARTIFACT_DIR")
            if artifact:
                Path(artifact).mkdir(parents=True, exist_ok=True)
                (Path(artifact) / "compile-command.json").write_text(json.dumps(command, indent=2))
            subprocess.run(command, check=True, capture_output=True, text=True)
            for scenario in ("boot-fail", "background-fail", "success"):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(output), scenario], capture_output=True, text=True, timeout=5)
                    if artifact:
                        (Path(artifact) / (scenario + ".log")).write_text(result.stdout + result.stderr)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stdout.count("event=fetch_terminal "), 2 if scenario == "success" else 1)
                    self.assertEqual(result.stdout.count("LAB02_HTTP_READY "), 1 if scenario == "success" else 0)
                    self.assertEqual(result.stdout.count("event=io_start_admitted "), 1)
                    self.assertEqual(result.stdout.count("event=io_start_terminal "), 1)
                    startup = next(line for line in result.stdout.splitlines() if "event=io_start_terminal " in line)
                    self.assertIn("outcome=" + ("success" if scenario == "success" else "failure"), startup)

    def test_owner_retry_policy(self):
        cc, cjson = self._compiler_inputs()
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_retry_policy"
            subprocess.run(
                [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                 "-pthread", "-I", str(LAB / "tests/host_stubs"),
                 "-I", str(LAB / "components/lab02/include"), "-I", str(cjson),
                 str(LAB / "tests/test_retry_policy_native.c"),
                 str(LAB / "tests/session_host_stubs.c"),
                 str(LAB / "components/lab02/lab02_session.c"),
                 str(LAB / "components/lab02/inbox_model.c"),
                 str(LAB / "components/lab02/inbox_page_metadata.c"),
                 str(cjson / "cJSON.c"), "-o", str(output)],
                check=True, capture_output=True, text=True,
            )
            result = subprocess.run([str(output)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("RETRY_POLICY_RESULT PASS", result.stdout)

    def test_session_result_ownership_and_snap_deferral(self):
        cc, cjson = self._compiler_inputs()
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_session_test"
            subprocess.run(
                [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                 "-pthread", "-I", str(LAB / "tests/host_stubs"),
                 "-I", str(LAB / "components/lab02/include"), "-I", str(cjson),
                 str(LAB / "tests/test_session_native.c"),
                 str(LAB / "tests/session_host_stubs.c"),
                 str(LAB / "components/lab02/lab02_session.c"),
                 str(LAB / "components/lab02/inbox_model.c"),
                 str(LAB / "components/lab02/inbox_page_metadata.c"),
                 str(cjson / "cJSON.c"), "-o", str(output)],
                check=True, capture_output=True, text=True,
            )
            subprocess.run([str(output)], check=True, capture_output=True, text=True,
                           timeout=10)
