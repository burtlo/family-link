#!/usr/bin/env python3
"""You on the Mac: drop a JPEG into the box inbox (parent → child photo).

Uses the same HTTP as the phone will: POST /v1/messages kind=image as the
peer's token. Combined host serves GET /v1/messages/{seq}/preview as
320x240 RGB565 for the box.

  python -m demos.server.combined.server --host 0.0.0.0 --port 8080
  python demos/parent/send_photo.py --base-url http://192.168.x.x:8080
  python demos/parent/send_photo.py --base-url … --in ~/snap.jpg --as box-b
"""

from __future__ import annotations

import argparse
import sys
from io import BytesIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import httpx

from demos.server._shared.registry import load_devices


def colored_jpeg() -> bytes:
    from PIL import Image, ImageDraw

    img = Image.new("RGB", (640, 480), (40, 120, 200))
    draw = ImageDraw.Draw(img)
    draw.rectangle((80, 60, 560, 420), fill=(220, 80, 60))
    buf = BytesIO()
    img.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--as", dest="as_id", default="box-b", help="sender registry id")
    parser.add_argument("--in", dest="in_path", help="JPEG to upload (default: generated 640x480)")
    args = parser.parse_args()

    devices = load_devices()
    me = devices[args.as_id]
    base = args.base_url.rstrip("/")
    headers = {"Authorization": f"Bearer {me.token}"}

    if args.in_path:
        blob = Path(args.in_path).expanduser().read_bytes()
        name = Path(args.in_path).name
    else:
        blob = colored_jpeg()
        name = "generated.jpg"

    r = httpx.post(
        f"{base}/v1/messages",
        headers=headers,
        data={"kind": "image"},
        files={"blob": (name, blob, "image/jpeg")},
        timeout=30.0,
    )
    print(f"image {r.status_code} {r.text}")
    r.raise_for_status()
    print(f"inbox of peer {me.peer} got {name} ({len(blob)} bytes)")
    print("-- PASS send_photo")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"FAIL {exc}")
        sys.exit(1)
