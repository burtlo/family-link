from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
REPO = SCRIPTS.parent
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from deployment_config import (  # noqa: E402
    DeploymentConfigError,
    build_catalog,
    dump_snapshot,
    load_active_deployment,
    load_deployment,
    resolve_deployment_path,
    validate_document_schema,
)
from project_config import ProjectConfig  # noqa: E402

EXAMPLE = REPO / "config" / "deployment" / "example.yaml"
LOCAL = REPO / "config" / "deployment" / "local.yaml"


class DeploymentConfigTests(unittest.TestCase):
    def test_explicit_host_config_selects_roster_and_schema_together(self):
        with tempfile.TemporaryDirectory() as raw_dir:
            root = Path(raw_dir)
            schema = root / "reject.json"
            schema.write_text(json.dumps({"not": {}}), encoding="utf-8")
            config = ProjectConfig(environ={
                "CONFIG_DEPLOYMENT_LOCAL": str(root / "absent.yaml"),
                "CONFIG_DEPLOYMENT_EXAMPLE": str(EXAMPLE),
                "CONFIG_DEPLOYMENT_SCHEMA": str(schema),
            })
            with self.assertRaises(DeploymentConfigError):
                load_active_deployment(config)

    def test_committed_example_parses_and_validates(self) -> None:
        deployment = load_deployment(EXAMPLE, source="example")
        self.assertEqual("example-hangout", deployment.meta.id)
        self.assertEqual(2, len(deployment.endpoints))
        self.assertEqual(2, len(deployment.kits))
        self.assertEqual("endpoint-alpha", deployment.resolve_alias("alpha"))
        self.assertEqual("endpoint-beta", deployment.resolve_alias("beta"))

    def test_local_file_parses_when_present(self) -> None:
        if not LOCAL.is_file():
            self.skipTest("config/deployment/local.yaml not present")
        deployment = load_deployment(LOCAL, source="local")
        mazi_kit = next(kit for kit in deployment.kits if kit.id == "box-a")
        self.assertEqual("E8:F6:0A:A8:D2:98", mazi_kit.usb_serial)

    def test_resolve_prefers_local_over_example(self) -> None:
        if not LOCAL.is_file():
            self.skipTest("config/deployment/local.yaml not present")
        path, source = resolve_deployment_path(ProjectConfig(environ={}))
        self.assertEqual("local", source)
        self.assertEqual(LOCAL.resolve(), path.resolve())

    def test_resolve_falls_back_to_example_when_local_missing(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            ini = Path(raw_dir) / "test.ini"
            missing_local = Path(raw_dir) / "missing-local.yaml"
            ini.write_text(
                "\n".join(
                    [
                        "[project]",
                        "config_version = 1",
                        "id = family-link",
                        "name = Test",
                        "[python]",
                        "minimum = 3.9",
                        "venv = .venv",
                        "requirements = requirements.txt",
                        "[idf]",
                        "path = ~/esp/esp-idf",
                        "version = v5.4.2",
                        "target = esp32s3",
                        "skip = true",
                        "reinstall = false",
                        "[config.deployment]",
                        f"example = {EXAMPLE.relative_to(REPO).as_posix()}",
                        f"local = {missing_local.as_posix()}",
                        f"schema = {(REPO / 'config/deployment/deployment.schema.v1.json').relative_to(REPO).as_posix()}",
                    ]
                ),
                encoding="utf-8",
            )
            project = ProjectConfig(ini, environ={})
            path, source = resolve_deployment_path(project)
            self.assertEqual("example", source)
            self.assertEqual(EXAMPLE.resolve(), path.resolve())

    def test_duplicate_endpoint_token_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            path = Path(raw_dir) / "bad.yaml"
            path.write_text(
                EXAMPLE.read_text(encoding="utf-8").replace(
                    "change-me-beta",
                    "change-me-alpha",
                    1,
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(DeploymentConfigError, "duplicate endpoint token"):
                load_deployment(path)

    def test_kit_must_reference_known_endpoint(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            path = Path(raw_dir) / "bad.yaml"
            path.write_text(
                EXAMPLE.read_text(encoding="utf-8").replace(
                    "endpoint-alpha",
                    "endpoint-missing",
                    1,
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(DeploymentConfigError, "not a known endpoint"):
                load_deployment(path)

    def test_dump_redacts_non_placeholder_tokens(self) -> None:
        deployment = load_deployment(EXAMPLE, source="example")
        document = dump_snapshot(deployment)
        for row in document["endpoints"]:
            self.assertTrue(
                str(row["token"]).startswith("change-me")
                or str(row["token"]).startswith("[redacted")
            )

    def test_schema_rejects_missing_deployment_block(self) -> None:
        with self.assertRaisesRegex(DeploymentConfigError, "schema:"):
            validate_document_schema({"endpoints": [], "kits": []})

    def test_catalog_documents_semantic_rules(self) -> None:
        catalog = build_catalog()
        self.assertIn("semantic_rules", catalog)
        self.assertTrue(catalog["semantic_rules"])

    def test_cli_catalog_emits_json(self) -> None:
        completed = subprocess.run(
            [sys.executable, str(SCRIPTS / "deployment_config.py"), "catalog"],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertIn("schema_file", document)

    def test_cli_validate_example_file(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPTS / "deployment_config.py"),
                "--file",
                str(EXAMPLE),
                "validate",
            ],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        self.assertIn("valid:", completed.stdout)

    def test_cli_dump_emits_json(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPTS / "deployment_config.py"),
                "--file",
                str(EXAMPLE),
                "dump",
            ],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertEqual("family-link.deployment.snapshot/v1", document["document"])

    def test_active_load_via_project_defaults(self) -> None:
        deployment = load_active_deployment(ProjectConfig(environ={}))
        self.assertGreaterEqual(len(deployment.endpoints), 2)


if __name__ == "__main__":
    unittest.main()
