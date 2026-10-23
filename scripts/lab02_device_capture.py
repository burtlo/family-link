#!/usr/bin/env python3
"""Deprecated: use scripts/lab_host.py or make lab.lab02."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    print("Note: lab02_device_capture.py is deprecated; use: make lab.lab02", file=sys.stderr)
    argv = [sys.executable, str(ROOT / "scripts" / "lab_host.py"), "run", "lab02"]
    argv.extend(sys.argv[1:])
    return subprocess.call(argv)


if __name__ == "__main__":
    raise SystemExit(main())
