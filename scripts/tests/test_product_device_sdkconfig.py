import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from deployment_config import Deployment, DeploymentMeta, Endpoint, Kit, SecretsProfile
from product_device_sdkconfig import (
    _normalize_p1_base_url,
    p1_base_url_for_secrets_profile,
    product_kit_sdkconfig_lines,
)


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
        self.assertEqual(
            "https://desk.example.ts.net",
            _normalize_p1_base_url("https://desk.example.ts.net"),
        )
        self.assertEqual(
            "https://desk.example.ts.net:8443",
            _normalize_p1_base_url("https://desk.example.ts.net:8443"),
        )

    @patch("product_device_sdkconfig.load_p1_desk_token", return_value="desk-bearer")
    @patch("product_device_sdkconfig._autodetect_p1_base_url", return_value="http://192.0.2.1:8777")
    def test_product_kit_lines_detect_server(self, _autodetect, _token):
        deployment = _deployment()
        kit = deployment.kits[0]
        lines = product_kit_sdkconfig_lines(deployment, kit)
        joined = "\n".join(lines)
        self.assertIn('CONFIG_FAMILY_P1_BASE_URL="http://192.0.2.1:8777"', joined)
        self.assertIn('CONFIG_FAMILY_DESK_TOKEN="desk-bearer"', joined)
        self.assertIn('CONFIG_FAMILY_WIFI_SSID="TestNet"', joined)
        self.assertIn('CONFIG_FAMILY_WIFI_PASSWORD="secret"', joined)

    @patch("product_device_sdkconfig.load_p1_desk_token", return_value="desk-bearer")
    @patch("product_device_sdkconfig._autodetect_p1_base_url")
    def test_product_kit_lines_fixed_p1_server(self, autodetect, _token):
        autodetect.side_effect = AssertionError("autodetect must not run for fixed p1_server")
        deployment = _deployment()
        deployment = Deployment(
            deployment.path,
            deployment.source,
            deployment.meta,
            deployment.users,
            deployment.endpoints,
            deployment.kits,
            deployment.aliases,
            secrets_profiles={
                "home": SecretsProfile(
                    wifi_ssid="TestNet",
                    wifi_password="secret",
                    p1_server="https://hivemind.tailfa86ac.ts.net/",
                ),
            },
        )
        lines = product_kit_sdkconfig_lines(deployment, deployment.kits[0])
        self.assertIn(
            'CONFIG_FAMILY_P1_BASE_URL="https://hivemind.tailfa86ac.ts.net"',
            "\n".join(lines),
        )

    def test_p1_base_url_for_profile_respects_detect(self):
        profile = SecretsProfile(wifi_ssid="n", p1_server="detect")
        with patch(
            "product_device_sdkconfig._autodetect_p1_base_url",
            return_value="http://192.0.2.9:8777",
        ):
            self.assertEqual(
                "http://192.0.2.9:8777",
                p1_base_url_for_secrets_profile(profile),
            )


if __name__ == "__main__":
    unittest.main()
