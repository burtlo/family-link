#!/usr/bin/env python3
"""Load Family Link host mechanics from ``config/host.defaults.ini``.

The file uses lowercase INI sections and keys. Each value may be overridden by
an environment variable formed from its fully qualified name:

    [idf] path                         -> IDF_PATH
    [command.install] timeout_seconds -> COMMAND_INSTALL_TIMEOUT_SECONDS

``PROJECT_CONFIG`` selects another INI file. Relative paths in that variable
and in configuration values are resolved from the repository root.
"""

from __future__ import annotations

import argparse
import configparser
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional, Sequence, Union

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG_PATH = ROOT / "config" / "host.defaults.ini"
CONFIG_PATH_ENV = "PROJECT_CONFIG"
SNAPSHOT_DOCUMENT = "family-link.project-config.snapshot/v1"
DOCS_RELATIVE = "docs/standards/project-configuration.md"

_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)*$")
_TRUE_VALUES = frozenset(("1", "true", "yes", "on"))
_FALSE_VALUES = frozenset(("0", "false", "no", "off"))

# Code-defined types for settings consumed by scripts (catalog / validation).
_SETTING_SPECS: dict[str, dict[str, object]] = {
    "project.config_version": {"type": "integer", "required": True, "minimum": 1},
    "project.id": {"type": "string", "required": True},
    "project.name": {"type": "string", "required": True},
    "python.minimum": {"type": "version", "required": True},
    "python.venv": {"type": "path", "required": True},
    "python.requirements": {"type": "path", "required": True},
    "idf.path": {"type": "path", "required": True},
    "idf.version": {"type": "string", "required": True},
    "idf.target": {"type": "string", "required": True},
    "idf.skip": {"type": "boolean", "required": True},
    "idf.reinstall": {"type": "boolean", "required": True},
    "config.deployment.example": {"type": "path", "required": True},
    "config.deployment.local": {"type": "path", "required": True},
    "config.deployment.schema": {"type": "path", "required": True},
}

_COMMAND_SECTION_KEYS: dict[str, dict[str, object]] = {
    "argv": {"type": "json_string_array", "required": True},
    "cwd": {"type": "repo_relative_path", "required": False, "default": "."},
    "timeout_seconds": {"type": "integer", "required": False, "default": 900, "minimum": 1},
}

_FLOW_SECTION_KEYS: dict[str, dict[str, object]] = {
    "commands": {"type": "command_name_list", "required": True},
}


class ProjectConfigError(Exception):
    """Invalid, missing, or inconsistent project configuration."""

    def __init__(self, message: str, exit_code: int = 1) -> None:
        super().__init__(message)
        self.exit_code = exit_code


@dataclass(frozen=True)
class Command:
    """One agent- and human-runnable repository command."""

    name: str
    argv: tuple[str, ...]
    cwd: Path
    timeout_seconds: int


def environment_name(qualified_name: str) -> str:
    """Return the uppercase environment override for a qualified INI key."""
    _validate_name(qualified_name, "qualified setting")
    return qualified_name.upper().replace(".", "_")


def _validate_name(name: str, label: str) -> None:
    if not _NAME_RE.fullmatch(name):
        raise ProjectConfigError(
            f"{label} must be lowercase dotted identifiers, got {name!r}"
        )


def _config_path(
    path: Optional[Union[str, Path]],
    environ: Mapping[str, str],
) -> Path:
    raw = path if path is not None else environ.get(CONFIG_PATH_ENV)
    if raw is None or not str(raw).strip():
        return DEFAULT_CONFIG_PATH
    resolved = Path(str(raw)).expanduser()
    if not resolved.is_absolute():
        resolved = ROOT / resolved
    return resolved.resolve()


