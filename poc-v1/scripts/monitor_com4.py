#!/usr/bin/env python3
"""Capture COM4 serial for a timed window (UTF-8 safe on Windows)."""
from __future__ import annotations

import sys
import time
from pathlib import Path

import serial

SECONDS = int(sys.argv[1]) if len(sys.argv) > 1 else 90
PORT = sys.argv[2] if len(sys.argv) > 2 else "COM4"
OUT = Path("logs") / "pin-attempt.txt"
OUT.parent.mkdir(parents=True, exist_ok=True)

ser = serial.Serial(PORT, 115200, timeout=0.25)
t0 = time.time()
chunks: list[str] = []
print(f"listening {SECONDS}s on {PORT} — pick user, enter PIN now", flush=True)
try:
    while time.time() - t0 < SECONDS:
        raw = ser.read(8192)
        if raw:
            text = raw.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            chunks.append(text)
finally:
    ser.close()
    OUT.write_text("".join(chunks), encoding="utf-8")
    print(f"\n-> wrote {OUT}", flush=True)
