"""Compile and run the LAB-07 production session model."""
import importlib.util
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-07-readiness-roster"
INCLUDE = LAB / "components/lab07_session/include"
SOURCES = [
    LAB / "components/lab07_session/session.cpp",
    LAB / "components/lab07_session/pollers.cpp",
    LAB / "tests/model_test.cpp",
]
STAGE_DEFINITION = re.compile(
    r'(?:constexpr|const)\s+char[^\n]*"(wifi_unavailable|connecting|roster|empty_hangout|muted)"'
)


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class Lab07ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab07-model-")
        cls.binary = Path(cls.temp.name) / "model_test.exe"
        subprocess.run([
            find_cxx(), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(INCLUDE), *[str(path) for path in SOURCES], "-o", str(cls.binary),
        ], check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _run(self, name: str) -> None:
        subprocess.run([str(self.binary), name], check=True, cwd=ROOT)

    def test_lab07_cadence(self):
        self._run("cadence")

    def test_lab07_timeout(self):
        self._run("timeout")

    def test_lab07_one_retry(self):
        self._run("retry")
        self._run("retry-fail")

    def test_lab07_invalid_not_ready(self):
        self._run("invalid")
        self._run("invalid-json")

    def test_lab07_mute_stale(self):
        self._run("mute-stale")

    def test_lab07_mute_release(self):
        self._run("mute-release")

    def test_lab07_stage_enum(self):
        self._run("stage")
        header = (INCLUDE / "stages.hpp").resolve()
        for path in (LAB / "components").rglob("*"):
            if path.suffix not in {".hpp", ".h", ".cpp", ".c"} or path.resolve() == header:
                continue
            text = path.read_text(encoding="utf-8")
            self.assertIsNone(STAGE_DEFINITION.search(text), f"{path} also defines a stage token")
        for path in (LAB / "main").rglob("*"):
            if path.suffix not in {".hpp", ".h", ".cpp", ".c"}:
                continue
            text = path.read_text(encoding="utf-8")
            self.assertIsNone(STAGE_DEFINITION.search(text), f"{path} also defines a stage token")

    def test_lab07_stale_flag(self):
        self._run("stale")

    def test_lab07_queue_focus_and_single_terminal(self):
        self._run("roster")
        self._run("terminal")

    def test_lab07_manual_control_traces(self):
        parser_spec = importlib.util.spec_from_file_location(
            "parse_lab07_trace_manual",
            ROOT / "client/labs/lab-07-readiness-roster/tools/parse_lab_trace.py")
        parser = importlib.util.module_from_spec(parser_spec)
        parser_spec.loader.exec_module(parser)
        completed = subprocess.run(
            [str(self.binary), "manual-traces"],
            check=False, cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        decided = [
            "R-wifi-absent", "R-invalid-200", "R-empty", "R-eight", "R-retry",
            "R-retry-fail", "R-idle", "R-stale-beat", "R-mute", "R-timeout-ui",
        ]
        result = parser.parse(completed.stdout, "manual-traces", mode="manual")
        metrics = result["lab_metrics"]
        self.assertEqual(metrics["experiment_inputs"], decided)
        self.assertEqual(metrics["experiment_verdict"], "PASS")
        self.assertEqual(metrics["performance_verdict"], "UNMEASURED")
        self.assertNotIn("R-wire", metrics["scenarios"])
        self.assertNotIn("R-stale-clock", metrics["scenarios"])
        self.assertNotIn("wrong_pin", completed.stdout)
        for scenario_id in decided:
            scenario = metrics["scenarios"][scenario_id]
            self.assertEqual(scenario["sample_count"], 1)
            self.assertEqual(scenario["class"], "decided")
        idle = metrics["scenarios"]["R-idle"]["samples"][0]
        self.assertEqual(idle["roster_period_ms"], 300000)
        self.assertEqual(idle["heartbeat_period_ms"], 60000)
        self.assertEqual(idle["stage"], "roster")
        stale = metrics["scenarios"]["R-stale-beat"]["samples"][0]
        self.assertEqual(stale["stale"], 1)
        self.assertEqual(stale["missed_beats"], 3)
        self.assertEqual(stale["stage_unchanged"], 1)
        self.assertEqual(stale["stage"], "roster")
        timeout = metrics["scenarios"]["R-timeout-ui"]["samples"][0]
        self.assertEqual(timeout["terminal"], "timed_out")
        self.assertEqual(timeout["touch_during_wait"], 1)
        self.assertNotIn("wrong_pin", timeout)
