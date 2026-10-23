"""L1/L2 tests for desk volume (P9)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VOLUME = ROOT / "client/shared/desk_volume"
INBOX = ROOT / "client/shared/inbox_carousel"
LOGIN = ROOT / "client/shared/login_session"
VOLUME_INCLUDE = VOLUME / "fl_product_volume/include"
INBOX_INCLUDE = INBOX / "fl_product_inbox/include"
LOGIN_INCLUDE = LOGIN / "fl_product_login/include"
SOURCES = [
    VOLUME / "fl_product_volume/stops.cpp",
    VOLUME / "fl_product_volume/storage.cpp",
    VOLUME / "fl_product_volume/session.cpp",
    VOLUME / "fl_product_volume/journey.cpp",
    INBOX / "fl_product_inbox/carousel.cpp",
    INBOX / "fl_product_inbox/p1_wire.cpp",
    INBOX / "fl_product_inbox/presentation.cpp",
    INBOX / "fl_product_inbox/flsk2.cpp",
    LOGIN / "fl_product_login/session.cpp",
    LOGIN / "fl_product_login/pin_session.cpp",
    LOGIN / "fl_product_login/scoped_session.cpp",
    LOGIN / "fl_product_login/credential_session.cpp",
    LOGIN / "fl_product_login/pollers.cpp",
    LOGIN / "fl_product_login/p1_wire.cpp",
    LOGIN / "fl_product_login/workers.cpp",
    VOLUME / "fl_product_volume/tests/product_volume_model_test.cpp",
]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class ProductVolumeModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-volume-model-")
        cls.binary = Path(cls.temp.name) / "product_volume_model_test"
        subprocess.run(
            [
                find_cxx(),
                "-std=c++14",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(VOLUME_INCLUDE),
                "-I",
                str(INBOX_INCLUDE),
                "-I",
                str(LOGIN_INCLUDE),
                *[str(path) for path in SOURCES],
                "-o",
                str(cls.binary),
            ],
            check=True,
            cwd=ROOT,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _run(self, name: str) -> None:
        subprocess.run([str(self.binary), name], check=True, cwd=ROOT)

    def test_p9_stop_table(self):
        self._run("stop-table")

    def test_p9_persist_fail_still_leaves(self):
        self._run("persist-fail")

    def test_p9_red_ignored_connectivity(self):
        self._run("red-ignored")

    def test_p9_playback_pause_flag(self):
        self._run("playback-pause")

    def test_p9_mute_signs_out(self):
        self._run("mute-signout")

    def test_p9_boot_roundtrip_journey(self):
        self._run("boot-roundtrip")

    def test_p9_signed_in_boot_return(self):
        self._run("signed-in-boot")

    def test_p9_play_head_boot_roundtrip(self):
        self._run("play-head-boot")

    def test_p9_restart_after_failed_persist(self):
        self._run("restart-persist")

    def test_p9_speaker_codec_callback(self):
        self._run("speaker-callback")


if __name__ == "__main__":
    unittest.main()
