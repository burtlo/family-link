#!/usr/bin/env python3
"""Pack and validate the device UI YAML catalog for LAB-14."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CATALOG = ROOT / "docs" / "product" / "device-ui" / "catalog"
DEFAULT_SCHEMA = DEFAULT_CATALOG / "schema" / "device-ui-catalog.v1.json"
DEFAULT_OUT = ROOT / "client" / "labs" / "lab-14-device-ui-catalog" / "generated" / "catalog.json"
DOCUMENT = "family-link.device-ui-catalog/v1"

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML missing. Run: make install") from exc


class CatalogError(ValueError):
    """Invalid device UI catalog."""


def _load_yaml(path: Path) -> Any:
    with path.open(encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    if data is None:
        raise CatalogError(f"{path} is empty")
    return data


def load_catalog(catalog_dir: Path) -> dict[str, Any]:
    index_path = catalog_dir / "catalog.yaml"
    tokens_path = catalog_dir / "tokens.yaml"
    if not index_path.is_file():
        raise CatalogError(f"missing {index_path}")
    if not tokens_path.is_file():
        raise CatalogError(f"missing {tokens_path}")

    index = _load_yaml(index_path)
    if not isinstance(index, dict):
        raise CatalogError("catalog.yaml must be a mapping")

    scene_dir = catalog_dir / str(index.get("scene_dir") or "scenes")
    scenes: list[dict[str, Any]] = []
    if scene_dir.is_dir():
        for path in sorted(scene_dir.glob("*.yaml")):
            scene = _load_yaml(path)
            if not isinstance(scene, dict):
                raise CatalogError(f"{path.name} must be a mapping")
            if "id" not in scene:
                raise CatalogError(f"{path.name} is missing id")
            scenes.append(scene)

    ids = [str(scene["id"]) for scene in scenes]
    if len(ids) != len(set(ids)):
        raise CatalogError("duplicate scene id")

    scenes.sort(key=lambda scene: (int(scene.get("order") or 0), str(scene["id"])))
    packed = {
        "document": index.get("document", DOCUMENT),
        "revision": int(index.get("revision") or 1),
        "theme": index.get("theme", "THEME-DARK"),
        "title": index.get("title", "Device UI catalog"),
        "tokens": _load_yaml(tokens_path),
        "scenes": scenes,
    }
    return packed


def validate_catalog(packed: dict[str, Any], schema_path: Path = DEFAULT_SCHEMA) -> None:
    try:
        import jsonschema
    except ImportError as exc:  # pragma: no cover
        raise CatalogError("jsonschema missing. Run: make install") from exc
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    try:
        jsonschema.validate(packed, schema)
    except jsonschema.ValidationError as exc:
        raise CatalogError(str(exc)) from exc
    if packed["document"] != DOCUMENT:
        raise CatalogError(f"document must be {DOCUMENT}")
    if not packed["scenes"]:
        raise CatalogError("catalog has no scenes")


def pack(catalog_dir: Path, out_path: Path, schema_path: Path = DEFAULT_SCHEMA) -> dict[str, Any]:
    packed = load_catalog(catalog_dir)
    validate_catalog(packed, schema_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(packed, indent=2) + "\n", encoding="utf-8")
    return packed


def _print_summary(packed: dict[str, Any]) -> None:
    print(f"{packed['document']} revision={packed['revision']} scenes={len(packed['scenes'])}")
    for scene in packed["scenes"]:
        print(f"  {scene['id']}  {scene['title']} ({scene['layout']})")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("validate", help="Load and schema-check the YAML catalog")
    sub.add_parser("pack", help="Write the packed JSON the firmware embeds")
    args = parser.parse_args(argv)

    try:
        if args.command == "validate":
            packed = load_catalog(args.catalog)
            validate_catalog(packed, args.schema)
            _print_summary(packed)
            return 0
        packed = pack(args.catalog, args.out, args.schema)
        print(f"wrote {args.out}")
        _print_summary(packed)
        return 0
    except (CatalogError, OSError) as exc:
        print(f"device_ui_catalog: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
