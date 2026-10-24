"""L1 tests for promoted product login session (P4) against P1 wire parsers."""
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
    LOGIN / "tests/product_login_model_test.cpp",
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


class ProductLoginModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-login-model-")
        cls.binary = Path(cls.temp.name) / "product_login_model_test"
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

    def test_p4_cadence(self):
        self._run("cadence")

    def test_p4_timeout(self):
        self._run("timeout")

    def test_p4_one_retry(self):
        self._run("retry")
        self._run("retry-fail")

    def test_p4_malformed_roster_not_selectable(self):
        self._run("invalid")
        self._run("invalid-json")

    def test_p4_request_identity(self):
        self._run("identity")

    def test_p4_mute_stale(self):
        self._run("mute-stale")

    def test_p4_mute_release(self):
        self._run("mute-release")

    def test_p4_stage_enum(self):
        self._run("stage")
        header = (INCLUDE / "fl_product_login_stages.hpp").resolve()
        for path in (LOGIN / "fl_product_login").rglob("*"):
            if path.suffix not in {".hpp", ".h", ".cpp", ".c"} or path.resolve() == header:
                continue
            text = path.read_text(encoding="utf-8")
            self.assertIsNone(STAGE_DEFINITION.search(text), f"{path} also defines a stage token")

    def test_p4_stale_health_ignores_ui(self):
        self._run("stale")

    def test_p4_roster_queue_and_terminal(self):
        self._run("roster")
        self._run("terminal")

    def test_p4_connecting_backoff(self):
        self._run("connecting-backoff")

    def test_p4_pin_lost_response_replay(self):
        self._run("pin-lost")

    def test_p4_pin_cooldown_through_mute(self):
        self._run("pin-cooldown-mute")

    def test_p4_card_background_fallback(self):
        self._run("card-fallback")

    def test_p4_wrong_pin_keypad_reopens(self):
        self._run("wrong-pin-keypad")

    def test_p4_wrong_pin_wire_json(self):
        self._run("wrong-pin-wire")

    def test_p4_pin_refused_recovery_without_mute(self):
        self._run("pin-refused-recovery")

    def test_p4_pin_partial_boot_cancels_operation_key(self):
        self._run("pin-partial-boot-key")

    def test_p4_cooldown_returns_to_roster(self):
        self._run("cooldown-exit")

    def test_p4_boot_ignored_during_status(self):
        self._run("boot-status")

    def test_p4_partial_pin_idle_pauses(self):
        self._run("pin-idle-pause")

    def test_p4_login_recency_order(self):
        self._run("login-recency")

    def test_p4_unknown_user_refresh(self):
        self._run("unknown-refresh")

    def test_p4_failed_body_arms_backoff(self):
        self._run("body-backoff")

    def test_p4_roster_accepts_uint32_revision(self):
        self._run("roster-revision")

    def test_p4_pin_retry_honors_delay(self):
        self._run("pin-retry-wait")


if __name__ == "__main__":
    unittest.main()
