#!/usr/bin/env python3
"""Desk record/send serial script — run against saved logs only (INV-INT-1).

Live desk capture is operator-authorized; CI uses fixtures under scripts/tests/fixtures/device-serial/.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from device_guardrails.product_boot_serial import verify_product_boot
from device_guardrails.serial_common import serial_forbidden_errors

# Wiring proof placeholders until INT-WIRE repair lands; script still fails closed on panic/guru.
PUSH_PCM = re.compile(r"product_record.*push_pcm|push_pcm", re.I)
NONZERO_AUDIO = re.compile(r"pcm_bytes=\d+", re.I)
SEND_WORKER = re.compile(r"product_send_http|send_http_worker", re.I)


def verify_record_send_desk(text: str, *, strict_wiring: bool = False) -> list[str]:
    errors = verify_product_boot(text, require_product_ui=True)
    errors.extend(serial_forbidden_errors(text))
    if strict_wiring:
        if not PUSH_PCM.search(text):
            errors.append("INT-WIRE pending: no push_pcm evidence in serial")
        if not NONZERO_AUDIO.search(text):
            errors.append("INT-WIRE pending: no non-zero pcm_bytes line")
        if not SEND_WORKER.search(text):
            errors.append("INT-WIRE pending: no send HTTP worker evidence")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument(
        "--strict-wiring",
        action="store_true",
        help="Require mic/send wiring markers (use after record/send repair)",
    )
    args = parser.parse_args()
    text = args.capture.read_text(encoding="utf-8", errors="replace")
    errors = verify_record_send_desk(text, strict_wiring=args.strict_wiring)
    if errors:
        for item in errors:
            print(item, file=sys.stderr)
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
