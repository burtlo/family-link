"""Kit builds for the login labs use the bounded-inbox workstation address."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from lab_login_reachability import (  # noqa: E402
    login_lab_assignments,
    workstation_origin,
    write_login_lab_defaults,
)
from project_config import ProjectConfig, ProjectConfigError  # noqa: E402


class LoginReachabilityTests(unittest.TestCase):
    def test_assignments_use_the_lab_port_and_quote_wifi(self):
        lines = login_lab_assignments("LAB-08", "http://10.1.2.3:8768", 'desk"net', "secret")
        self.assertIn('CONFIG_LAB08_PIN_URL="http://10.1.2.3:8768/pin"', lines)
        self.assertIn('CONFIG_LAB08_OBSERVE_URL="http://10.1.2.3:8768/observe"', lines)
        self.assertIn('CONFIG_LAB08_MALFORMED_URL="http://10.1.2.3:8768/malformed"', lines)
        self.assertIn('CONFIG_LAB08_WIFI_SSID="desk\\"net"', lines)
        self.assertTrue(any(line.startswith("CONFIG_LAB08_WIFI_PASSWORD=") for line in lines))

    def test_each_lab_keeps_its_own_port(self):
        roster = login_lab_assignments("LAB-07", "http://10.1.2.3:8767", "desk", "secret")
        session = login_lab_assignments("LAB-09", "http://10.1.2.3:8769", "desk", "secret")
        self.assertIn('CONFIG_LAB07_ROSTER_URL="http://10.1.2.3:8767/roster?script=eight"', roster)
        self.assertIn('CONFIG_LAB09_BASE_URL="http://10.1.2.3:8769"', session)

    def test_missing_wifi_is_rejected(self):
        with self.assertRaises(ProjectConfigError):
            login_lab_assignments("LAB-08", "http://10.1.2.3:8768", "", "secret")

    def test_changed_address_drops_stale_sdkconfig(self):
        with tempfile.TemporaryDirectory() as tmp:
            build_dir = Path(tmp) / "lab-08-scripted"
            build_dir.mkdir()
            sdkconfig = build_dir / "sdkconfig"
            sdkconfig.write_text('CONFIG_LAB08_PIN_URL="http://127.0.0.1:8768/pin"\n', encoding="utf-8")
            first = login_lab_assignments("LAB-08", "http://10.1.2.3:8768", "desk", "secret")
            path = write_login_lab_defaults(build_dir, "box-a", first, "digest-1")
            self.assertFalse(sdkconfig.exists())
            self.assertIn("10.1.2.3:8768", path.read_text(encoding="utf-8"))
            sdkconfig.write_text("CONFIG_LAB08_PIN_URL=kept\n", encoding="utf-8")
            again = write_login_lab_defaults(build_dir, "box-a", first, "digest-1")
            self.assertEqual(again, path)
            self.assertTrue(sdkconfig.exists())
            second = login_lab_assignments("LAB-08", "http://10.9.8.7:8768", "desk", "secret")
            write_login_lab_defaults(build_dir, "box-a", second, "digest-2")
            self.assertFalse(sdkconfig.exists())
            self.assertIn("10.9.8.7:8768", path.read_text(encoding="utf-8"))

    def test_workstation_origin_keeps_the_bounded_inbox_host(self):
        workflow = SimpleNamespace(client_url="http://192.168.8.199:8765")
        with patch("lab02_workflow_config.load_lab02_workflow_config", return_value=workflow):
            self.assertEqual(workstation_origin(ProjectConfig(), 8768), "http://192.168.8.199:8768")
            self.assertEqual(workstation_origin(ProjectConfig(), 8767), "http://192.168.8.199:8767")

    def test_loopback_origin_is_rejected_for_a_kit_build(self):
        workflow = SimpleNamespace(client_url="http://127.0.0.1:8765")
        with patch("lab02_workflow_config.load_lab02_workflow_config", return_value=workflow):
            with self.assertRaises(ProjectConfigError):
                workstation_origin(ProjectConfig(), 8769)


if __name__ == "__main__":
    unittest.main()
