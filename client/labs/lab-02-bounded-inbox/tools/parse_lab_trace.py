#!/usr/bin/env python3
"""Parse LAB_TRACE serial lines into a minimal metrics summary."""

import argparse
import json
import re
import sys
from collections import Counter

LINE = re.compile(
    r"LAB_TRACE t_us=(\d+) event=(\S+) gen=(\d+) focus=(-?\d+) msg=(\S+)(?:\s+(.*))?"
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", nargs="?", type=argparse.FileType("r"), default=sys.stdin)
    ap.add_argument("-o", "--output", help="Write metrics JSON path")
    args = ap.parse_args()

    events = Counter()
    snaps = []
    for line in args.trace:
        m = LINE.search(line)
        if not m:
            if line.startswith("LAB02_READY"):
                events["boot_ready"] += 1
            elif "HARNESS_DONE" in line:
                events["harness_done"] += 1
            elif "LAB02_HTTP_READY" in line:
                events["http_ready"] += 1
            continue
        t_us, event, gen, focus, msg, extra = m.groups()
        events[event] += 1
        if event in ("snap_start", "snap_end"):
            snaps.append({"t_us": int(t_us), "event": event})

    metrics = {
        "document": "family-link.lab-metrics/v1",
        "event_counts": dict(events),
        "snap_events": len(snaps),
    }
    out = json.dumps(metrics, indent=2)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(out)
    else:
        print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
