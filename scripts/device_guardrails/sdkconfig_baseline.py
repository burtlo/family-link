"""Locked sdkconfig.defaults values for product device (MEM-LEDGER, MEM-DELTA, CFG-FP)."""
from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULTS = REPO / "client/device/sdkconfig.defaults"
DISPLAY = REPO / "client/device/main/product_display.c"
DRAW_LINES_DEFINE = "#define PRODUCT_LCD_DRAW_LINES 16"

# Documented product floor; do not raise LV_MEM or draw height via guardrails.
REQUIRED = {
    "CONFIG_ESP_MAIN_TASK_STACK_SIZE": "16384",
    "CONFIG_LV_MEM_SIZE": "32768",
    "CONFIG_BSP_LCD_DRAW_BUF_HEIGHT": "40",
    "CONFIG_BSP_LCD_DRAW_BUF_DOUBLE": "n",
}


def parse_defaults(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        match = re.match(r"^(CONFIG_[A-Z0-9_]+)=(.*)$", line)
        if match:
            key, value = match.group(1), match.group(2).strip()
            if value.startswith('"') and value.endswith('"'):
                value = value[1:-1]
            out[key] = value
    return out


def check_sdkconfig_defaults(path: Path | None = None) -> list[str]:
    path = path or DEFAULTS
    errors: list[str] = []
    if not path.is_file():
        return [f"missing sdkconfig.defaults: {path}"]
    values = parse_defaults(path.read_text(encoding="utf-8"))
    for key, expected in REQUIRED.items():
        actual = values.get(key)
        if actual is None:
            errors.append(f"missing {key} in {path}")
        elif actual != expected:
            errors.append(f"{key} drift: expected {expected}, got {actual}")
    return errors


def check_draw_lines(path: Path | None = None) -> list[str]:
    """Runtime draw height is the code define, not CONFIG_BSP_LCD_DRAW_BUF_HEIGHT."""
    path = path or DISPLAY
    if not path.is_file():
        return [f"missing product display source: {path}"]
    text = path.read_text(encoding="utf-8")
    if DRAW_LINES_DEFINE not in text:
        return [f"PRODUCT_LCD_DRAW_LINES drift: expected `{DRAW_LINES_DEFINE}` in {path.name}"]
    return []
