#!/usr/bin/env python3
"""Run persona-asset host demos in order (fixtures, no family media).

  python scripts/run_persona_demo.py
  python scripts/run_persona_demo.py 02_cut
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEMOS = ROOT / "demos" / "persona"
ORDER = [
    ("01_capture", "capture.py"),
    ("02_cut", "cut.py"),
    ("03_record", "record.py"),
    ("04_ideas", "ideas.py"),
    ("05_pack", "pack.py"),
]


def _venv_python() -> str:
    if os.name == "nt":
        cand = ROOT / ".venv" / "Scripts" / "python.exe"
    else:
        cand = ROOT / ".venv" / "bin" / "python"
    if cand.is_file():
        return str(cand)
    return sys.executable


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("name", nargs="?", help="e.g. 01_capture; default is all")
    args = parser.parse_args(argv)
    py = _venv_python()
    env = os.environ.copy()
    env["PYTHONPATH"] = str(ROOT) + os.pathsep + env.get("PYTHONPATH", "")
    jobs = ORDER
    if args.name:
        jobs = [row for row in ORDER if row[0] == args.name]
        if not jobs:
            print(f"unknown {args.name}", file=sys.stderr)
            return 2
    for folder, script in jobs:
        path = DEMOS / folder / script
        print(f"== {folder}", flush=True)
        rc = subprocess.run([py, str(path)], cwd=str(ROOT), env=env).returncode
        if rc != 0:
            print(f"FAIL {folder} exit {rc}", file=sys.stderr)
            return rc
    if not args.name:
        print("-- PASS demos-persona")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
