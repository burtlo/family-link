#!/usr/bin/env python3
"""Pass/fail for LAB-16 live stream + SD failover during a single recording."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

READY = re.compile(r"LAB16_READY build=\S+ mode=scripted fixture=lab16-sd/v5 harness=1")
SD_OK = re.compile(
    r"LAB16_SD status=detected sectors=\d+ sector_bytes=512 capacity_bytes=\d+ "
    r"bus_width=\d+ real_freq_khz=\d+ read_lba0=ok probe_writes=0"
)
LIVE_STREAM = re.compile(r"LAB16_LIVE status=streaming session=1 seconds=180")
FAILOVER = re.compile(r"LAB16_FAILOVER seq=\d+ reason=http_")
STREAM_OK = re.compile(
    r"LAB16_STREAM status=complete elapsed_ms=\d+ pcm_bytes=5760000 "
    r"sectors_written=\d+ driver_writes=\d+ start_lba=2048 sample_rate=16000"
)
DRAIN_OK = re.compile(r"LAB16_LIVE status=drain complete recovered=\d+ failed=0 failover_events=\d+")
LIVE_DONE = re.compile(r"LAB16_LIVE status=complete live_uploaded=\d+ drained=\d+ failover_events=\d+")
UPLOAD_OK = re.compile(r"LAB16_UPLOAD session=1 status=complete uploaded=180 failed=0")
HARNESS_OK = re.compile(r"LAB16_HARNESS_DONE status=complete")


def verify(text: str) -> list[str]:
    errors: list[str] = []
    if not READY.search(text):
        errors.append("missing LAB16_READY (v5 live-stream harness)")
    if not SD_OK.search(text):
        errors.append("missing successful LAB16_SD detected line")
    if not LIVE_STREAM.search(text):
        errors.append("missing LAB16_LIVE status=streaming")
    if not FAILOVER.search(text):
        errors.append("missing LAB16_FAILOVER (expected server outage during record)")
    if not STREAM_OK.search(text):
        errors.append("missing LAB16_STREAM status=complete with 5760000 PCM bytes")
    if not DRAIN_OK.search(text):
        errors.append("missing LAB16_LIVE drain complete with failed=0")
    if not LIVE_DONE.search(text):
        errors.append("missing LAB16_LIVE status=complete")
    if not UPLOAD_OK.search(text):
        errors.append("missing LAB16_UPLOAD session=1 uploaded=180")
    if not HARNESS_OK.search(text):
        errors.append("missing LAB16_HARNESS_DONE status=complete")
    m = re.search(r"failover_events=(\d+)", text)
    if m and int(m.group(1)) < 1:
        errors.append("failover_events must be >= 1")
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
