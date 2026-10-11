#!/usr/bin/env python3
"""Load and validate config/labs.registry.yaml for lab_host make targets."""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Optional, Sequence

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_REGISTRY_PATH = ROOT / "config" / "labs.registry.yaml"
DEFAULT_SCHEMA_PATH = ROOT / "config" / "labs" / "labs.registry.schema.v1.json"
SCHEMA_DOCUMENT = "family-link.labs-registry.schema/v1"
SNAPSHOT_DOCUMENT = "family-link.labs-registry.snapshot/v1"
REGISTRY_DOCUMENT = "family-link.labs-registry/v1"
DOCS_RELATIVE = "docs/standards/labs-configuration.md"

try:
    import jsonschema
except ImportError as exc:  # pragma: no cover
    raise SystemExit("jsonschema missing. Run: make install") from exc

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML missing. Run: make install") from exc

from project_config import ProjectConfig, ProjectConfigError


class LabRegistryError(ProjectConfigError):
    """Invalid or inconsistent labs registry."""


@dataclass(frozen=True)
class LabVariant:
    name: str
    build_dir: Path
    sdkconfig_defaults: tuple[Path, ...]


@dataclass(frozen=True)
class LabSpec:
    make_name: str
    lab_id: str
    evidence_slug: str
    project_dir: Path
    target: str
    description: str
    variants: dict[str, LabVariant]
    default_variant: str
    trace_parser: Path | None
    capture_seconds_default: int
    flash_by_default: bool
    run_instructions: str | None
    run_instructions_doc: str | None


def _repo_path(raw: str, environ: Mapping[str, str]) -> Path:
    resolved = Path(raw).expanduser()
    if not resolved.is_absolute():
        resolved = ROOT / resolved
    return resolved.resolve()


def resolve_registry_path(
    project: ProjectConfig | None = None,
    *,
    environ: Mapping[str, str] | None = None,
) -> Path:
    cfg = project if project is not None else ProjectConfig(environ=environ)
    return _repo_path(cfg.text("labs.registry", required=True), cfg.environ)


def resolve_schema_path(
    project: ProjectConfig | None = None,
    *,
    environ: Mapping[str, str] | None = None,
) -> Path:
    cfg = project if project is not None else ProjectConfig(environ=environ)
    return _repo_path(cfg.text("labs.schema", required=True), cfg.environ)


