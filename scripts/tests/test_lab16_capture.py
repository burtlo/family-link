"""LAB-16 serial capture verifier."""
from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERIFY = ROOT / "client/labs/lab-16-attached-storage/tools/verify_capture.py"


class Lab16CaptureTest(unittest.TestCase):
    def test_scripted_ok_sample(self):
        sample = (
            "LAB16_READY build=lab16-fw-v2 mode=scripted fixture=lab16-sd/v3 harness=1\n"
            "LAB16_BOOT status=stable settle_ms=500\n"
            "LAB16_SD status=detected sectors=61071360 sector_bytes=512 capacity_bytes=31268536320 "
            "bus_width=4 real_freq_khz=20000 read_lba0=ok probe_writes=0\n"
            "LAB16_STREAM status=recording seconds=210 target_pcm_bytes=6720000 start_lba=2048 sample_rate=16000\n"
            "LAB16_STREAM status=complete elapsed_ms=210012 pcm_bytes=6720000 "
            "sectors_written=13126 driver_writes=1641 start_lba=2048 sample_rate=16000\n"
            "LAB16_UPLOAD status=complete uploaded=210 failed=0\n"
            "LAB16_HARNESS_DONE status=complete\n"
        )
        path = self._tmpdir() / "cap.txt"
        path.write_text(sample, encoding="utf-8")
        proc = subprocess.run(
            [sys.executable, str(VERIFY), str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def _tmpdir(self) -> Path:
        import tempfile

        return Path(tempfile.mkdtemp())


if __name__ == "__main__":
    unittest.main()
