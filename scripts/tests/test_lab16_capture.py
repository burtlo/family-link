"""LAB-16 serial capture verifier."""
from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERIFY = ROOT / "client/labs/lab-16-attached-storage/tools/verify_capture.py"
sys.path.insert(0, str(ROOT / "scripts"))
from device_guardrails.lab16_serial import verify_lab16_upload_capture


class Lab16CaptureTest(unittest.TestCase):
    def test_scripted_ok_sample(self):
        path = self._tmpdir() / "cap.txt"
        path.write_text(self._ok_sample(), encoding="utf-8")
        proc = subprocess.run(
            [sys.executable, str(VERIFY), str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_upload_ready_requires_upload_fixture_fails_without_it(self):
        sample = self._ok_sample().replace("upload_fixture=lab16-upload/v2 ", "")
        errors = verify_lab16_upload_capture(sample)
        self.assertTrue(any("missing LAB16_READY upload harness" in item for item in errors))

    def test_complete_capture_with_guru_rejected(self):
        sample = self._ok_sample() + "Guru Meditation Error: Core 0 panic'ed\n"
        self.assertNotEqual(self._run(sample).returncode, 0)

    def test_absurd_elapsed_rejected(self):
        sample = self._ok_sample().replace("elapsed_ms=210012", "elapsed_ms=10")
        proc = self._run(sample)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("duration contradicts", proc.stderr)

    def _ok_sample(self) -> str:
        return (
            "LAB16_READY build=lab16-fw-v5-upload-memory mode=scripted fixture=lab16-sd/v3 "
            "upload_fixture=lab16-upload/v2 harness=1\n"
            "LAB16_BOOT status=stable settle_ms=500\n"
            "LAB16_SD status=detected sectors=61071360 sector_bytes=512 capacity_bytes=31268536320 "
            "bus_width=4 real_freq_khz=20000 read_lba0=ok probe_writes=0\n"
            "LAB16_STREAM status=recording seconds=210 target_pcm_bytes=6720000 start_lba=2048 sample_rate=16000\n"
            "LAB16_STREAM status=complete elapsed_ms=210012 pcm_bytes=6720000 "
            "sectors_written=13126 driver_writes=1641 start_lba=2048 sample_rate=16000\n"
            "LAB16_AUDIO status=ok header_magic=LAB16PCM peak_first=127 peak_mid=3326 peak_last=3643 "
            "rms_mid=1647251 silent=0\n"
            "LAB16_UPLOAD session=1 status=complete uploaded=210 failed=0\n"
            "LAB16_HARNESS_DONE status=complete\n"
        )

    def _run(self, sample: str):
        path = self._tmpdir() / "cap.txt"
        path.write_text(sample, encoding="utf-8")
        return subprocess.run(
            [sys.executable, str(VERIFY), str(path)],
            capture_output=True,
            text=True,
            check=False,
        )

    def _tmpdir(self) -> Path:
        import tempfile

        return Path(tempfile.mkdtemp())


if __name__ == "__main__":
    unittest.main()
