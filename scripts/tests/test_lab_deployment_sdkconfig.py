import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from deployment_config import (
    Deployment,
    DeploymentMeta,
    Kit,
    LabDeskSettings,
    SecretsProfile,
)
from lab_deployment_sdkconfig import (
    deployment_kit_defaults_path,
    deployment_kit_sdkconfig_lines,
    sdkconfig_quoted_value,
    write_deployment_kit_sdkconfig,
)


def _deployment(**kwargs) -> Deployment:
    defaults = {
        "path": Path("unused"),
        "source": "local",
        "meta": DeploymentMeta("desk", "Desk"),
        "users": (),
        "endpoints": (),
        "kits": (),
        "aliases": {},
        "lab": LabDeskSettings(),
        "secrets_profiles": {},
    }
    defaults.update(kwargs)
    return Deployment(**defaults)


class LabDeploymentSdkconfigTests(unittest.TestCase):
    def test_quoted_value_escapes(self):
        self.assertEqual('"plain"', sdkconfig_quoted_value("plain"))
        self.assertEqual('"say \\"hi\\""', sdkconfig_quoted_value('say "hi"'))

    def test_lines_use_secrets_profile(self):
        kit = Kit("lynn-kit", "endpoint-lynn", secrets_profile="sunset")
        deployment = _deployment(
            secrets_profiles={
                "sunset": SecretsProfile(wifi_ssid="Net", wifi_password='p"w'),
            },
            lab=LabDeskSettings(phaseb_server_url="http://192.168.1.2:8765"),
        )
        lines = deployment_kit_sdkconfig_lines(deployment, kit)
        self.assertIn('CONFIG_LAB02_SERVER_URL="http://192.168.1.2:8765"', lines)
        self.assertIn('CONFIG_LAB02_WIFI_SSID="Net"', lines)
        self.assertIn('CONFIG_LAB02_WIFI_PASSWORD="p\\"w"', lines)

    def test_write_invalidates_on_secret_change(self):
        kit = Kit("k", "e-a", secrets_profile="sunset")
        dep_a = _deployment(secrets_profiles={"sunset": SecretsProfile(wifi_ssid="A", wifi_password="1")})
        dep_b = _deployment(secrets_profiles={"sunset": SecretsProfile(wifi_ssid="B", wifi_password="1")})
        with tempfile.TemporaryDirectory() as raw:
            build = Path(raw) / "lab-build"
            build.mkdir()
            sdk = build / "sdkconfig"
            sdk.write_text("CONFIG_FOO=y\n", encoding="utf-8")
            write_deployment_kit_sdkconfig(build, kit, dep_a)
            self.assertTrue(deployment_kit_defaults_path(build, kit).is_file())
            self.assertFalse(sdk.is_file())
            sdk.write_text("CONFIG_FOO=y\n", encoding="utf-8")
            write_deployment_kit_sdkconfig(build, kit, dep_b)
            self.assertFalse(sdk.is_file())


if __name__ == "__main__":
    unittest.main()
