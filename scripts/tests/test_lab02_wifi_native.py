"""Native host lifecycle checks for the LAB-02 Wi-Fi setup retry path."""

import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-02-bounded-inbox"
STUBS = LAB / "tests/wifi_host_stubs"
WIFI_SOURCE = LAB / "components/lab02_platform/wifi.c"


class Lab02WifiNativeTests(unittest.TestCase):
    def test_setup_is_reused_and_errors_are_returned(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_wifi_lifecycle"
            subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(STUBS),
                 "-I", str(LAB / "components/lab02_platform/include"),
                 str(LAB / "tests/test_wifi_lifecycle_native.c"), str(WIFI_SOURCE),
                 "-o", str(output)],
                check=True, capture_output=True, text=True,
            )
            for scenario, marker in (
                ("lifecycle", "WIFI_LIFECYCLE_RESULT PASS"),
                ("init-fail", "WIFI_INIT_ERROR_RESULT PASS"),
                ("start-fail", "WIFI_START_ERROR_RESULT PASS"),
            ):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(output), scenario],
                                            check=True, capture_output=True, text=True,
                                            timeout=5)
                    self.assertIn(marker, result.stdout)


if __name__ == "__main__":
    unittest.main()