class ProjectConfig:
    """Typed view of project defaults with deterministic environment overrides."""

    def __init__(
        self,
        path: Optional[Union[str, Path]] = None,
        *,
        environ: Optional[Mapping[str, str]] = None,
    ) -> None:
        self.environ = os.environ if environ is None else environ
        self.path = _config_path(path, self.environ)
        self._parser = self._load()

    def _load(self) -> configparser.ConfigParser:
        parser = configparser.ConfigParser(interpolation=None)
        parser.optionxform = str
        try:
            with self.path.open("r", encoding="utf-8") as handle:
                parser.read_file(handle)
        except FileNotFoundError as exc:
            raise ProjectConfigError(
                f"project configuration not found: {self.path} "
                f"(override with {CONFIG_PATH_ENV})"
            ) from exc
        except (OSError, configparser.Error) as exc:
            raise ProjectConfigError(
                f"could not load project configuration {self.path}: {exc}"
            ) from exc

        for section in parser.sections():
            _validate_name(section, "section name")
            for key in parser[section]:
                _validate_name(key, f"key in [{section}]")
        return parser

    @staticmethod
    def _split(qualified_name: str) -> tuple[str, str]:
        _validate_name(qualified_name, "qualified setting")
        section, separator, key = qualified_name.rpartition(".")
        if not separator:
            raise ProjectConfigError(
                f"setting must include a section prefix, got {qualified_name!r}"
            )
        return section, key

    def has(self, qualified_name: str) -> bool:
        section, key = self._split(qualified_name)
        return (
            environment_name(qualified_name) in self.environ
            or self._parser.has_option(section, key)
        )

    def text(
        self,
        qualified_name: str,
        *,
        default: Optional[str] = None,
        required: bool = False,
    ) -> str:
        section, key = self._split(qualified_name)
        env_name = environment_name(qualified_name)
        if env_name in self.environ:
            value = self.environ[env_name]
        elif self._parser.has_option(section, key):
            value = self._parser.get(section, key)
        elif default is not None:
            value = default
        elif required:
            raise ProjectConfigError(
                f"missing required setting {qualified_name} "
                f"(or environment variable {env_name})"
            )
        else:
            return ""

        value = str(value).strip()
        if required and not value:
            raise ProjectConfigError(
                f"required setting {qualified_name} is empty "
                f"(environment variable {env_name})"
            )
        return value

    def boolean(self, qualified_name: str, *, default: Optional[bool] = None) -> bool:
        fallback = None if default is None else ("true" if default else "false")
        value = self.text(
            qualified_name,
            default=fallback,
            required=default is None,
        ).lower()
        if value in _TRUE_VALUES:
            return True
        if value in _FALSE_VALUES:
            return False
        raise ProjectConfigError(
            f"{qualified_name} must be one of "
            f"{sorted(_TRUE_VALUES | _FALSE_VALUES)}, got {value!r}"
        )

    def integer(
        self,
        qualified_name: str,
        *,
        default: Optional[int] = None,
        minimum: Optional[int] = None,
    ) -> int:
        raw = self.text(
            qualified_name,
            default=None if default is None else str(default),
            required=default is None,
        )
        try:
            value = int(raw)
        except ValueError as exc:
            raise ProjectConfigError(
                f"{qualified_name} must be an integer, got {raw!r}"
            ) from exc
        if minimum is not None and value < minimum:
            raise ProjectConfigError(
                f"{qualified_name} must be at least {minimum}, got {value}"
            )
        return value

    def path_value(
        self,
        qualified_name: str,
        *,
        required: bool = True,
    ) -> Path:
        raw = self.text(qualified_name, required=required)
        value = Path(raw).expanduser()
        if not value.is_absolute():
            value = ROOT / value
        return value.resolve()

    def names(
        self,
        qualified_name: str,
        *,
        required: bool = False,
    ) -> tuple[str, ...]:
        raw = self.text(qualified_name, required=required)
        if not raw:
            return ()
        values = tuple(item.strip() for item in raw.split(",") if item.strip())
        for value in values:
            _validate_name(value, f"value in {qualified_name}")
        return values

    def version_tuple(self, qualified_name: str) -> tuple[int, ...]:
        raw = self.text(qualified_name, required=True)
        try:
            value = tuple(int(part) for part in raw.split("."))
        except ValueError as exc:
            raise ProjectConfigError(
                f"{qualified_name} must be a dotted numeric version, got {raw!r}"
            ) from exc
        if not value:
            raise ProjectConfigError(f"{qualified_name} must not be empty")
        return value

    def command_names(self) -> tuple[str, ...]:
        prefix = "command."
        names = {
            section[len(prefix) :].split(".", 1)[0]
            for section in self._parser.sections()
            if section.startswith(prefix)
        }
        return tuple(sorted(names))

    def command(self, name: str) -> Command:
        _validate_name(name, "command name")
        section = f"command.{name}"
        if not self._parser.has_section(section):
            raise ProjectConfigError(f"unknown command {name!r}")

        argv_raw = self.text(f"{section}.argv", required=True)
        try:
            argv_value = json.loads(argv_raw)
        except json.JSONDecodeError as exc:
            raise ProjectConfigError(
                f"{section}.argv must be a JSON array of strings: {exc}"
            ) from exc
        if (
            not isinstance(argv_value, list)
            or not argv_value
            or any(not isinstance(arg, str) or not arg for arg in argv_value)
        ):
            raise ProjectConfigError(
                f"{section}.argv must be a non-empty JSON array of non-empty strings"
            )

        cwd_raw = self.text(f"{section}.cwd", default=".")
        cwd_relative = Path(cwd_raw)
        if cwd_relative.is_absolute() or ".." in cwd_relative.parts:
            raise ProjectConfigError(
                f"{section}.cwd must stay within the repository, got {cwd_raw!r}"
            )
        cwd = (ROOT / cwd_relative).resolve()
        timeout = self.integer(
            f"{section}.timeout_seconds",
            default=900,
            minimum=1,
        )
        return Command(name, tuple(argv_value), cwd, timeout)

    def flow(self, name: str) -> tuple[Command, ...]:
        _validate_name(name, "flow name")
        command_names = self.names(f"flow.{name}.commands", required=True)
        if not command_names:
            raise ProjectConfigError(f"flow.{name}.commands must not be empty")
        return tuple(self.command(command_name) for command_name in command_names)

    def qualified_setting_names(self) -> tuple[str, ...]:
        names: list[str] = []
        for section in self._parser.sections():
            for key in self._parser.options(section):
                names.append(f"{section}.{key}")
        return tuple(sorted(names))

    def setting_source(self, qualified_name: str) -> str:
        """Return ``environment`` or ``file`` for where ``text()`` reads the value."""
        env_name = environment_name(qualified_name)
        if env_name in self.environ:
            return "environment"
        return "file"

    def _setting_type(self, qualified_name: str) -> str:
        spec = _SETTING_SPECS.get(qualified_name)
        if spec is not None:
            return str(spec["type"])
        if qualified_name.endswith(".argv"):
            return "json_string_array"
        if qualified_name.endswith(".timeout_seconds"):
            return "integer"
        if qualified_name.endswith(".cwd"):
            return "repo_relative_path"
        if qualified_name.endswith(".commands"):
            return "command_name_list"
        return "string"

    def _resolved_setting_value(self, qualified_name: str) -> object:
        kind = self._setting_type(qualified_name)
        if kind == "boolean":
            return self.boolean(qualified_name)
        if kind == "integer":
            default = 900 if qualified_name.endswith(".timeout_seconds") else None
            minimum = 1 if (
                qualified_name.endswith(".timeout_seconds")
                or qualified_name == "project.config_version"
            ) else None
            return self.integer(qualified_name, default=default, minimum=minimum)
        if kind == "version":
            return ".".join(str(part) for part in self.version_tuple(qualified_name))
        if kind == "path":
            return str(self.path_value(qualified_name))
        if kind == "command_name_list":
            return list(self.names(qualified_name, required=True))
        if kind == "json_string_array":
            raw = self.text(qualified_name, required=True)
            return json.loads(raw)
        return self.text(qualified_name)

    def build_catalog(self) -> dict[str, object]:
        """Machine-readable contract (types, env vars, command/flow shapes)."""
        settings: dict[str, object] = {}
        for qualified_name in self.qualified_setting_names():
            spec = dict(_SETTING_SPECS.get(qualified_name, {"type": "string", "required": False}))
            spec["environment_variable"] = environment_name(qualified_name)
            settings[qualified_name] = spec

        flow_names = tuple(
            section.removeprefix("flow.")
            for section in self._parser.sections()
            if section.startswith("flow.")
        )
        return {
            "settings": settings,
            "command_section": {
                "name_pattern": "command.<name>",
                "keys": _COMMAND_SECTION_KEYS,
                "defined": list(self.command_names()),
            },
            "flow_section": {
                "name_pattern": "flow.<name>",
                "keys": _FLOW_SECTION_KEYS,
                "defined": list(flow_names),
            },
        }

    @staticmethod
    def build_meta(config_file: Path) -> dict[str, object]:
        return {
            "config_file": str(config_file),
            "default_config_file": str(DEFAULT_CONFIG_PATH),
            "config_path_env": CONFIG_PATH_ENV,
            "override_rule": (
                "Each qualified setting name may be overridden by an environment "
                "variable: uppercase the name and replace '.' with '_'."
            ),
            "repository_config_format": "ini",
            "product_config_format": "yaml",
            "product_config_note": (
                "Product roster YAML lives under config/deployment/; paths are "
                "in [config.deployment] on the host INI. Use make config.deployment."
            ),
            "docs": str(ROOT / DOCS_RELATIVE),
        }

    def dump_snapshot(self) -> dict[str, object]:
        """Agent-oriented snapshot: catalog, resolved values, and provenance."""
        self.validate()
        resolved_settings: dict[str, object] = {}
        provenance_settings: dict[str, str] = {}
        for qualified_name in self.qualified_setting_names():
            resolved_settings[qualified_name] = self._resolved_setting_value(qualified_name)
            provenance_settings[qualified_name] = self.setting_source(qualified_name)

        commands = {
            name: json.loads(_command_json(self.command(name)))
            for name in self.command_names()
        }
        flows: dict[str, object] = {}
        for section in self._parser.sections():
            if not section.startswith("flow."):
                continue
            flow_name = section.removeprefix("flow.")
            flows[flow_name] = {
                "command_names": list(self.names(f"{section}.commands", required=True)),
                "commands": [
                    json.loads(_command_json(command))
                    for command in self.flow(flow_name)
                ],
            }

        return {
            "document": SNAPSHOT_DOCUMENT,
            "meta": self.build_meta(self.path),
            "catalog": self.build_catalog(),
            "resolved": {
                "settings": resolved_settings,
                "commands": commands,
                "flows": flows,
            },
            "provenance": {
                "settings": provenance_settings,
            },
        }

    def dump_resolved(self) -> dict[str, object]:
        """Backward-compatible alias for :meth:`dump_snapshot`."""
        return self.dump_snapshot()

    def validate(self) -> None:
        """Validate core settings, all commands, and all flow references."""
        if self.integer("project.config_version", minimum=1) != 1:
            raise ProjectConfigError("only project.config_version=1 is supported")
        self.text("project.id", required=True)
        self.text("project.name", required=True)
        self.version_tuple("python.minimum")
        self.path_value("python.venv")
        self.path_value("python.requirements")
        self.path_value("idf.path")
        self.text("idf.version", required=True)
        self.text("idf.target", required=True)
        self.boolean("idf.skip")
        self.boolean("idf.reinstall")
        self.path_value("config.deployment.example")
        self.path_value("config.deployment.local")
        self.path_value("config.deployment.schema")

        for name in self.command_names():
            self.command(name)
        for section in self._parser.sections():
            if section.startswith("flow."):
                self.flow(section.removeprefix("flow."))


