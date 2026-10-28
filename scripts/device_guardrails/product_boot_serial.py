"""Product boot marker checks (boot-marker claim class)."""
from __future__ import annotations

import re

from device_guardrails.serial_common import serial_forbidden_errors

EARLY_BUILD = re.compile(r"\bbuild=\S+")
PRODUCT_UI_READY = re.compile(r"product_ui_ready build=\S+")


def verify_product_boot(text: str, *, require_product_ui: bool = True) -> list[str]:
    errors = serial_forbidden_errors(text)
    if not EARLY_BUILD.search(text):
        errors.append("missing early build=<id> (app_main)")
    if require_product_ui and not PRODUCT_UI_READY.search(text):
        errors.append("missing product_ui_ready build=<id>")
    return errors


def verify_incomplete_ui_boot_should_fail(text: str) -> list[str]:
    """Return errors when a log has early build id but not product UI ready (negative gate)."""
    errors: list[str] = []
    if not EARLY_BUILD.search(text):
        errors.append("fixture should include early build=<id>")
    if PRODUCT_UI_READY.search(text):
        errors.append("fixture should not include product_ui_ready build=<id>")
    if not errors:
        errors = verify_product_boot(text, require_product_ui=True)
    return errors
