"""Product boot marker checks (boot-marker claim class)."""
from __future__ import annotations

import re

from device_guardrails.serial_common import require_substrings, serial_forbidden_errors

BOOTSTRAP = re.compile(r"V2_BOOTSTRAP_READY build=\S+")
PRODUCT_UI = re.compile(r"product_ui_ready")


def verify_product_boot(text: str, *, require_product_ui: bool = True) -> list[str]:
    errors = serial_forbidden_errors(text)
    if not BOOTSTRAP.search(text):
        errors.append("missing V2_BOOTSTRAP_READY build=<id>")
    if require_product_ui and not PRODUCT_UI.search(text):
        errors.append("missing product_ui_ready (bootstrap-only is not product UI boot)")
    return errors


def verify_bootstrap_only_should_fail(text: str) -> list[str]:
    """Return errors when a log has bootstrap but not product UI (negative gate)."""
    errors: list[str] = []
    if not BOOTSTRAP.search(text):
        errors.append("fixture should include bootstrap marker for negative test")
    if PRODUCT_UI.search(text):
        errors.append("fixture should not include product_ui_ready")
    if not errors:
        errors = verify_product_boot(text, require_product_ui=True)
    return errors
