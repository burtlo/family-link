"""Meta-tests for app:test discovery (config/app-test-suite.yaml)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from app_test_suite import DEFAULT_CONFIG, discover_modules, discover_stems

ROOT = Path(__file__).resolve().parents[2]


class AppTestSuiteDiscoveryTests(unittest.TestCase):
    def test_includes_core_product_modules(self):
        modules = set(discover_modules(DEFAULT_CONFIG))
        expected = {
            "scripts.tests.test_device_guardrails",
            "scripts.tests.test_product_login_model",
            "scripts.tests.test_app_host",
            "scripts.tests.test_product_device_sdkconfig",
            "scripts.tests.test_device_ui_catalog",
            "scripts.tests.test_firmware",
        }
        self.assertTrue(expected.issubset(modules))

    def test_excludes_lab_and_product_server_modules(self):
        modules = set(discover_modules(DEFAULT_CONFIG))
        self.assertNotIn("scripts.tests.test_lab02_architecture", modules)
        self.assertNotIn("scripts.tests.test_product_server_startup", modules)
        self.assertNotIn("scripts.tests.test_project_config", modules)

    def test_includes_suite_meta_tests(self):
        stems = discover_stems(DEFAULT_CONFIG)
        self.assertIn("test_app_test_suite", stems)


if __name__ == "__main__":
    unittest.main()
