#!/usr/bin/env python3
"""Pass/fail for LAB-16 overlap variant (two 180 s messages, concurrent record + upload)."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

READY = re.compile(r"LAB16_READY build=\S+ mode=scripted fixture=lab16-sd/v4 harness=1")
SD_OK = re.compile(
    r"LAB16_SD status=detected sectors=\d+ sector_bytes=512 capacity_bytes=\d+ "
    r"bus_width=\d+ real_freq_khz=\d+ read_lba0=ok probe_writes=0"
)
STREAM1 = re.compile(
    r"LAB16_STREAM msg=1 status=complete elapsed_ms=\d+ pcm_bytes=5760000 "
    r"sectors_written=\d+ driver_writes=\d+ start_lba=2048 sample_rate=16000"
)
STREAM2 = re.compile(
    r"LAB16_STREAM msg=2 status=complete elapsed_ms=\d+ pcm_bytes=5760000 "
    r"sectors_written=\d+ driver_writes=\d+ start_lba=\d+ sample_rate=16000"
)
OVERLAP = re.compile(r"LAB16_OVERLAP status=recording_msg2 upload_session=1_active")
UPLOAD1 = re.compile(r"LAB16_UPLOAD session=1 status=complete uploaded=180 failed=0")
UPLOAD2 = re.compile(r"LAB16_UPLOAD session=2 status=complete uploaded=180 failed=0")
OVERLAP_DONE = re.compile(r"LAB16_OVERLAP status=complete msg1_session=1 msg2_session=2 seconds=180")
HARNESS_OK = re.compile(r"LAB16_HARNESS_DONE status=complete")


def verify(text: str) -> list[str]:
    errors: list[str] = []
    if not READY.search(text):
        errors.append("missing LAB16_READY (v4 overlap harness)")
    if not SD_OK.search(text):
        errors.append("missing successful LAB16_SD detected line")
    if not STREAM1.search(text):
        errors.append("missing LAB16_STREAM msg=1 complete at start_lba=2048 (5760000 PCM bytes)")
    if not OVERLAP.search(text):
        errors.append("missing LAB16_OVERLAP recording_msg2 while upload session 1 active")
    if not STREAM2.search(text):
        errors.append("missing LAB16_STREAM msg=2 complete (5760000 PCM bytes)")
    if not UPLOAD1.search(text):
        errors.append("missing LAB16_UPLOAD session=1 complete uploaded=180")
    if not UPLOAD2.search(text):
        errors.append("missing LAB16_UPLOAD session=2 complete uploaded=180")
    if not OVERLAP_DONE.search(text):
        errors.append("missing LAB16_OVERLAP status=complete")
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