def _command_json(command: Command) -> str:
    return json.dumps(
        {
            "name": command.name,
            "argv": list(command.argv),
            "cwd": str(command.cwd),
            "timeout_seconds": command.timeout_seconds,
        },
        indent=2,
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Read and validate project configuration.")
    parser.add_argument(
        "--config",
        type=Path,
        help=f"INI path (default: {DEFAULT_CONFIG_PATH.name}; env: {CONFIG_PATH_ENV})",
    )
    subparsers = parser.add_subparsers(dest="action", required=True)
    subparsers.add_parser(
        "dump",
        help="print catalog + resolved values + provenance snapshot as JSON",
    )
    subparsers.add_parser(
        "catalog",
        help="print machine-readable catalog only (requires valid config file)",
    )
    subparsers.add_parser("validate", help="validate only; print config path on success")
    get_parser = subparsers.add_parser("get", help="print one resolved setting")
    get_parser.add_argument("name")
    command_parser = subparsers.add_parser("command", help="print one command as JSON")
    command_parser.add_argument("name")
    flow_parser = subparsers.add_parser("flow", help="print a flow's commands as JSON")
    flow_parser.add_argument("name")
    args = parser.parse_args(argv)

    try:
        config = ProjectConfig(args.config)
        if args.action == "dump":
            print(json.dumps(config.dump_snapshot(), indent=2, sort_keys=True))
        elif args.action == "catalog":
            config.validate()
            print(
                json.dumps(
                    {
                        "document": SNAPSHOT_DOCUMENT,
                        "meta": ProjectConfig.build_meta(config.path),
                        "catalog": config.build_catalog(),
                    },
                    indent=2,
                    sort_keys=True,
                )
            )
        elif args.action == "validate":
            config.validate()
            print(f"valid: {config.path}")
        elif args.action == "get":
            print(config.text(args.name, required=True))
        elif args.action == "command":
            print(_command_json(config.command(args.name)))
        elif args.action == "flow":
            print(json.dumps([json.loads(_command_json(item)) for item in config.flow(args.name)], indent=2))
    except ProjectConfigError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code
    return 0


if __name__ == "__main__":
    sys.exit(main())
