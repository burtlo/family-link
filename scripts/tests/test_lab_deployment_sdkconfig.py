import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from deployment_config import (
    Deployment,
    DeploymentMeta,
    Kit,
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
        )
        lines = deployment_kit_sdkconfig_lines(
            deployment, kit, "http://192.168.1.2:8765"
        )
        self.assertIn('CONFIG_LAB02_SERVER_URL="http://192.168.1.2:8765"', lines)
        self.assertIn('CONFIG_LAB02_WIFI_SSID="Net"', lines)
        self.assertIn('CONFIG_LAB02_WIFI_PASSWORD="p\\"w"', lines)

    def test_phaseb_overlay_freezes_fixture_scenario_and_harness(self):
        kit = Kit("k", "endpoint-k", secrets_profile="desk")
        deployment = _deployment(secrets_profiles={"desk": SecretsProfile(wifi_ssid="A", wifi_password="1")})
        lines = deployment_kit_sdkconfig_lines(deployment, kit, "http://192.168.1.2:8765",
            fixture_size=64, scenario="S-push", scripted=True)
        self.assertIn("CONFIG_LAB02_FIXTURE_64=y", lines)
        self.assertIn('CONFIG_LAB02_SCENARIO="S-push"', lines)
        self.assertIn("CONFIG_LAB_AUTO_HARNESS=y", lines)

    def test_write_invalidates_on_secret_change(self):
        kit = Kit("k", "e-a", secrets_profile="sunset")
        dep_a = _deployment(secrets_profiles={"sunset": SecretsProfile(wifi_ssid="A", wifi_password="1")})
        dep_b = _deployment(secrets_profiles={"sunset": SecretsProfile(wifi_ssid="B", wifi_password="1")})
        with tempfile.TemporaryDirectory() as raw:
            build = Path(raw) / "lab-build"
            build.mkdir()
            sdk = build / "sdkconfig"
            sdk.write_text("CONFIG_FOO=y\n", encoding="utf-8")
            write_deployment_kit_sdkconfig(build, kit, dep_a, "http://192.168.1.2:8765")
            self.assertTrue(deployment_kit_defaults_path(build, kit).is_file())
            self.assertFalse(sdk.is_file())
            sdk.write_text("CONFIG_FOO=y\n", encoding="utf-8")
            write_deployment_kit_sdkconfig(build, kit, dep_b, "http://192.168.1.3:8765")
            self.assertFalse(sdk.is_file())

    def test_write_invalidates_when_client_url_changes(self):
        kit = Kit("k", "e-a", secrets_profile="desk")
        deployment = _deployment(
            secrets_profiles={"desk": SecretsProfile(wifi_ssid="A", wifi_password="1")}
        )
        with tempfile.TemporaryDirectory() as raw:
            build = Path(raw) / "lab-build"
            build.mkdir()
            write_deployment_kit_sdkconfig(
                build, kit, deployment, "http://192.168.1.2:8765"
            )
            sdk = build / "sdkconfig"
            sdk.write_text("CONFIG_FOO=y\n", encoding="utf-8")
            write_deployment_kit_sdkconfig(
                build, kit, deployment, "http://192.168.1.3:8765"
            )
            self.assertFalse(sdk.exists())


if __name__ == "__main__":
    unittest.main()
