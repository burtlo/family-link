#!/usr/bin/env python3
"""Execute the bounded scripted scheduler against native capture/render code."""

import json
import subprocess
import tempfile
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
ROOT = PACKAGE.parents[2]


class ScriptedDeviceContractTests(unittest.TestCase):
    def test_native_scheduler_capture_and_continuous_render(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "lab10_scripted_native_test"
            command = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                f"-I{PACKAGE / 'components/lab10_ui/include'}",
                f"-I{PACKAGE / 'components/lab10_capture/include'}",
                f"-I{PACKAGE / 'components/lab10_render/include'}",
                str(PACKAGE / "components/lab10_ui/lab10_ui.c"),
                str(PACKAGE / "components/lab10_capture/lab10_capture.c"),
                str(PACKAGE / "components/lab10_render/lab10_render.c"),
                str(PACKAGE / "tests/test_lab10_scripted.c"),
                "-o",
                str(binary),
            ]
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
            result = subprocess.run(
                [str(binary)], cwd=ROOT, check=True, capture_output=True, text=True
            )
            self.assertIn("LAB10_SCRIPTED_NATIVE_TEST_PASS", result.stdout)

    def test_device_defaults_select_connected_segments(self):
        kconfig = (PACKAGE / "main/Kconfig.projbuild").read_text(encoding="utf-8")
        defaults = (PACKAGE / "sdkconfig.defaults").read_text(encoding="utf-8")
        scripted_defaults = (PACKAGE / "sdkconfig.defaults.scripted").read_text(
            encoding="utf-8"
        )
        bridge = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
        manifest = json.loads((PACKAGE / "manifest.lab.json").read_text())

        self.assertIn("default LAB10_RENDER_MODE_SEGMENT", kconfig)
        self.assertIn("CONFIG_LAB10_RENDER_MODE_SEGMENT=y", defaults)
        self.assertIn("CONFIG_LAB10_RENDER_MODE_SEGMENT=y", scripted_defaults)
        self.assertIn("return LAB10_RENDER_MODE_SEGMENT;", bridge)
        self.assertEqual(
            manifest["rendering_policy"]["hardware_selection"],
            "package-local LAB10_RENDER_MODE Kconfig choice; default segment candidate for pen-like same-stroke drawing; direct_point remains an explicitly labeled control",
        )


if __name__ == "__main__":
    unittest.main()
