"""L1 tests for product send outbox (P8)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SEND = ROOT / "client/shared/message_delivery"
INCLUDE = SEND / "fl_product_send/include"
SOURCES = [
    SEND / "fl_product_send/classify.cpp",
    SEND / "fl_product_send/journey.cpp",
    SEND / "fl_product_send/send_stage.cpp",
    SEND / "fl_product_send/outbox.cpp",
    SEND / "fl_product_send/p1_wire.cpp",
    SEND / "fl_product_send/picker.cpp",
    SEND / "fl_product_send/sha256.cpp",
    SEND / "fl_product_send/tests/product_send_model_test.cpp",
]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class ProductSendModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-send-model-")
        cls.binary = Path(cls.temp.name) / "product_send_model_test"
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

    def test_p8_part_size(self):
        self._run("part-size")

    def test_p8_lost_receipt(self):
        self._run("lost-receipt")

    def test_p8_identity_conflict(self):
        self._run("identity-conflict")

    def test_p8_offline_queue(self):
        self._run("offline-queue")

    def test_p8_cancel_lookup(self):
        self._run("cancel-lookup")

    def test_p8_empty_copy(self):
        self._run("empty-copy")

    def test_p8_wait_body(self):
        self._run("wait-body")

    def test_p8_receipt_state(self):
        self._run("receipt-state")

    def test_p8_http_415(self):
        self._run("http-415")

    def test_p8_couldnt_dwell(self):
        self._run("couldnt-dwell")

    def test_p8_sender_binding(self):
        self._run("sender-binding")

    def test_p8_leave_skips_sent(self):
        self._run("leave-skips-sent")

    def test_p8_client_uuid(self):
        self._run("client-uuid")

    def test_p8_recipient_fallback(self):
        self._run("recipient-fallback")

    def test_p8_refresh_wait(self):
        self._run("refresh-wait")

    def test_p8_inbox_fetch(self):
        self._run("inbox-fetch")

    def test_p8_mute_rules(self):
        self._run("mute-rules")

    def test_p8_malformed_wait(self):
        self._run("malformed-wait")

    def test_p8_trace(self):
        self._run("trace")

    def test_p8_inbox_restore(self):
        self._run("inbox-restore")

    def test_p3_stage_buttons(self):
        self._run("stage-buttons")

    def test_p5_couldnt_record(self):
        self._run("couldnt-record")

    def test_p5_frozen_recipients(self):
        self._run("frozen-recipients")

    def test_p4_picker_header(self):
        self._run("picker-header")

    def test_p4_picker_focus(self):
        self._run("picker-focus")

    def test_p4_picker_refresh(self):
        self._run("picker-refresh")

    def test_p6_stored_part(self):
        self._run("stored-part")

    def test_p6_subtitle_overflow(self):
        self._run("subtitle-overflow")

    def test_p6_sent_dwell(self):
        self._run("sent-dwell")

    def test_p6_leave_wait_place(self):
        self._run("leave-wait-place")

    def test_p6_sender_mismatch(self):
        self._run("sender-mismatch")

    def test_p7_session_bounds(self):
        self._run("phase7-bounds")


if __name__ == "__main__":
    unittest.main()
