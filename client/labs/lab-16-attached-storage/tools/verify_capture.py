#!/usr/bin/env python3
"""Pass/fail check for LAB-16 serial capture (SD record + diary upload)."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

READY = re.compile(r"LAB16_READY build=\S+ mode=scripted fixture=lab16-sd/v3 harness=1")
SD_OK = re.compile(
    r"LAB16_SD status=detected sectors=\d+ sector_bytes=512 capacity_bytes=\d+ "
    r"bus_width=\d+ real_freq_khz=\d+ read_lba0=ok probe_writes=0"
)
STREAM_OK = re.compile(
    r"LAB16_STREAM status=complete elapsed_ms=\d+ pcm_bytes=6720000 "
    r"sectors_written=\d+ driver_writes=\d+ start_lba=2048 sample_rate=16000"
)
UPLOAD_OK = re.compile(r"LAB16_UPLOAD status=complete uploaded=210 failed=0")
HARNESS_OK = re.compile(r"LAB16_HARNESS_DONE status=complete")


def verify(text: str) -> list[str]:
    errors: list[str] = []
    if not READY.search(text):
        errors.append("missing LAB16_READY (v3 harness)")
    if not SD_OK.search(text):
        errors.append("missing successful LAB16_SD detected line")
    if not STREAM_OK.search(text):
        errors.append("missing LAB16_STREAM status=complete with 6720000 PCM bytes")
    if not UPLOAD_OK.search(text):
        errors.append("missing LAB16_UPLOAD status=complete uploaded=210")
    if not HARNESS_OK.search(text):
        errors.append("missing LAB16_HARNESS_DONE status=complete")
    return errors


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
