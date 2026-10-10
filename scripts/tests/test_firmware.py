import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from deployment_config import Deployment, DeploymentMeta, Endpoint, Kit
from device_targets import Port, select, bind
from firmware import digest, package_for, publish, build
from project_config import ProjectConfigError


def roster():
    return Deployment(Path("unused"), "local", DeploymentMeta("desk", "Desk"), (),
                      (Endpoint("e-a", "", "a", "desk"), Endpoint("e-b", "", "b", "desk")),
                      (Kit("k-a", "e-a", usb_serial="AA:11"), Kit("k-b", "e-b", usb_serial="BB:22")),
                      {"mazi": "e-a", "arlo": "e-b"})


class TargetTests(unittest.TestCase):
    def test_unbound_kit_cannot_be_inferred(self):
        from dataclasses import replace
        original = roster()
        deployment = replace(original, kits=(replace(original.kits[0], usb_serial=""), original.kits[1]))
        with self.assertRaises(ProjectConfigError):
            select(deployment, [Port("COM1", "AA11")], "mazi")

    def test_duplicate_roster_bindings_rejected(self):
        from dataclasses import replace
        original = roster()
        deployment = replace(original, kits=(original.kits[0], replace(original.kits[1], usb_serial="AA11")))
        with self.assertRaises(ProjectConfigError):
            select(deployment, [Port("COM1", "AA11")], "mazi")

    def test_multiple_connected_requires_name(self):
        with self.assertRaises(ProjectConfigError):
            select(roster(), [Port("COM1", "AA11"), Port("COM2", "BB22")])

    def test_alias_selects_current_port(self):
        targets = select(roster(), [Port("COM99", "aa11")], "mazi")
        self.assertEqual(targets[0][1].device, "COM99")

    def test_all_requires_every_kit(self):
        with self.assertRaises(ProjectConfigError):
            select(roster(), [Port("COM1", "AA11")], all_kits=True)

    def test_duplicate_usb_matches_rejected(self):
        with self.assertRaises(ProjectConfigError):
            select(roster(), [Port("COM1", "AA11"), Port("COM2", "AA11")], "mazi")

    def test_bind_rejects_other_kits_serial_before_writing(self):
        with self.assertRaises(ProjectConfigError):
            bind(roster(), "arlo", "COM1", [Port("COM1", "AA11")])


class PackageTests(unittest.TestCase):
    def test_named_flash_preflight_failure_never_writes(self):
        from firmware import flash
        with patch("firmware.package_for", return_value=(Path("unused"), {})), \
             patch("deployment_config.load_active_deployment", return_value=roster()), \
             patch("device_targets.ports", return_value=[Port("COM1", "AA11")]), \
             patch("firmware.run") as write:
            with self.assertRaises(ProjectConfigError):
                flash(object(), "", True)
            write.assert_not_called()

    def test_publish_and_detect_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staging = root / "staging"
            staging.mkdir()
            image = staging / "app.bin"
            image.write_bytes(b"original")
            manifest = {"document": "family-link.firmware-package/v1", "target": "esp32s3",
                        "idf_version": "v5.4.2", "images": [{"offset": "0x10000", "file": "app.bin"}],
                        "files": {"app.bin": digest(image)}}
            (staging / "manifest.json").write_text(json.dumps(manifest))
            publish(staging, root, "test-build")
            class Config:
                def path_value(self, key): return root
                def text(self, key): return "esp32s3" if key == "idf.target" else "v5.4.2"
            package, _ = package_for(Config())
            (package / "app.bin").write_bytes(b"tampered")
            with self.assertRaises(ProjectConfigError):
                package_for(Config())

    def test_build_failure_does_not_publish(self):
        from project_config import ProjectConfig
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pointer = root / "current.json"
            pointer.write_text('{"package":"previous"}')
            config = ProjectConfig(environ={"FIRMWARE_BUILD": str(root / "work"),
                                            "FIRMWARE_ARTIFACTS": str(root)})
            with patch("firmware.idf_environment", return_value=({}, ["idf.py"])), \
                 patch("firmware.run", side_effect=ProjectConfigError("build failed")):
                with self.assertRaises(ProjectConfigError):
                    build(config)
            self.assertEqual(pointer.read_text(), '{"package":"previous"}')


if __name__ == "__main__":
    unittest.main()
