"""Generated stand-ins so the pipeline works before any family media exists."""

from __future__ import annotations

import math
import wave
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


W, H = 320, 240
RATE = 16000


def draw_standin_portrait(watermark: bool = True) -> Image.Image:
    """Geometric adult-coded stand-in. Not a family photo. Same palette as p01."""
    im = Image.new("RGB", (W, H), (0x14, 0x30, 0x44))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((50, 148, 270, 240), radius=36, fill=(0x2A, 0x6A, 0x6E))
    d.ellipse((60, 6, 260, 206), fill=(0xE8, 0xC4, 0x8A))
    d.ellipse((92, 58, 148, 114), fill=(0xFF, 0xF6, 0xE8))
    d.ellipse((172, 58, 228, 114), fill=(0xFF, 0xF6, 0xE8))
    d.ellipse((106, 72, 134, 100), fill=(0x1A, 0x1A, 0x1A))
    d.ellipse((186, 72, 214, 100), fill=(0x1A, 0x1A, 0x1A))
    d.rounded_rectangle((88, 48, 148, 58), radius=4, fill=(0x3A, 0x28, 0x18))
    d.rounded_rectangle((172, 48, 232, 58), radius=4, fill=(0x3A, 0x28, 0x18))
    d.rounded_rectangle((100, 138, 220, 168), radius=12, fill=(0xC0, 0x45, 0x3C))
    if watermark:
        try:
            font = ImageFont.load_default()
        except Exception:
            font = None
        d.text((8, 8), "SAMPLE", fill=(0xE8, 0xF0, 0xE8), font=font)
    return im


def write_standin_png(dest: Path, watermark: bool = True) -> Path:
    dest.parent.mkdir(parents=True, exist_ok=True)
    draw_standin_portrait(watermark=watermark).save(dest, "PNG")
    return dest


def write_standin_wav(dest: Path, seconds: float = 0.9) -> Path:
    """Warm hum, not speech. Replace with --mic / --in when you record."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    n = int(RATE * seconds)
    fade = min(int(RATE * 0.04), n // 8)
    frames = bytearray()
    for i in range(n):
        t = i / RATE
        s = 0.45 * math.sin(2 * math.pi * 196 * t)
        s += 0.22 * math.sin(2 * math.pi * 392 * t)
        s += 0.08 * math.sin(2 * math.pi * 247 * t)
        env = 1.0
        if i < fade:
            env = i / fade
        if i > n - fade:
            env = (n - i) / fade
        v = int(max(-1.0, min(1.0, s * env)) * 12000)
        frames += int(v).to_bytes(2, "little", signed=True)
    with wave.open(str(dest), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(bytes(frames))
    return dest
