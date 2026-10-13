import sys
import tempfile
import unittest
import yaml
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lab02_workflow_config import (
    Lab02WorkflowConfigError,
    eligible_lan_ipv4,
    load_lab02_workflow_config,
)
from project_config import ProjectConfig
from lab_registry import load_registry_yaml


class Lab02WorkflowConfigTests(unittest.TestCase):
    def test_direct_loader_enforces_registry_schema_and_workflow_domain(self):
        source = Path(__file__).resolve().parents[2] / "config/labs.registry.yaml"
        original = load_registry_yaml(source)
        for key, value in (
            ("bind_port", 70000),
            ("fixture_size_default", 2147483648),
            ("scenario_default", "made-up"),
        ):
            with self.subTest(key=key):
                document = yaml.safe_load(yaml.safe_dump(original))
                document["labs"]["lab02"]["workflow"][key] = value
                with tempfile.TemporaryDirectory() as directory:
                    registry = Path(directory) / "registry.yaml"
                    registry.write_text(yaml.safe_dump(document), encoding="utf-8")
                    project = ProjectConfig(environ={
                        "LABS_REGISTRY": str(registry),
                        "LABS_LAB02_CLIENT_URL": "",
                    })
                    with self.assertRaises(Lab02WorkflowConfigError) as raised:
                        load_lab02_workflow_config(project, interface_addresses=["10.0.0.2"])
                    self.assertNotIn(str(value), str(raised.exception))

    def test_derives_url_from_one_eligible_interface(self):
        config = load_lab02_workflow_config(
            ProjectConfig(environ={"LABS_LAB02_CLIENT_URL": ""}),
            interface_addresses=["127.0.0.1", "169.254.2.1", "10.20.0.3"],
        )
        self.assertEqual(config.bind_host, "0.0.0.0")
        self.assertEqual(config.bind_port, 8765)
        self.assertEqual(config.client_url, "http://10.20.0.3:8765")
        self.assertEqual(config.fixture_size_default, 64)
        self.assertEqual(config.scenario_default, "nominal")

    def test_filters_ineligible_and_duplicate_interfaces(self):
        self.assertEqual(
            eligible_lan_ipv4(
                ["10.1.2.3", "10.1.2.3", "127.0.0.1", "169.254.1.2", "0.0.0.0"]
            ),
            ("10.1.2.3",),
        )

    def test_ambiguous_interfaces_fail(self):
        with self.assertRaisesRegex(Lab02WorkflowConfigError, "ambiguous"):
            load_lab02_workflow_config(
                ProjectConfig(environ={"LABS_LAB02_CLIENT_URL": ""}),
                interface_addresses=["10.0.0.2", "10.0.0.3"],
            )

    def test_explicit_url_must_match_local_interface_and_bind_port(self):
        config = load_lab02_workflow_config(
            ProjectConfig(environ={"LABS_LAB02_CLIENT_URL": "http://10.4.0.7:8765"}),
            interface_addresses=["10.4.0.7"],
        )
        self.assertEqual(config.bind_host, "0.0.0.0")
        self.assertEqual(config.client_url, "http://10.4.0.7:8765")
        for invalid in (
            "http://user:password@10.4.0.7:8765",
            "http://10.4.0.7:9999",
            "http://10.4.0.8:8765",
            "http://10.4.0.7:8765/path?token=hidden",
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(Lab02WorkflowConfigError) as raised:
                    load_lab02_workflow_config(
                        ProjectConfig(environ={"LABS_LAB02_CLIENT_URL": invalid}),
                        interface_addresses=["10.4.0.7"],
                    )
                self.assertNotIn(invalid, str(raised.exception))


if __name__ == "__main__":
    unittest.main()
