"""L1 tests for product inbox carousel (P6)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INBOX = ROOT / "client/shared/inbox_carousel"
INCLUDE = INBOX / "fl_product_inbox/include"
SOURCES = [
    INBOX / "fl_product_inbox/carousel.cpp",
    INBOX / "fl_product_inbox/p1_wire.cpp",
    INBOX / "fl_product_inbox/presentation.cpp",
    INBOX / "fl_product_inbox/flsk2.cpp",
    INBOX / "tests/product_inbox_model_test.cpp",
]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class ProductInboxModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-inbox-model-")
        cls.binary = Path(cls.temp.name) / "product_inbox_model_test"
        subprocess.run(
            [
                find_cxx(),
                "-std=c++14",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(INCLUDE),
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

    def test_p6_snapshot_apply(self):
        self._run("snapshot")

    def test_p6_last_viewed_coalesce(self):
        self._run("last-viewed")

    def test_p6_connectivity_blocks_play(self):
        self._run("connectivity")

    def test_p6_prefix_hash_pause(self):
        self._run("prefix-hash")

    def test_p6_sender_fallback(self):
        self._run("presentation")

    def test_p6_flsk2_vectors(self):
        self._run("flsk2")

    def test_p6_recovery_fetch_connecting(self):
        self._run("recovery-fetch")

    def test_p6_inbox_fetch_60s(self):
        self._run("inbox-fetch-60s")

    def test_p6_refresh_503_keeps_inbox(self):
        self._run("refresh-503")

    def test_p6_level1_new_card(self):
        self._run("level1-new-card")

    def test_p6_sketch_through_session(self):
        self._run("sketch-session")


if __name__ == "__main__":
    unittest.main()
