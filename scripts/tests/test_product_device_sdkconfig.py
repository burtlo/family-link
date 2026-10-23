import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from deployment_config import Deployment, DeploymentMeta, Endpoint, Kit, SecretsProfile
from product_device_sdkconfig import _normalize_p1_base_url, product_kit_sdkconfig_lines


def _deployment() -> Deployment:
    return Deployment(
        Path("unused"),
        "local",
        DeploymentMeta("desk", "Desk"),
        (),
        (Endpoint("e-a", "", "a", "desk"),),
        (Kit("k-a", "e-a", usb_serial="AA:11", secrets_profile="home"),),
        {"box-a": "e-a"},
        secrets_profiles={
            "home": SecretsProfile(wifi_ssid="TestNet", wifi_password="secret"),
        },
    )


class ProductDeviceSdkconfigTests(unittest.TestCase):
    def test_p1_host_override_normalizes(self):
        self.assertEqual("http://192.168.8.236:8777", _normalize_p1_base_url("192.168.8.236"))
        self.assertEqual(
            "http://192.168.8.236:8777",
            _normalize_p1_base_url("http://192.168.8.236"),
        )

    @patch("product_device_sdkconfig.p1_base_url_for_host", return_value="http://192.0.2.1:8777")
    @patch("product_device_sdkconfig.load_p1_desk_token", return_value="desk-bearer")
    def test_product_kit_lines(self, _token, _url):
        deployment = _deployment()
        kit = deployment.kits[0]
        lines = product_kit_sdkconfig_lines(deployment, kit)
        joined = "\n".join(lines)
        self.assertIn('CONFIG_FAMILY_P1_BASE_URL="http://192.0.2.1:8777"', joined)
        self.assertIn('CONFIG_FAMILY_DESK_TOKEN="desk-bearer"', joined)
        self.assertIn('CONFIG_FAMILY_WIFI_SSID="TestNet"', joined)
        self.assertIn('CONFIG_FAMILY_WIFI_PASSWORD="secret"', joined)


if __name__ == "__main__":
    unittest.main()
