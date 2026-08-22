"""a01 — Capture a still. File, webcam, or generated SAMPLE stand-in."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "demos" / "persona"))

from _shared import paths
from _shared.placeholder import write_standin_png


def _ffmpeg_still(dest: Path) -> None:
    exe = shutil.which("ffmpeg")
    if not exe:
        raise SystemExit("ffmpeg not on PATH (needed for --webcam)")
    cmd = [
        exe,
        "-y",
        "-f",
        "avfoundation",
        "-framerate",
        "30",
        "-i",
        "0",
        "-frames:v",
        "1",
        str(dest),
    ]
    print("->", " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=str(ROOT))
    if proc.returncode != 0 or not dest.is_file():
        raise SystemExit("webcam capture failed")


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture a parent still (or a stand-in).")
    parser.add_argument("--in", dest="src", help="Existing photo (jpg/png). Not committed.")
    parser.add_argument("--webcam", action="store_true", help="One frame via ffmpeg avfoundation")
    parser.add_argument("--out", type=Path, default=paths.CAPTURE)
    args = parser.parse_args()
    paths.ensure_data()
    dest = args.out

    if args.src:
        src = Path(args.src).expanduser()
        if not src.is_file():
            print(f"FAIL missing {src}")
            return 1
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dest)
        print(f"copied {src} -> {dest}")
    elif args.webcam:
        dest.parent.mkdir(parents=True, exist_ok=True)
        _ffmpeg_still(dest)
        print(f"webcam -> {dest}")
    else:
        write_standin_png(dest, watermark=True)
        print(f"stand-in SAMPLE portrait -> {dest} (pass --in or --webcam later)")

    if not dest.is_file() or dest.stat().st_size < 64:
        print("FAIL empty capture")
        return 1
    print("-- PASS a01")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
