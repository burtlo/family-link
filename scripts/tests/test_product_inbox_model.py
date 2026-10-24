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
    INBOX / "fl_product_inbox/inbox_wire.cpp",
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

    def test_p3_pause_for_record(self):
        self._run("pause-for-record")

    def test_p6_sender_fallback(self):
        self._run("presentation")

    def test_p3_chrome_selected(self):
        self._run("chrome-selected")

    def test_p3_chrome_playing(self):
        self._run("chrome-playing")

    def test_p3_chrome_empty(self):
        self._run("chrome-empty")

    def test_p3_chrome_connecting(self):
        self._run("chrome-connecting")

    def test_p3_chrome_cant_play(self):
        self._run("chrome-cant-play")

    def test_p3_chrome_duration(self):
        self._run("chrome-duration")

    def test_p3_chrome_card_face(self):
        self._run("chrome-card-face")

    def test_p6_flsk2_vectors(self):
        self._run("flsk2")

    def test_p6_recovery_fetch_connecting(self):
        self._run("recovery-fetch")

    def test_p6_inbox_fetch_60s(self):
        self._run("inbox-fetch-60s")

    def test_p6_refresh_503_keeps_inbox(self):
        self._run("refresh-503")

    def test_p6_refresh_failure_stays(self):
        self._run("refresh-failure-stays")

    def test_p6_level1_new_card(self):
        self._run("level1-new-card")

    def test_p6_sketch_through_session(self):
        self._run("sketch-session")

    def test_p6_newest_fallback(self):
        self._run("newest-fallback")

    def test_p6_last_viewed_no_steal(self):
        self._run("last-viewed-no-steal")

    def test_p6_delete_selected_middle(self):
        self._run("delete-selected-middle")

    def test_p6_delete_selected_oldest(self):
        self._run("delete-selected-oldest")

    def test_p6_delete_only_message(self):
        self._run("delete-only-message")

    def test_p6_selection_keeps_play_head(self):
        self._run("selection-keeps-play-head")

    def test_p6_completed_replay(self):
        self._run("completed-replay")

    def test_p6_chunks_cover_asset(self):
        self._run("chunks-cover-asset")

    def test_p6_wifi_drop_keeps_progress(self):
        self._run("wifi-drop-keeps-progress")

    def test_p6_mute_retires_generation(self):
        self._run("mute-retires-generation")

    def test_p6_generation_zero(self):
        self._run("generation-zero")

    def test_p6_connecting_waits(self):
        self._run("connecting-waits")

    def test_p6_snapshot_overflow(self):
        self._run("snapshot-overflow")

    def test_p6_connecting_before_snapshot(self):
        self._run("connecting-before-snapshot")

    def test_p6_empty_then_first_message(self):
        self._run("empty-then-first-message")

    def test_p6_sketch_validation_keeps_playing(self):
        self._run("sketch-validation-keeps-playing")

    def test_p6_wire_inbox_poll(self):
        self._run("wire-inbox-poll")

    def test_p6_wire_recovery_nowifi(self):
        self._run("wire-recovery-nowifi")

    def test_p6_wire_arm_fetch(self):
        self._run("wire-arm-fetch")

    def test_p6_wire_last_viewed(self):
        self._run("wire-last-viewed")

    def test_p6_wire_mute_generation(self):
        self._run("wire-mute-generation")

    def test_p6_wire_audio_job(self):
        self._run("wire-audio-job")

    def test_p6_wire_inbox_hidden(self):
        self._run("wire-inbox-hidden")

    def test_p6_wire_arm_survives_inflight(self):
        self._run("wire-arm-survives-inflight")

    def test_p6_wire_media_cancelled(self):
        self._run("wire-media-cancelled")


if __name__ == "__main__":
    unittest.main()
