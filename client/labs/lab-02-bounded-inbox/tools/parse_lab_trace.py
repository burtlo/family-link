#!/usr/bin/env python3
"""Parse LAB_TRACE serial lines into a minimal metrics summary."""

import argparse
import json
import re
import sys
from collections import Counter
from typing import Iterable, TextIO, Union

LINE = re.compile(
    r"LAB_TRACE t_us=(\d+) event=(\S+) gen=(\d+) focus=(-?\d+) msg=(\S+)(?:\s+(.*))?"
)

LineSource = Union[Iterable[str], TextIO]


def parse_trace(lines: LineSource) -> dict:
    """Parse serial capture lines into family-link.lab-metrics/v1 summary."""
    if hasattr(lines, "read"):
        line_iter: Iterable[str] = lines
    else:
        line_iter = lines

    events: Counter[str] = Counter()
    snaps: list[dict] = []
    harness_printf_success = 0
    harness_printf_error = 0

    for line in line_iter:
        m = LINE.search(line)
        if not m:
            if line.startswith("LAB02_READY"):
                events["boot_ready"] += 1
            elif "HARNESS_DONE" in line:
                if "status=" in line:
                    harness_printf_error += 1
                else:
                    harness_printf_success += 1
            elif "LAB02_HTTP_READY" in line:
                events["http_ready"] += 1
            continue
        t_us, event, gen, focus, msg, extra = m.groups()
        events[event] += 1
        if event in ("snap_start", "snap_end"):
            snaps.append({"t_us": int(t_us), "event": event})

    # Success: firmware emits LAB_TRACE event=harness_done then HARNESS_DONE steps=N.
    # Count once — trace is canonical; printf is fallback for captures without trace.
    if events.get("harness_done", 0) == 0 and harness_printf_success > 0:
        events["harness_done"] = harness_printf_success
    if harness_printf_error:
        events["harness_abort"] = harness_printf_error

    return {
        "document": "family-link.lab-metrics/v1",
        "event_counts": dict(events),
        "snap_events": len(snaps),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", nargs="?", type=argparse.FileType("r"), default=sys.stdin)
    ap.add_argument("-o", "--output", help="Write metrics JSON path")
    args = ap.parse_args()

    metrics = parse_trace(args.trace)
    out = json.dumps(metrics, indent=2)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(out)
    else:
        print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
