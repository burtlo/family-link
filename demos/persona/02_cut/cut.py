"""a02 — Cut a square idle portrait from the capture. Optional --box x,y,w,h."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "demos" / "persona"))

from _shared import paths

SIZE = 200


def parse_box(raw: str) -> tuple[int, int, int, int]:
    parts = [int(p.strip()) for p in raw.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("box must be x,y,w,h")
    return parts[0], parts[1], parts[2], parts[3]


def center_square(im: Image.Image) -> Image.Image:
    w, h = im.size
    side = min(w, h)
    x = (w - side) // 2
    y = (h - side) // 2
    return im.crop((x, y, x + side, y + side))


def desk_contrast(im: Image.Image) -> Image.Image:
    im = ImageOps.autocontrast(im, cutoff=2)
    return ImageEnhance.Contrast(im).enhance(1.15)


def main() -> int:
    parser = argparse.ArgumentParser(description="Crop a 200×200 idle face.")
    parser.add_argument("--in", dest="src", type=Path, default=paths.CAPTURE)
    parser.add_argument("--out", type=Path, default=paths.IDLE)
    parser.add_argument("--box", type=parse_box, help="x,y,w,h in capture pixels")
    parser.add_argument("--size", type=int, default=SIZE)
    args = parser.parse_args()
    paths.ensure_data()

    src = args.src
    if not src.is_file():
        print(f"FAIL missing {src} (run a01 first, or --in a photo)")
        return 1

    im = Image.open(src).convert("RGB")
    if args.box:
        x, y, w, h = args.box
        im = im.crop((x, y, x + w, y + h))
    else:
        im = center_square(im)
    im = im.resize((args.size, args.size), Image.Resampling.LANCZOS)
    im = desk_contrast(im)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    im.save(args.out, "PNG")
    print(f"cut {src.name} -> {args.out} {args.size}x{args.size}")
    print("-- PASS a02")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
