#!/usr/bin/env python3
"""Load and validate Family Link deployment YAML (endpoints, kits, aliases).

Paths come from ``config/host.defaults.ini`` ``[config.deployment]``. When
``config/deployment/local.yaml`` exists it wins over ``config/deployment/example.yaml``.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Optional, Sequence

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SCHEMA_PATH = ROOT / "config" / "deployment" / "deployment.schema.v1.json"
SCHEMA_DOCUMENT = "family-link.deployment.schema/v1"
SNAPSHOT_DOCUMENT = "family-link.deployment.snapshot/v1"
DOCS_RELATIVE = "docs/standards/deployment-configuration.md"
_PLACEHOLDER_TOKEN_RE = re.compile(r"^change-me(-[a-z0-9-]+)?$", re.IGNORECASE)

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML missing. Run: make install") from exc

try:
    import jsonschema
except ImportError as exc:  # pragma: no cover
    raise SystemExit("jsonschema missing. Run: make install") from exc

from project_config import ProjectConfig, ProjectConfigError


class DeploymentConfigError(ProjectConfigError):
    """Invalid or inconsistent deployment configuration."""


@dataclass(frozen=True)
class DeploymentMeta:
    id: str
    name: str


@dataclass(frozen=True)
class User:
    id: str
    name: str
    pin: str
    web_admin: bool = False
    web_password: str = ""


@dataclass(frozen=True)
class Endpoint:
    id: str
    name: str
    token: str
    hangout_id: str
    peer: str = ""


@dataclass(frozen=True)
class Kit:
    id: str
    endpoint_id: str
    secrets_profile: str = ""
    usb_serial: str = ""


@dataclass(frozen=True)
class Deployment:
    path: Path
    source: str
    meta: DeploymentMeta
    users: tuple[User, ...]
    endpoints: tuple[Endpoint, ...]
    kits: tuple[Kit, ...]
    aliases: dict[str, str]

    def endpoint_by_id(self, endpoint_id: str) -> Endpoint | None:
        for item in self.endpoints:
            if item.id == endpoint_id:
                return item
        return None

    def resolve_alias(self, key: str) -> str | None:
        normalized = key.strip().lower()
        if not normalized:
            return None
        target = self.aliases.get(normalized)
        if target is None:
            return None
        if self.endpoint_by_id(target) is None:
            return None
        return target


def _repo_path(raw: str, environ: Mapping[str, str]) -> Path:
    resolved = Path(raw).expanduser()
    if not resolved.is_absolute():
        resolved = ROOT / resolved
    return resolved.resolve()


def resolve_schema_path(
    project: ProjectConfig | None = None,
    *,
    environ: Mapping[str, str] | None = None,
) -> Path:
    cfg = project if project is not None else ProjectConfig(environ=environ)
    return _repo_path(cfg.text("config.deployment.schema", required=True), cfg.environ)


def load_schema_document(path: Path | None = None) -> dict[str, Any]:
    dest = path if path is not None else DEFAULT_SCHEMA_PATH
    try:
        return json.loads(dest.read_text(encoding="utf-8"))
    except OSError as exc:
        raise DeploymentConfigError(f"could not read schema {dest}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise DeploymentConfigError(f"invalid JSON schema in {dest}: {exc}") from exc


def validate_document_schema(
    document: dict[str, Any],
    *,
    schema_path: Path | None = None,
    project: ProjectConfig | None = None,
) -> None:
    path = schema_path if schema_path is not None else resolve_schema_path(project)
    schema = load_schema_document(path)
    try:
        jsonschema.validate(instance=document, schema=schema)
    except jsonschema.ValidationError as exc:
        location = "/".join(str(part) for part in exc.absolute_path)
        detail = exc.message
        if location:
            raise DeploymentConfigError(f"schema: {location}: {detail}") from exc
        raise DeploymentConfigError(f"schema: {detail}") from exc


def build_catalog(schema_path: Path | None = None) -> dict[str, object]:
    path = schema_path if schema_path is not None else DEFAULT_SCHEMA_PATH
    return {
        "document": SCHEMA_DOCUMENT,
        "schema_file": str(path.resolve()),
        "schema_id": SCHEMA_DOCUMENT,
        "yaml_format": "deployment roster v1",
        "structural_validation": "jsonschema against deployment.schema",
        "semantic_rules": [
            "endpoint ids and tokens are unique within the file",
            "aliases values must name a defined endpoint id",
            "kit endpoint_id must name a defined endpoint id",
            "every endpoint id has exactly one kit (kit endpoint_id set equals endpoint ids)",
            "endpoint peer, when set, must name another endpoint id in the same file",
        ],
        "docs": str(ROOT / DOCS_RELATIVE),
    }


def resolve_deployment_path(
    project: ProjectConfig | None = None,
    *,
    environ: Mapping[str, str] | None = None,
) -> tuple[Path, str]:
    """Return (path, source) where source is ``local`` or ``example``."""
    cfg = project if project is not None else ProjectConfig(environ=environ)
    local = _repo_path(cfg.text("config.deployment.local", required=True), cfg.environ)
    example = _repo_path(cfg.text("config.deployment.example", required=True), cfg.environ)
    if local.is_file():
        return local, "local"
    if example.is_file():
        return example, "example"
    raise DeploymentConfigError(
        f"deployment file not found: tried {local} then {example}"
    )


def _require_mapping(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DeploymentConfigError(f"{label} must be a mapping")
    return value


def _optional_str(row: dict[str, Any], key: str, default: str = "") -> str:
    raw = row.get(key, default)
    if raw is None:
        return default
    return str(raw).strip()


def _parse_users(raw: Any) -> tuple[User, ...]:
    if raw is None:
        return ()
    if not isinstance(raw, list):
        raise DeploymentConfigError("users must be a list")
    out: list[User] = []
    seen: set[str] = set()
    for index, row in enumerate(raw):
        if not isinstance(row, dict):
            raise DeploymentConfigError(f"users[{index}] must be a mapping")
        user_id = _optional_str(row, "id")
        if not user_id:
            raise DeploymentConfigError(f"users[{index}].id is required")
        if user_id in seen:
            raise DeploymentConfigError(f"duplicate user id {user_id!r}")
        seen.add(user_id)
        out.append(
            User(
                id=user_id,
                name=_optional_str(row, "name", user_id),
                pin=_optional_str(row, "pin"),
                web_admin=bool(row.get("web_admin", False)),
                web_password=_optional_str(row, "web_password"),
            )
        )
    return tuple(out)


def _parse_endpoints(raw: Any) -> tuple[Endpoint, ...]:
    if raw is None:
        return ()
    if not isinstance(raw, list):
        raise DeploymentConfigError("endpoints must be a list")
    out: list[Endpoint] = []
    seen_ids: set[str] = set()
    seen_tokens: set[str] = set()
    for index, row in enumerate(raw):
        if not isinstance(row, dict):
            raise DeploymentConfigError(f"endpoints[{index}] must be a mapping")
        endpoint_id = _optional_str(row, "id")
        token = _optional_str(row, "token")
        hangout_id = _optional_str(row, "hangout_id")
        if not endpoint_id:
            raise DeploymentConfigError(f"endpoints[{index}].id is required")
        if not token:
            raise DeploymentConfigError(f"endpoints[{index}].token is required")
        if not hangout_id:
            raise DeploymentConfigError(f"endpoints[{index}].hangout_id is required")
        if endpoint_id in seen_ids:
            raise DeploymentConfigError(f"duplicate endpoint id {endpoint_id!r}")
        if token in seen_tokens:
            raise DeploymentConfigError(f"duplicate endpoint token for {endpoint_id!r}")
        seen_ids.add(endpoint_id)
        seen_tokens.add(token)
        out.append(
            Endpoint(
                id=endpoint_id,
                name=_optional_str(row, "name", endpoint_id),
                token=token,
                hangout_id=hangout_id,
                peer=_optional_str(row, "peer"),
            )
        )
    return tuple(out)


def _parse_kits(raw: Any, endpoint_ids: set[str]) -> tuple[Kit, ...]:
    if raw is None:
        return ()
    if not isinstance(raw, list):
        raise DeploymentConfigError("kits must be a list")
    out: list[Kit] = []
    seen: set[str] = set()
    for index, row in enumerate(raw):
        if not isinstance(row, dict):
            raise DeploymentConfigError(f"kits[{index}] must be a mapping")
        kit_id = _optional_str(row, "id")
        endpoint_id = _optional_str(row, "endpoint_id")
        if not kit_id:
            raise DeploymentConfigError(f"kits[{index}].id is required")
        if not endpoint_id:
            raise DeploymentConfigError(f"kits[{index}].endpoint_id is required")
        if kit_id in seen:
            raise DeploymentConfigError(f"duplicate kit id {kit_id!r}")
        if endpoint_id not in endpoint_ids:
            raise DeploymentConfigError(
                f"kits[{index}].endpoint_id {endpoint_id!r} is not a known endpoint"
            )
        seen.add(kit_id)
        out.append(
            Kit(
                id=kit_id,
                endpoint_id=endpoint_id,
                secrets_profile=_optional_str(row, "secrets_profile"),
                usb_serial=_optional_str(row, "usb_serial"),
            )
        )
    return tuple(out)


def _parse_aliases(raw: Any, endpoint_ids: set[str]) -> dict[str, str]:
    if raw is None:
        return {}
    if not isinstance(raw, dict):
        raise DeploymentConfigError("aliases must be a mapping")
    out: dict[str, str] = {}
    for key, value in raw.items():
        alias = str(key).strip().lower()
        target = str(value).strip()
        if not alias or not target:
            raise DeploymentConfigError("aliases keys and values must be non-empty")
        if alias in out:
            raise DeploymentConfigError(f"duplicate alias {alias!r}")
        if target not in endpoint_ids:
            raise DeploymentConfigError(
                f"alias {alias!r} points to unknown endpoint {target!r}"
            )
        out[alias] = target
    return out


def load_deployment(
    path: Path,
    *,
    source: str = "file",
) -> Deployment:
    try:
        raw_doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    except OSError as exc:
        raise DeploymentConfigError(f"could not read {path}: {exc}") from exc
    except yaml.YAMLError as exc:
        raise DeploymentConfigError(f"invalid YAML in {path}: {exc}") from exc

    if not isinstance(raw_doc, dict):
        raise DeploymentConfigError(f"deployment root must be a mapping in {path}")

    validate_document_schema(raw_doc)

    meta_raw = _require_mapping(raw_doc.get("deployment"), "deployment")
    meta_id = _optional_str(meta_raw, "id")
    meta_name = _optional_str(meta_raw, "name")
    if not meta_id:
        raise DeploymentConfigError("deployment.id is required")
    if not meta_name:
        raise DeploymentConfigError("deployment.name is required")

    endpoints = _parse_endpoints(raw_doc.get("endpoints"))
    endpoint_ids = {item.id for item in endpoints}
    users = _parse_users(raw_doc.get("users"))
    kits = _parse_kits(raw_doc.get("kits"), endpoint_ids)
    aliases = _parse_aliases(raw_doc.get("aliases"), endpoint_ids)

    deployment = Deployment(
        path=path.resolve(),
        source=source,
        meta=DeploymentMeta(id=meta_id, name=meta_name),
        users=users,
        endpoints=endpoints,
        kits=kits,
        aliases=aliases,
    )
    validate_deployment(deployment)
    return deployment


def validate_deployment(deployment: Deployment) -> None:
    if not deployment.endpoints:
        raise DeploymentConfigError("at least one endpoint is required")
    endpoint_ids = {item.id for item in deployment.endpoints}
    for endpoint in deployment.endpoints:
        if endpoint.peer and endpoint.peer not in endpoint_ids:
            raise DeploymentConfigError(
                f"endpoint {endpoint.id!r} peer {endpoint.peer!r} is not defined"
            )
    kit_targets = {kit.endpoint_id for kit in deployment.kits}
    if kit_targets != endpoint_ids:
        missing = endpoint_ids - kit_targets
        extra = kit_targets - endpoint_ids
        if missing:
            raise DeploymentConfigError(
                f"every endpoint needs a kit; missing kits for {sorted(missing)!r}"
            )
        if extra:
            raise DeploymentConfigError(f"kits reference unknown endpoints: {sorted(extra)!r}")


def load_active_deployment(
    project: ProjectConfig | None = None,
    *,
    environ: Mapping[str, str] | None = None,
) -> Deployment:
    path, source = resolve_deployment_path(project, environ=environ)
    return load_deployment(path, source=source)


def _redact_token(token: str) -> str:
    if _PLACEHOLDER_TOKEN_RE.fullmatch(token):
        return token
    if len(token) <= 4:
        return "[redacted]"
    return f"[redacted:{token[-4:]}]"


def dump_snapshot(deployment: Deployment) -> dict[str, object]:
    return {
        "document": SNAPSHOT_DOCUMENT,
        "meta": {
            "path": str(deployment.path),
            "source": deployment.source,
            "endpoint_count": len(deployment.endpoints),
        },
        "deployment": {
            "id": deployment.meta.id,
            "name": deployment.meta.name,
        },
        "users": [
            {
                "id": user.id,
                "name": user.name,
                "pin": "[redacted]" if user.pin else "",
                "web_admin": user.web_admin,
                "web_password": _redact_token(user.web_password) if user.web_password else "",
            }
            for user in deployment.users
        ],
        "endpoints": [
            {
                "id": endpoint.id,
                "name": endpoint.name,
                "token": _redact_token(endpoint.token),
                "hangout_id": endpoint.hangout_id,
                "peer": endpoint.peer,
            }
            for endpoint in deployment.endpoints
        ],
        "kits": [
            {
                "id": kit.id,
                "endpoint_id": kit.endpoint_id,
                "secrets_profile": kit.secrets_profile,
                "usb_serial": kit.usb_serial,
            }
            for kit in deployment.kits
        ],
        "aliases": dict(sorted(deployment.aliases.items())),
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Read and validate deployment YAML.")
    parser.add_argument(
        "--config",
        type=Path,
        help="host INI path (default: config/host.defaults.ini)",
    )
    parser.add_argument(
        "--file",
        type=Path,
        help="load a specific deployment YAML (skips local/example resolution)",
    )
    subparsers = parser.add_subparsers(dest="action", required=True)
    subparsers.add_parser("validate", help="validate deployment; print path on success")
    subparsers.add_parser(
        "catalog",
        help="print schema location and semantic validation rules as JSON",
    )
    subparsers.add_parser("dump", help="print redacted deployment snapshot as JSON")

    args = parser.parse_args(argv)
    try:
        if args.action == "catalog":
            project = ProjectConfig(args.config)
            project.validate()
            print(
                json.dumps(
                    build_catalog(resolve_schema_path(project)),
                    indent=2,
                    sort_keys=True,
                )
            )
            return 0

        if args.file is not None:
            deployment = load_deployment(args.file.resolve(), source="file")
        else:
            project = ProjectConfig(args.config)
            project.validate()
            deployment = load_active_deployment(project)
        if args.action == "validate":
            print(f"valid: {deployment.path} ({deployment.source})")
        elif args.action == "dump":
            print(json.dumps(dump_snapshot(deployment), indent=2, sort_keys=True))
    except (DeploymentConfigError, ProjectConfigError) as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code
    return 0


if __name__ == "__main__":
    sys.exit(main())
