"""Compile and run the LAB-08 session model."""
import hashlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB07 = ROOT / "client/labs/lab-07-readiness-roster"
LAB08 = ROOT / "client/labs/lab-08-pin-cooldown"
INCLUDE = [
    LAB07 / "components/lab07_session/include",
    LAB08 / "components/lab08_pin/include",
]
SOURCES = [
    LAB07 / "components/lab07_session/session.cpp",
    LAB07 / "components/lab07_session/pin_session.cpp",
    LAB07 / "components/lab07_session/scoped_session.cpp",
    LAB08 / "components/lab08_pin/pin_http.cpp",
    LAB08 / "components/lab08_pin/pin_catalog.cpp",
    LAB08 / "tests/model_test.cpp",
]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class Lab08ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab08-model-")
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

    def test_lab08_transport_schedule(self):
        self._run("transport")

    def test_lab08_cooldown(self):
        self._run("cooldown")

    def test_lab08_wrong_pin_copy_time(self):
        self._run("wrong-copy")

    def test_lab08_mask(self):
        catalog = self._run("catalog").stdout
        for pin in ("1234", "5678", "9999", "1111"):
            self.assertNotRegex(catalog, rf"(?<![0-9a-fA-F]){pin}(?![0-9a-fA-F])")
            self.assertNotIn(hashlib.sha256(pin.encode("ascii")).hexdigest(), catalog)
        self._run("mask")

    def test_lab08_boot(self):
        self._run("boot")

    def test_lab08_one_terminal(self):
        self._run("one-terminal")

    def test_lab08_mute_release(self):
        self._run("mute-release")

    def test_identity_queue_and_level0(self):
        for name in ("identity", "newer", "overflow", "roster-digits", "bad-body", "level0", "lost", "idle", "switch-user"):
            self._run(name)
