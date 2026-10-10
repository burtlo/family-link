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

ALT_CONFIG = REPO / "scripts/tests/fixtures/alt-project.defaults.ini"


def _document_from_process_output(stdout: str, stderr: str) -> dict:
    combined = stdout + stderr
    start = combined.find("{")
    end = combined.rfind("}")
    if start < 0 or end < start:
        raise AssertionError(f"no JSON document in process output:\n{combined}")
    return json.loads(combined[start : end + 1])


from project_config import (  # noqa: E402
    CONFIG_PATH_ENV,
    DEFAULT_CONFIG_PATH,
    SNAPSHOT_DOCUMENT,
    ProjectConfig,
    ProjectConfigError,
    environment_name,
)


class ProjectConfigTests(unittest.TestCase):
    def test_default_configuration_is_valid(self) -> None:
        config = ProjectConfig(environ={})

        config.validate()

        self.assertEqual(DEFAULT_CONFIG_PATH, config.path)
        self.assertEqual("family-link", config.text("project.id"))
        self.assertEqual(("test",), tuple(item.name for item in config.flow("implementation")))
        self.assertEqual(
            REPO / "config" / "deployment" / "example.yaml",
            config.path_value("config.deployment.example"),
        )

    def test_uppercase_qualified_environment_variable_overrides_value(self) -> None:
        config = ProjectConfig(
            environ={
                "IDF_TARGET": "esp32",
                "COMMAND_TEST_TIMEOUT_SECONDS": "321",
            }
        )

        self.assertEqual("esp32", config.text("idf.target"))
        self.assertEqual(321, config.command("test").timeout_seconds)

    def test_config_path_environment_override_is_repo_relative(self) -> None:
        with tempfile.TemporaryDirectory(dir=REPO) as raw_dir:
            temporary = Path(raw_dir)
            path = temporary / "custom.ini"
            path.write_text(DEFAULT_CONFIG_PATH.read_text(encoding="utf-8"), encoding="utf-8")
            relative = path.relative_to(REPO)

            config = ProjectConfig(environ={CONFIG_PATH_ENV: str(relative)})

            self.assertEqual(path.resolve(), config.path)

    def test_command_argv_is_structured(self) -> None:
        command = ProjectConfig(environ={}).command("install")

        self.assertEqual(("make", "install"), command.argv)
        self.assertEqual(REPO, command.cwd)
        self.assertGreater(command.timeout_seconds, 0)

    def test_unknown_flow_command_fails_validation(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            path = Path(raw_dir) / "invalid.ini"
            source = DEFAULT_CONFIG_PATH.read_text(encoding="utf-8")
            path.write_text(
                source.replace("commands = test", "commands = missing", 1),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ProjectConfigError, "unknown command"):
                ProjectConfig(path, environ={}).validate()

    def test_uppercase_file_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw_dir:
            path = Path(raw_dir) / "invalid.ini"
            path.write_text("[idf]\nTarget = esp32s3\n", encoding="utf-8")

            with self.assertRaisesRegex(ProjectConfigError, "must be lowercase"):
                ProjectConfig(path, environ={})

    def test_environment_name_uses_all_prefix_groups(self) -> None:
        self.assertEqual(
            "COMMAND_INSTALL_TIMEOUT_SECONDS",
            environment_name("command.install.timeout_seconds"),
        )

    def test_snapshot_separates_catalog_resolved_and_provenance(self) -> None:
        config = ProjectConfig(environ={"IDF_TARGET": "esp32"})
        document = config.dump_snapshot()

        self.assertEqual(SNAPSHOT_DOCUMENT, document["document"])
        self.assertEqual(str(DEFAULT_CONFIG_PATH), document["meta"]["config_file"])
        self.assertEqual("esp32", document["resolved"]["settings"]["idf.target"])
        self.assertEqual("environment", document["provenance"]["settings"]["idf.target"])
        self.assertEqual(
            "IDF_TARGET",
            document["catalog"]["settings"]["idf.target"]["environment_variable"],
        )
        self.assertIn("install", document["resolved"]["commands"])
        self.assertIn("implementation", document["resolved"]["flows"])
        self.assertNotIn("value", document["resolved"]["settings"]["idf.target"])

    def test_catalog_lists_command_and_flow_shapes(self) -> None:
        catalog = ProjectConfig(environ={}).build_catalog()

        self.assertEqual("path", catalog["settings"]["idf.path"]["type"])
        self.assertIn("install", catalog["command_section"]["defined"])
        self.assertIn("setup", catalog["flow_section"]["defined"])

    def test_dump_cli_emits_valid_json_with_env_override(self) -> None:
        completed = subprocess.run(
            [sys.executable, str(SCRIPTS / "project_config.py"), "dump"],
            cwd=REPO,
            env={**os.environ, "IDF_TARGET": "esp32c3"},
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertEqual("esp32c3", document["resolved"]["settings"]["idf.target"])

    def test_dump_cli_with_separate_config_file(self) -> None:
        relative = ALT_CONFIG.relative_to(REPO)
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPTS / "project_config.py"),
                "--config",
                str(relative),
                "dump",
            ],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertEqual(str(ALT_CONFIG.resolve()), document["meta"]["config_file"])
        self.assertEqual(
            "Alt Family Link",
            document["resolved"]["settings"]["project.name"],
        )

    def test_make_config_returns_snapshot_json(self) -> None:
        completed = subprocess.run(
            ["make", "config"],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = _document_from_process_output(completed.stdout, completed.stderr)
        self.assertEqual(SNAPSHOT_DOCUMENT, document["document"])

    def test_make_config_with_config_variable(self) -> None:
        config_arg = ALT_CONFIG.relative_to(REPO).as_posix()
        completed = subprocess.run(
            ["make", "config", f"CONFIG={config_arg}"],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = _document_from_process_output(completed.stdout, completed.stderr)
        self.assertEqual(str(ALT_CONFIG.resolve()), document["meta"]["config_file"])
        self.assertEqual(
            "Alt Family Link",
            document["resolved"]["settings"]["project.name"],
        )

    def test_project_config_env_matches_make_loader(self) -> None:
        """Same loader path as ``make config`` when PROJECT_CONFIG and env are set."""
        env = os.environ.copy()
        env["PROJECT_CONFIG"] = ALT_CONFIG.relative_to(REPO).as_posix()
        env["IDF_SKIP"] = "true"
        completed = subprocess.run(
            [sys.executable, str(SCRIPTS / "project_config.py"), "dump"],
            cwd=REPO,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertTrue(document["resolved"]["settings"]["idf.skip"])
        self.assertEqual("environment", document["provenance"]["settings"]["idf.skip"])


if __name__ == "__main__":
    unittest.main()
