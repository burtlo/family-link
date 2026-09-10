#!/usr/bin/env python3
"""Compare v1 timing constants across YAML, firmware, and web twin.

Reads shared/v1/timing.yaml when present; otherwise compares firmware and web
by grepping x02_product_shell.c and box.js (or generated v1_timing.* when present).

Exit 0 when connect_probe_ms, connect_retry_ms, snap min/max, and toast_ms match.
Exit 1 and print a diff table on mismatch.

  python scripts/check_v1_parity.py
  make check-v1-parity
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
YAML_PATH = ROOT / "shared" / "v1" / "timing.yaml"
FW_C = ROOT / "firmware" / "demos" / "x02_product_shell.c"
FW_H = ROOT / "firmware" / "v1" / "v1_timing.h"
WEB_JS = ROOT / "demos" / "server" / "v1_product" / "web" / "box.js"
WEB_TIMING = ROOT / "demos" / "server" / "v1_product" / "web" / "v1_timing.js"

MISSING = "—"


def _read(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8")


def _yaml_value(data: dict[str, Any], *keys: str) -> int | None:
    node: Any = data
    for key in keys:
        if not isinstance(node, dict) or key not in node:
            return None
        node = node[key]
    try:
        return int(node)
    except (TypeError, ValueError):
        return None


def _first_int(text: str, patterns: list[str]) -> int | None:
    for pat in patterns:
        if m := re.search(pat, text, re.MULTILINE):
            return int(m.group(1))
    return None


def load_yaml() -> dict[str, Any] | None:
    if not YAML_PATH.is_file():
        return None
    try:
        import yaml
    except ImportError:
        print("check_v1_parity: PyYAML required (pip install pyyaml)", file=sys.stderr)
        sys.exit(2)
    data = yaml.safe_load(_read(YAML_PATH))
    return data if isinstance(data, dict) else None


def parse_firmware() -> dict[str, int | None]:
    h_text = _read(FW_H)
    c_text = _read(FW_C)
    text = h_text + "\n" + c_text

    toast = _first_int(
        text,
        [
            r"#define\s+V1_UI_TOAST_MS\s+(\d+)",
            r"#define\s+TOAST_MS\s+(\d+)",
        ],
    )
    if toast is None and c_text:
        if m := re.search(
            r"s_toast_pending\s*=\s*false;.*?vTaskDelay\(pdMS_TO_TICKS\((\d+)\)",
            c_text,
            re.DOTALL,
        ):
            toast = int(m.group(1))

    return {
        "connect_probe_ms": _first_int(
            text,
            [r"#define\s+V1_CONNECT_PROBE_MS\s+(\d+)", r"#define\s+CONNECT_PROBE_MS\s+(\d+)"],
        ),
        "connect_retry_ms": _first_int(
            text,
            [r"#define\s+V1_CONNECT_RETRY_MS\s+(\d+)", r"#define\s+CONNECT_RETRY_MS\s+(\d+)"],
        ),
        "snap_ms_min": _first_int(
            text,
            [
                r"#define\s+V1_CAROUSEL_SNAP_MS_MIN\s+(\d+)",
                r"#define\s+V1_SNAP_SCROLL_MS_MIN\s+(\d+)",
                r"#define\s+SNAP_SCROLL_MS\s+(\d+)",
            ],
        ),
        "snap_ms_max": _first_int(
            text,
            [
                r"#define\s+V1_CAROUSEL_SNAP_MS_MAX\s+(\d+)",
                r"#define\s+V1_SNAP_SCROLL_MS_MAX\s+(\d+)",
                r"#define\s+SNAP_SCROLL_MS_MAX\s+(\d+)",
            ],
        ),
        "toast_ms": toast,
    }


def parse_web() -> dict[str, int | None]:
    timing_text = _read(WEB_TIMING)
    box_text = _read(WEB_JS)
    text = timing_text + "\n" + box_text

    export_or_var = r"(?:export\s+const|var|const)\s+{name}\s*=\s*(\d+)"

    def web_pat(name: str) -> str:
        return export_or_var.format(name=re.escape(name))

    return {
        "connect_probe_ms": _first_int(
            text,
            [
                web_pat("V1_CONNECT_PROBE_MS"),
                r"const\s+CONNECT_PROBE_MS\s*=\s*(\d+)",
            ],
        ),
        "connect_retry_ms": _first_int(
            text,
            [
                web_pat("V1_CONNECT_RETRY_MS"),
                r"const\s+CONNECT_RETRY_MS\s*=\s*(\d+)",
            ],
        ),
        "snap_ms_min": _first_int(
            text,
            [
                web_pat("V1_CAROUSEL_SNAP_MS_MIN"),
                web_pat("V1_SNAP_SCROLL_MS_MIN"),
                r"const\s+SNAP_SCROLL_MS_MIN\s*=\s*(\d+)",
            ],
        ),
        "snap_ms_max": _first_int(
            text,
            [
                web_pat("V1_CAROUSEL_SNAP_MS_MAX"),
                web_pat("V1_SNAP_SCROLL_MS_MAX"),
                r"const\s+SNAP_SCROLL_MS_MAX\s*=\s*(\d+)",
            ],
        ),
        "toast_ms": _first_int(
            text,
            [
                web_pat("V1_UI_TOAST_MS"),
                r"function\s+setToast\(msg,\s*ms\s*=\s*(\d+)\)",
            ],
        ),
    }


def parse_yaml(data: dict[str, Any]) -> dict[str, int | None]:
    return {
        "connect_probe_ms": _yaml_value(data, "connect", "probe_ms"),
        "connect_retry_ms": _yaml_value(data, "connect", "retry_ms"),
        "snap_ms_min": _yaml_value(data, "carousel", "snap_ms_min"),
        "snap_ms_max": _yaml_value(data, "carousel", "snap_ms_max"),
        "toast_ms": _yaml_value(data, "ui", "toast_ms"),
    }


def _fmt(value: int | None) -> str:
    return str(value) if value is not None else MISSING


def _row_values(row: dict[str, int | None]) -> set[int]:
    return {v for v in row.values() if v is not None}


def main() -> int:
    yaml_data = load_yaml()
    yaml_vals = parse_yaml(yaml_data) if yaml_data else {k: None for k in parse_firmware()}
    fw_vals = parse_firmware()
    web_vals = parse_web()

    keys = [
        "connect_probe_ms",
        "connect_retry_ms",
        "snap_ms_min",
        "snap_ms_max",
        "toast_ms",
    ]

    mismatches: list[str] = []
    rows: list[tuple[str, str, str, str, bool]] = []

    for key in keys:
        y, f, w = yaml_vals.get(key), fw_vals.get(key), web_vals.get(key)
        present = _row_values({"yaml": y, "firmware": f, "web": w})
        if yaml_data is not None and y is not None:
            ok = f == y and w == y
        else:
            ok = f is not None and w is not None and f == w
        if not ok:
            mismatches.append(key)
        rows.append((key, _fmt(y), _fmt(f), _fmt(w), ok))

    sources = []
    if yaml_data:
        sources.append(str(YAML_PATH.relative_to(ROOT)))
    if FW_H.is_file():
        sources.append(str(FW_H.relative_to(ROOT)))
    else:
        sources.append(str(FW_C.relative_to(ROOT)))
    if WEB_TIMING.is_file():
        sources.append(str(WEB_TIMING.relative_to(ROOT)))
    else:
        sources.append(str(WEB_JS.relative_to(ROOT)))

    print("v1 timing parity")
    print("sources:", ", ".join(sources))
    print()
    print(f"{'constant':<20} {'yaml':>8} {'firmware':>10} {'web':>8}  ok")
    print("-" * 58)
    for name, y, f, w, ok in rows:
        mark = "yes" if ok else "NO"
        print(f"{name:<20} {y:>8} {f:>10} {w:>8}  {mark}")

    if mismatches:
        print()
        print("mismatch:", ", ".join(mismatches))
        return 1

    print()
    print("all checked constants match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
