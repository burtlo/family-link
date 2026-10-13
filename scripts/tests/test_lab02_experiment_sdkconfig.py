import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from lab02_experiment_sdkconfig import write_lab02_local_overlay
import lab_host


class LocalOverlayTests(unittest.TestCase):
    def test_frozen_run_inputs_survive_registry_drift_during_build(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            registry = root / "registry.yaml"
            registry.write_text("changed after server start", encoding="utf-8")
            config = SimpleNamespace(path_value=lambda key: registry)
            spec = lab_host.get_lab("lab02")
            variant = lab_host.resolve_variant(spec, "recycle")
            build_dir = root / "build" / "lab02"
            defaults = lab_host._defaults_files_for_lab_build(config, spec, variant, build_dir,
                "", "", frozen_fixture_size=64, frozen_scenario="nominal",
                frozen_build_id="frozen-run-identity")
            overlay = defaults[-1].read_text(encoding="utf-8")
            self.assertIn("CONFIG_LAB02_FIXTURE_64=y", overlay)
            self.assertIn('CONFIG_LAB02_SCENARIO="nominal"', overlay)
            self.assertIn('CONFIG_LAB02_BUILD_ID="frozen-run-identity"', overlay)

    def test_overlay_freezes_local_fixture_scenario_and_harness_without_secrets(self):
        with tempfile.TemporaryDirectory() as raw:
            build = Path(raw) / "local-build"
            build.mkdir()
            path = write_lab02_local_overlay(build, fixture_size=16, scenario="nominal", scripted=True,
                                             build_id="frozen-identity")
            text = path.read_text(encoding="utf-8")
            self.assertIn('CONFIG_LAB02_SERVER_URL=""', text)
            self.assertIn('CONFIG_LAB02_WIFI_PASSWORD=""', text)
            self.assertIn("CONFIG_LAB02_FIXTURE_16=y", text)
            self.assertIn("CONFIG_LAB_AUTO_HARNESS=y", text)
            self.assertIn('CONFIG_LAB02_BUILD_ID="frozen-identity"', text)
            self.assertNotIn("password-from-roster", text)

    def test_overlay_change_invalidates_stale_sdkconfig(self):
        with tempfile.TemporaryDirectory() as raw:
            build = Path(raw) / "local-build"
            build.mkdir()
            sdkconfig = build / "sdkconfig"
            sdkconfig.write_text("CONFIG_OLD=y\n", encoding="utf-8")
            write_lab02_local_overlay(build, fixture_size=16, scenario="nominal", scripted=False)
            self.assertFalse(sdkconfig.exists())


if __name__ == "__main__":
    unittest.main()
