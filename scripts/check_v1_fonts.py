#!/usr/bin/env python3
"""Fail if firmware/v1 references Montserrat sizes outside the locked v1 set."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
V1_DIR = ROOT / "firmware" / "v1"
ALLOWED = {14, 16, 22, 24, 28, 32}
PAT = re.compile(r"lv_font_montserrat_(\d+)")

def main() -> int:
    bad: list[str] = []
    for path in sorted(V1_DIR.glob("*.c")):
        text = path.read_text()
        for m in PAT.finditer(text):
            size = int(m.group(1))
            if size not in ALLOWED:
                bad.append(f"{path.relative_to(ROOT)}: montserrat_{size}")
    if bad:
        print("v1 font guard: disallowed Montserrat references:", file=sys.stderr)
        for line in bad:
            print(f"  {line}", file=sys.stderr)
        print(f"Allowed sizes: {sorted(ALLOWED)}", file=sys.stderr)
        return 1
    print(f"OK: v1 Montserrat references ⊆ {sorted(ALLOWED)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
