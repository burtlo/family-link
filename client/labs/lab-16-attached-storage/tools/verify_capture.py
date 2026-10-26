#!/usr/bin/env python3
"""Pass/fail check for LAB-16 serial capture (SD record + diary upload)."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
from device_guardrails.lab16_serial import verify_lab16_upload_capture as verify


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("capture", type=Path)
    a = p.parse_args()
    text = a.capture.read_text(encoding="utf-8", errors="replace")
    errors = verify(text)
    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
