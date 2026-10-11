import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lab_host import (  # noqa: E402
    _refresh_stale_sdkconfig,
    _sdkconfig_missing_assignments,
)


class LabHostSdkconfigTests(unittest.TestCase):
    def test_missing_assignments_detects_stale_sdkconfig(self):
        with self.subTest("empty sdkconfig"):
            sdk = Path("sdkconfig")
            sdk.write_text("# empty\n", encoding="utf-8")
            defaults = Path("defaults")
            defaults.write_text("CONFIG_LAB_AUTO_HARNESS=y\n", encoding="utf-8")
            missing = _sdkconfig_missing_assignments(sdk, [defaults])
            self.assertEqual(missing, ["CONFIG_LAB_AUTO_HARNESS=y"])

        with self.subTest("satisfied sdkconfig"):
            sdk.write_text("CONFIG_LAB_AUTO_HARNESS=y\n", encoding="utf-8")
            missing = _sdkconfig_missing_assignments(sdk, [defaults])
            self.assertEqual(missing, [])

    def test_refresh_removes_stale_sdkconfig(self):
        build = Path("build_dir")
        build.mkdir()
        sdk = build / "sdkconfig"
        sdk.write_text("CONFIG_IDF_TARGET_ESP32S3=y\n", encoding="utf-8")
        defaults = Path("lab.defaults")
        defaults.write_text(
            "CONFIG_LAB02_FIXTURE_16=y\nCONFIG_LAB_AUTO_HARNESS=y\n", encoding="utf-8"
        )
        removed = _refresh_stale_sdkconfig(build, [defaults])
        self.assertTrue(removed)
        self.assertFalse(sdk.is_file())

    def tearDown(self):
        for name in ("sdkconfig", "defaults", "lab.defaults"):
            p = Path(name)
            if p.is_file():
                p.unlink()
        build = Path("build_dir")
        if build.is_dir():
            for child in build.iterdir():
                child.unlink()
            build.rmdir()


if __name__ == "__main__":
    unittest.main()
