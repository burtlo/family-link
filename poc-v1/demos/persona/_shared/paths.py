"""Output locations for persona-asset demos. Personal files stay under data/ (gitignored)."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / "data" / "persona"
CAPTURE = DATA / "capture.png"
CUT_DIR = DATA / "cut"
IDLE = CUT_DIR / "idle.png"
IDEAS = DATA / "ideas"
GREETING = DATA / "greeting.wav"
EXAMPLE = ROOT / "firmware" / "assets" / "persona.example"
PERSONAL = ROOT / "firmware" / "assets" / "persona"


def ensure_data() -> Path:
    DATA.mkdir(parents=True, exist_ok=True)
    CUT_DIR.mkdir(parents=True, exist_ok=True)
    IDEAS.mkdir(parents=True, exist_ok=True)
    return DATA
