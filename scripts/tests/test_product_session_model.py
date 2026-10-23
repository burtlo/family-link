"""L1 race tests for product session credential (P5)."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOGIN = ROOT / "client/shared/login_session"
INCLUDE = LOGIN / "fl_product_login/include"
SOURCES = [
    LOGIN / "fl_product_login/session.cpp",
    LOGIN / "fl_product_login/pin_session.cpp",
    LOGIN / "fl_product_login/scoped_session.cpp",
    LOGIN / "fl_product_login/credential_session.cpp",
    LOGIN / "fl_product_login/pollers.cpp",
    LOGIN / "fl_product_login/p1_wire.cpp",
    LOGIN / "fl_product_login/workers.cpp",
    LOGIN / "tests/product_session_model_test.cpp",
]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class ProductSessionModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-session-model-")
        cls.binary = Path(cls.temp.name) / "product_session_model_test"
        subprocess.run([
            find_cxx(), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(INCLUDE), *[str(path) for path in SOURCES], "-o", str(cls.binary),
        ], check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _run(self, name: str) -> None:
        completed = subprocess.run([str(self.binary), name], check=True, cwd=ROOT, capture_output=True, text=True)
        self.assertNotIn("pin=", completed.stdout.lower())
        for digit_run in re.findall(r"\b[1-9]{4}\b", completed.stdout):
            self.assertNotEqual(len(digit_run), 4, "possible PIN leaked in trace")

    def test_p5_refresh_and_bound(self):
        self._run("refresh-bound")
        self._run("expire-bound")

    def test_p5_refresh_failure_stays_signed_in(self):
        self._run("refresh-fail")

    def test_p5_mute_invalidate_spacing(self):
        self._run("mute-invalidate")

    def test_p5_late_pin_after_mute(self):
        self._run("late-pin-mute")

    def test_p5_inbox_generation_fence(self):
        self._run("inbox-fence")

    def test_p5_send_worker_generation_fence(self):
        self._run("send-fence-mute")
        self._run("send-fence-switch")

    def test_p5_inbox_stale_prior_user(self):
        self._run("inbox-stale-user")

    def test_p5_health_ack_gate(self):
        self._run("health-ack")

    def test_p5_late_pin_after_sign_in_b(self):
        self._run("late-pin-b")

    def test_p5_mute_invalidate_local(self):
        self._run("mute-invalidate-local")

    def test_p5_lab09_scoped_scenarios(self):
        self._run("lab09-scenarios")

    def test_p5_health_lost_ack_signed_out(self):
        self._run("health-signed-out")


if __name__ == "__main__":
    unittest.main()
