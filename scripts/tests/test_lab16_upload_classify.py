"""LAB-16 upload adapter classification (metrics vs serial verify)."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import lab_adapter_lab16 as adapter


def _metrics(functional: str = "PASS", memory: str = "COMPLETE") -> dict:
    return {"functional_status": functional, "memory_status": memory,
            "incomplete_reasons": ["missing_beat"], "functional_incomplete_reasons": ["sd_probe_not_verified"]}


class Lab16UploadClassifyTest(unittest.TestCase):
    def test_metrics_pass_overrides_stale_serial_smoke_gaps(self):
        serial = Path(tempfile.mkdtemp()) / "serial.txt"
        serial.write_text("LAB16_HARNESS_DONE status=complete\n", encoding="utf-8")
        post = "LAB16_READY build=x\nLAB16_HARNESS_DONE status=complete\n"
        result = adapter.classify_upload_capture(post, True, serial, _metrics(), server_errors=[])
        self.assertEqual(result["workflow_status"], "PASS")
        self.assertEqual(result["experiment_verdict"], "PASS")
        self.assertTrue(result["caller_completed"])

    def test_functional_fail_uses_parser_verdict(self):
        serial = Path(tempfile.mkdtemp()) / "serial.txt"
        serial.write_text("", encoding="utf-8")
        post = "LAB16_READY \nLAB16_HARNESS_DONE status=complete\n"
        result = adapter.classify_upload_capture(
            post, True, serial,
            {"functional_status": "FAIL", "memory_status": "COMPLETE",
             "functional_verification_errors": ["chunk hash mismatch"]},
            server_errors=[],
        )
        self.assertEqual(result["workflow_status"], "FAIL")
        self.assertEqual(result["experiment_verdict"], "FAIL")

    def test_memory_incomplete_after_successful_device_run(self):
        serial = Path(tempfile.mkdtemp()) / "serial.txt"
        serial.write_text("", encoding="utf-8")
        post = "LAB16_READY \nLAB16_HARNESS_DONE status=complete\n"
        result = adapter.classify_upload_capture(
            post, True, serial, _metrics(memory="INCOMPLETE"), server_errors=[],
        )
        self.assertEqual(result["workflow_status"], "INCOMPLETE")
        self.assertEqual(result["experiment_verdict"], "UNMEASURED")


if __name__ == "__main__":
    unittest.main()
