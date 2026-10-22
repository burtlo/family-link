"""Compile and run the LAB-09 session model."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB07 = ROOT / "client/labs/lab-07-readiness-roster"
LAB09 = ROOT / "client/labs/lab-09-session-isolation"
INCLUDE = [
    LAB07 / "components/lab07_session/include",
    LAB09 / "components/lab09_probe/include",
]
SOURCES = [
    LAB07 / "components/lab07_session/session.cpp",
    LAB07 / "components/lab07_session/pin_session.cpp",
    LAB07 / "components/lab07_session/scoped_session.cpp",
    LAB09 / "components/lab09_probe/probe.cpp",
    LAB09 / "tests/model_test.cpp",
]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class Lab09ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab09-model-")
        cls.binary = Path(cls.temp.name) / "model_test.exe"
        command = [find_cxx(), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        for include in INCLUDE:
            command.extend(["-I", str(include)])
        command.extend(str(path) for path in SOURCES)
        command.extend(["-o", str(cls.binary)])
        subprocess.run(command, check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _run(self, name: str) -> subprocess.CompletedProcess:
        return subprocess.run([str(self.binary), name], check=True, cwd=ROOT, capture_output=True, text=True)

    def test_lab09_mute_release(self):
        self._run("mute-release")

    def test_lab09_no_cross_apply(self):
        self._run("no-cross")

    def test_lab09_one_terminal(self):
        self._run("one-terminal")

    def test_lab09_historical_redelivery(self):
        self._run("historical")

    def test_lab09_signout_switch_push_expire(self):
        for name in ("signout", "push", "expire", "overflow", "newer", "missing", "level"):
            self._run(name)
