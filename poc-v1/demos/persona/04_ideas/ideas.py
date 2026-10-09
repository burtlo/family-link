"""a04 — Stylized avatar ideas from one idle crop. Does not pick the product look."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "demos" / "persona"))

from _shared import paths


def desk(im: Image.Image) -> Image.Image:
    return ImageEnhance.Contrast(ImageOps.autocontrast(im)).enhance(1.25)


def poster(im: Image.Image) -> Image.Image:
    return im.convert("P", palette=Image.Palette.ADAPTIVE, colors=8).convert("RGB")


def circle(im: Image.Image) -> Image.Image:
    side = 200
    im = im.resize((side, side), Image.Resampling.LANCZOS)
    mask = Image.new("L", (side, side), 0)
    from PIL import ImageDraw

    ImageDraw.Draw(mask).ellipse((2, 2, side - 3, side - 3), fill=255)
    bg = Image.new("RGB", (320, 240), (0x14, 0x30, 0x44))
    body = Image.new("RGB", (220, 96), (0x2A, 0x6A, 0x6E))
    bg.paste(body, (50, 148))
    rgba = im.convert("RGBA")
    rgba.putalpha(mask)
    bg.paste(rgba, (60, 6), rgba)
    return bg


def pixel(im: Image.Image) -> Image.Image:
    tiny = im.resize((40, 40), Image.Resampling.BILINEAR)
    return tiny.resize(im.size, Image.Resampling.NEAREST)


def ink(im: Image.Image) -> Image.Image:
    g = ImageOps.grayscale(im)
    g = ImageOps.autocontrast(g)
    g = g.point(lambda p: 255 if p > 110 else 40)
    return g.convert("RGB")


def warm(im: Image.Image) -> Image.Image:
    overlay = Image.new("RGB", im.size, (0xF2, 0xC0, 0x7A))
    return Image.blend(im, overlay, 0.28)


VARIANTS = {
    "desk": desk,
    "poster": poster,
    "circle": circle,
    "pixel": pixel,
    "ink": ink,
    "warm": warm,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Write look-variants next to the idle crop.")
    parser.add_argument("--in", dest="src", type=Path, default=paths.IDLE)
    parser.add_argument("--out", type=Path, default=paths.IDEAS)
    args = parser.parse_args()
    paths.ensure_data()
    if not args.src.is_file():
        print(f"FAIL missing {args.src} (run a02 first)")
        return 1
    im = Image.open(args.src).convert("RGB")
    args.out.mkdir(parents=True, exist_ok=True)
    for name, fn in VARIANTS.items():
        dest = args.out / f"{name}.png"
        fn(im).save(dest, "PNG")
        print(f"idea {name} -> {dest}")
    print("-- PASS a04")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
