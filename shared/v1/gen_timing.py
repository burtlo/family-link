#!/usr/bin/env python3
"""Generate v1_timing.h and v1_timing.js from shared/v1/timing.yaml."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _load_yaml_minimal(text: str) -> dict[str, Any]:
    """Parse the fixed timing.yaml schema without PyYAML."""

    root: dict[str, Any] = {}
    section: dict[str, Any] | None = None

    for raw in text.splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        if not line.startswith(" ") and line.endswith(":"):
            name = line[:-1].strip()
            if name == "version":
                continue
            section = {}
            root[name] = section
            continue
        if line.startswith("version:"):
            root["version"] = int(line.split(":", 1)[1].strip())
            continue
        if section is None:
            continue
        key, val = line.split(":", 1)
        section[key.strip()] = int(val.strip())

    return root


def _load_yaml(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    try:
        import yaml  # type: ignore
    except ImportError:
        return _load_yaml_minimal(text)
    data = yaml.safe_load(text)
    if not isinstance(data, dict):
        raise SystemExit(f"{path}: expected mapping at root")
    return data


# (yaml path, C/JS symbol suffix, unit suffix for comment)
_SYMBOLS: list[tuple[tuple[str, ...], str, str]] = [
    (("connect", "retry_ms"), "CONNECT_RETRY_MS", "ms"),
    (("connect", "probe_ms"), "CONNECT_PROBE_MS", "ms"),
    (("connect", "wifi_retry_ms"), "CONNECT_WIFI_RETRY_MS", "ms"),
    (("carousel", "snap_ms_min"), "CAROUSEL_SNAP_MS_MIN", "ms"),
    (("carousel", "snap_ms_max"), "CAROUSEL_SNAP_MS_MAX", "ms"),
    (("ui", "toast_ms"), "UI_TOAST_MS", "ms"),
    (("ui", "pick_timeout_ms"), "UI_PICK_TIMEOUT_MS", "ms"),
    (("ui", "idle_relock_ms"), "UI_IDLE_RELOCK_MS", "ms"),
    (("ui", "dim_ms"), "UI_DIM_MS", "ms"),
    (("ui", "sleep_ms"), "UI_SLEEP_MS", "ms"),
    (("record", "max_sec"), "RECORD_MAX_SEC", "s"),
    (("record", "silence_sec"), "RECORD_SILENCE_SEC", "s"),
    (("record", "trim_ms"), "RECORD_TRIM_MS", "ms"),
    (("auth", "pin_tries"), "AUTH_PIN_TRIES", ""),
    (("auth", "pin_cooldown_ms"), "AUTH_PIN_COOLDOWN_MS", "ms"),
]


def _lookup(data: dict[str, Any], path: tuple[str, ...]) -> int:
    node: Any = data
    for key in path:
        if not isinstance(node, dict) or key not in node:
            raise SystemExit(f"timing.yaml missing key: {'.'.join(path)}")
        node = node[key]
    if not isinstance(node, int):
        raise SystemExit(f"timing.yaml {'.'.join(path)} must be an integer")
    return node


def _emit_header(data: dict[str, Any], version: int) -> str:
    lines = [
        "/* Generated from shared/v1/timing.yaml — do not edit by hand. */",
        "/* Run: make v1-timing */",
        "#pragma once",
        "",
        f"#define V1_TIMING_VERSION {version}",
        "",
    ]
    for path, suffix, unit in _SYMBOLS:
        val = _lookup(data, path)
        comment = f"  /* {unit} */" if unit else ""
        lines.append(f"#define V1_{suffix} {val}{comment}")
    lines.append("")
    return "\n".join(lines)


def _emit_js(data: dict[str, Any], version: int) -> str:
    lines = [
        "/** Generated from shared/v1/timing.yaml — do not edit by hand. */",
        "/** Run: make v1-timing */",
        "",
        f"export const V1_TIMING_VERSION = {version};",
        "",
    ]
    for path, suffix, _unit in _SYMBOLS:
        val = _lookup(data, path)
        lines.append(f"export const V1_{suffix} = {val};")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    root = _repo_root()
    yaml_path = root / "shared" / "v1" / "timing.yaml"
    header_path = root / "firmware" / "v1" / "v1_timing.h"
    js_path = root / "demos" / "server" / "v1_product" / "web" / "v1_timing.js"

    data = _load_yaml(yaml_path)
    version = int(data.get("version", 1))

    header_path.parent.mkdir(parents=True, exist_ok=True)
    js_path.parent.mkdir(parents=True, exist_ok=True)

    header_text = _emit_header(data, version)
    js_text = _emit_js(data, version)

    header_path.write_text(header_text, encoding="utf-8", newline="\n")
    js_path.write_text(js_text, encoding="utf-8", newline="\n")

    print(f"wrote {header_path.relative_to(root)} ({len(_SYMBOLS)} defines)")
    print(f"wrote {js_path.relative_to(root)} ({len(_SYMBOLS)} exports)")
    print(f"timing.yaml version={version}")
    for path, suffix, _unit in _SYMBOLS:
        print(f"  V1_{suffix} = {_lookup(data, path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
