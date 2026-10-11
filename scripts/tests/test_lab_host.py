import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lab_host import (  # noqa: E402
    _defaults_files_for_lab_build,
    _prepare_lab_build_directory,
    _refresh_stale_sdkconfig,
    _sdkconfig_missing_assignments,
    clean_lab,
)
from lab_registry import get_lab, resolve_variant
from project_config import ProjectConfig, ProjectConfigError


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

    def test_prepare_unconfigured_build_dir_is_empty(self):
        build = Path("build_prepare")
        build.mkdir(exist_ok=True)
        (build / "CMakeFiles").mkdir()
        _prepare_lab_build_directory(build)
        self.assertTrue(build.is_dir())
        self.assertEqual([], list(build.iterdir()))

    def test_clean_lab_removes_build_dir(self):
        config = ProjectConfig()
        spec = get_lab("lab02")
        var = resolve_variant(spec, "phaseb")
        with tempfile.TemporaryDirectory() as temp, patch("lab_host.ROOT", Path(temp)):
            build_dir = Path(temp) / var.build_dir
            build_dir.parent.mkdir(parents=True, exist_ok=True)
            build_dir.mkdir(exist_ok=True)
            (build_dir / "marker").write_text("x", encoding="utf-8")
            clean_lab(config, "lab02", "phaseb")
            self.assertFalse(build_dir.exists())

    def test_phaseb_build_requires_kit(self):
        config = ProjectConfig()
        spec = get_lab("lab02")
        var = resolve_variant(spec, "phaseb")
        build_dir = Path("build_dir_phaseb")
        build_dir.mkdir(exist_ok=True)
        with self.assertRaisesRegex(ProjectConfigError, "Phase B build requires KIT"):
            _defaults_files_for_lab_build(config, spec, var, build_dir, "", "")

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
        phaseb = Path("build_dir_phaseb")
        if phaseb.is_dir():
            for child in phaseb.iterdir():
                child.unlink()
            phaseb.rmdir()
        prepare = Path("build_prepare")
        if prepare.is_dir():
            import shutil
            shutil.rmtree(prepare)
        phaseb_build = Path("build/labs/lab-02-bounded-inbox-phaseb")
        if phaseb_build.is_dir():
            import shutil
            shutil.rmtree(phaseb_build)


if __name__ == "__main__":
    unittest.main()