def load_schema_document(path: Path | None = None) -> dict[str, Any]:
    dest = path if path is not None else DEFAULT_SCHEMA_PATH
    try:
        return json.loads(dest.read_text(encoding="utf-8"))
    except OSError as exc:
        raise LabRegistryError(f"could not read schema {dest}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise LabRegistryError(f"invalid JSON schema in {dest}: {exc}") from exc


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
            raise LabRegistryError(f"schema: {location}: {detail}") from exc
        raise LabRegistryError(f"schema: {detail}") from exc


def build_catalog(schema_path: Path | None = None) -> dict[str, object]:
    path = schema_path if schema_path is not None else DEFAULT_SCHEMA_PATH
    return {
        "document": SCHEMA_DOCUMENT,
        "schema_file": str(path.resolve()),
        "schema_id": SCHEMA_DOCUMENT,
        "yaml_format": REGISTRY_DOCUMENT,
        "structural_validation": "jsonschema against labs.registry.schema",
        "semantic_rules": [
            "document must be family-link.labs-registry/v1",
            "make names (labs map keys) are lowercase alphanumeric",
            "paths are repo-relative; absolute paths are rejected",
            "project_dir must exist under the repository root",
            "variants.default must name a defined variant",
            "trace_parser, when set, must reference an existing file",
            "run.instructions or run.instructions_doc is required (operator runbook)",
            "instructions_doc path must exist; anchor is optional (#heading-id)",
        ],
        "docs": str(ROOT / DOCS_RELATIVE),
        "product_registry_docs": str(ROOT / "docs/labs/README.md"),
    }


def _as_path(value: str) -> Path:
    path = Path(value)
    if path.is_absolute():
        raise LabRegistryError(f"Lab registry paths must be repo-relative: {value}")
    return path


def _parse_run_operator(make_name: str, run: Any) -> tuple[str | None, str | None]:
    if not isinstance(run, dict):
        run = {}
    raw_instructions = run.get("instructions")
    raw_doc = run.get("instructions_doc")
    instructions = (
        raw_instructions.strip()
        if isinstance(raw_instructions, str) and raw_instructions.strip()
        else None
    )
    instructions_doc = (
        raw_doc.strip() if isinstance(raw_doc, str) and raw_doc.strip() else None
    )
    if not instructions and not instructions_doc:
        raise LabRegistryError(
            f"lab.{make_name} run requires instructions or instructions_doc"
        )
    if instructions_doc:
        path_part, anchor = (
            instructions_doc.split("#", 1) if "#" in instructions_doc else (instructions_doc, "")
        )
        doc_path = _as_path(path_part)
        if doc_path.suffix.lower() != ".md":
            raise LabRegistryError(
                f"lab.{make_name} instructions_doc must end with .md: {instructions_doc}"
            )
        if not (ROOT / doc_path).is_file():
            raise LabRegistryError(
                f"lab.{make_name} instructions_doc missing: {instructions_doc}"
            )
        if anchor and not anchor.replace("-", "").replace("_", "").isalnum():
            raise LabRegistryError(
                f"lab.{make_name} instructions_doc anchor is invalid: {anchor!r}"
            )
    return instructions, instructions_doc


def operator_help_lines(
    spec: LabSpec,
    *,
    variant: str | None = None,
    action: str | None = None,
    run_id: str | None = None,
    seconds: int | None = None,
    flash: bool | None = None,
) -> list[str]:
    """Terminal-friendly operator briefing for make lab.<name>.help and run start."""
    lines = [f"=== lab.{spec.make_name} ({spec.lab_id}) ==="]
    if spec.description:
        lines.append(spec.description)
    details: list[str] = []
    if variant:
        details.append(f"variant={variant}")
    if action:
        details.append(f"action={action}")
    if run_id:
        details.append(f"run_id={run_id}")
    if seconds is not None:
        details.append(f"capture={seconds}s")
    if flash is not None:
        details.append(f"flash={'yes' if flash else 'no'}")
    if details:
        lines.append(" ".join(details))
    capture_s = seconds if seconds is not None else spec.capture_seconds_default
    lines.append("")
    lines.append(
        f"Host serial capture: {capture_s}s after flash "
        f"(set SECONDS= to override capture_seconds_default in labs.registry.yaml)."
    )
    lines.append(
        "This terminal stays quiet during capture. Device lines (e.g. LAB02_READY, "
        "HARNESS_DONE) are saved to logs/device/labs/.../serial.txt, then summarized."
    )
    lines.append("")
    lines.append("Operator:")
    if spec.run_instructions:
        for paragraph in spec.run_instructions.strip().splitlines():
            text = paragraph.strip()
            if text:
                lines.append(f"  {text}")
    if spec.run_instructions_doc:
        lines.append(f"  Read: {spec.run_instructions_doc}")
    return lines


def print_operator_help(spec: LabSpec, **kwargs: object) -> None:
    for line in operator_help_lines(spec, **kwargs):  # type: ignore[arg-type]
        print(line)


def run_mode_hint_lines(spec: LabSpec) -> list[str]:
    """Behavior-named make targets (preferred over VARIANT=)."""
    name = spec.make_name
    lines = ["Run modes:"]
    lines.append(f"  Manual (you scroll on device)     make lab.{name} [KIT=<nickname>]")
    if "scripted" in spec.variants:
        lines.append(f"  Auto-scroll (device runs script)  make lab.{name}.scripted [KIT=<nickname>]")
    if "eager" in spec.variants:
        lines.append(f"  Eager comparison build            make lab.{name}.eager [KIT=<nickname>]")
    if spec.trace_parser:
        lines.append(f"  Parse last capture (metrics.json) make lab.{name}.parse [RUN_ID=…]")
    return lines


def _parse_registry(raw: dict[str, Any], registry_file: Path) -> dict[str, LabSpec]:
    if raw.get("document") != REGISTRY_DOCUMENT:
        raise LabRegistryError(f"Unsupported lab registry document in {registry_file}")
    labs_raw = raw.get("labs")
    if not isinstance(labs_raw, dict) or not labs_raw:
        raise LabRegistryError("Lab registry must define a non-empty labs map")
    specs: dict[str, LabSpec] = {}
    for make_name, entry in labs_raw.items():
        if not isinstance(entry, dict):
            raise LabRegistryError(f"lab.{make_name} entry must be a mapping")
        project_dir = _as_path(entry["project_dir"])
        if not (ROOT / project_dir).is_dir():
            raise LabRegistryError(f"lab.{make_name} project_dir missing: {project_dir}")
        variants_raw = entry.get("variants")
        if not isinstance(variants_raw, dict):
            raise LabRegistryError(f"lab.{make_name} requires variants")
        default_variant = variants_raw.get("default", "default")
        if default_variant == "default" and "default" not in variants_raw:
            default_variant = next(k for k in variants_raw if k != "default")
        variants: dict[str, LabVariant] = {}
        for vname, ventry in variants_raw.items():
            if vname == "default" or not isinstance(ventry, dict):
                continue
            build_dir = _as_path(ventry["build_dir"])
            defaults = ventry.get("sdkconfig_defaults") or []
            if not isinstance(defaults, list):
                raise LabRegistryError(
                    f"lab.{make_name} variant {vname}: sdkconfig_defaults must be a list"
                )
            spec_defaults = tuple(project_dir / name for name in defaults)
            variants[vname] = LabVariant(vname, build_dir, spec_defaults)
        if default_variant not in variants:
            raise LabRegistryError(
                f"lab.{make_name} default variant {default_variant!r} is not defined"
            )
        trace = entry.get("trace_parser")
        trace_parser = _as_path(trace) if trace else None
        if trace_parser is not None and not (ROOT / trace_parser).is_file():
            raise LabRegistryError(f"lab.{make_name} trace_parser missing: {trace_parser}")
        run = entry.get("run") or {}
        run_instructions, run_instructions_doc = _parse_run_operator(make_name, run)
        specs[make_name] = LabSpec(
            make_name=make_name,
            lab_id=str(entry.get("lab_id", make_name)),
            evidence_slug=str(entry.get("evidence_slug", make_name)),
            project_dir=project_dir,
            target=str(entry.get("target", "esp32s3")),
            description=str(entry.get("description", "")),
            variants=variants,
            default_variant=default_variant,
            trace_parser=trace_parser,
            capture_seconds_default=int(run.get("capture_seconds_default", 90)),
            flash_by_default=bool(run.get("flash_by_default", True)),
            run_instructions=run_instructions,
            run_instructions_doc=run_instructions_doc,
        )
    return specs


def load_registry_yaml(registry_file: Path) -> dict[str, Any]:
    if not registry_file.is_file():
        raise LabRegistryError(f"Lab registry not found: {registry_file}")
    raw = yaml.safe_load(registry_file.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise LabRegistryError(f"Lab registry root must be a mapping: {registry_file}")
    return raw


def load_registry(
    path: Path | None = None,
    *,
    project: ProjectConfig | None = None,
    validate_schema: bool = True,
) -> dict[str, LabSpec]:
    registry_file = path if path is not None else resolve_registry_path(project)
    raw = load_registry_yaml(registry_file)
    if validate_schema:
        validate_document_schema(raw, project=project)
    return _parse_registry(raw, registry_file)


def get_lab(
    make_name: str,
    registry: dict[str, LabSpec] | None = None,
    *,
    project: ProjectConfig | None = None,
) -> LabSpec:
    registry = registry or load_registry(project=project)
    key = make_name.lower()
    if key not in registry:
        known = ", ".join(sorted(registry))
        raise LabRegistryError(f"Unknown lab {make_name!r}; known: {known}")
    return registry[key]


def resolve_variant(spec: LabSpec, variant: str | None) -> LabVariant:
    name = (variant or spec.default_variant).lower()
    if name not in spec.variants:
        known = ", ".join(sorted(spec.variants))
        raise LabRegistryError(
            f"Unknown variant {variant!r} for lab.{spec.make_name}; known: {known}"
        )
    return spec.variants[name]


def dump_snapshot(
    registry: dict[str, LabSpec],
    *,
    registry_path: Path,
    schema_path: Path,
) -> dict[str, object]:
    labs_payload: dict[str, object] = {}
    for make_name in sorted(registry):
        spec = registry[make_name]
        labs_payload[make_name] = {
            "lab_id": spec.lab_id,
            "evidence_slug": spec.evidence_slug,
            "project_dir": str(spec.project_dir),
            "target": spec.target,
            "description": spec.description,
            "default_variant": spec.default_variant,
            "variants": {
                vname: {
                    "build_dir": str(variant.build_dir),
                    "sdkconfig_defaults": [str(p) for p in variant.sdkconfig_defaults],
                }
                for vname, variant in sorted(spec.variants.items())
            },
            "trace_parser": str(spec.trace_parser) if spec.trace_parser else None,
            "run": {
                "capture_seconds_default": spec.capture_seconds_default,
                "flash_by_default": spec.flash_by_default,
                "instructions": spec.run_instructions,
                "instructions_doc": spec.run_instructions_doc,
            },
        }
    return {
        "document": SNAPSHOT_DOCUMENT,
        "meta": {
            "registry_path": str(registry_path),
            "schema_path": str(schema_path),
            "lab_count": len(registry),
        },
        "labs": labs_payload,
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Read and validate device labs registry YAML.")
    parser.add_argument(
        "--config",
        type=Path,
        help="host INI path (default: config/host.defaults.ini)",
    )
    parser.add_argument(
        "--file",
        type=Path,
        help="load a specific registry YAML (skips host [labs].registry path)",
    )
    subparsers = parser.add_subparsers(dest="action", required=True)
    subparsers.add_parser("validate", help="validate registry; print path on success")
    subparsers.add_parser(
        "catalog",
        help="print schema location and semantic validation rules as JSON",
    )
    subparsers.add_parser("dump", help="print labs registry snapshot as JSON")

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
            registry_path = args.file.resolve()
            project = ProjectConfig(args.config) if args.config else None
            if project is not None:
                project.validate()
            schema_path = (
                resolve_schema_path(project) if project is not None else DEFAULT_SCHEMA_PATH
            )
            registry = load_registry(registry_path, project=project)
        else:
            project = ProjectConfig(args.config)
            project.validate()
            registry_path = resolve_registry_path(project)
            schema_path = resolve_schema_path(project)
            registry = load_registry(project=project)

        if args.action == "validate":
            print(f"valid: {registry_path}")
        elif args.action == "dump":
            print(
                json.dumps(
                    dump_snapshot(
                        registry,
                        registry_path=registry_path,
                        schema_path=schema_path,
                    ),
                    indent=2,
                    sort_keys=True,
                )
            )
    except (LabRegistryError, ProjectConfigError) as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
