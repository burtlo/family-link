"""a03 — Record or import a short greeting WAV. Trim silence, fade, 16 kHz mono.

Default is a generated hum (not speech). --mic or --in is your voice later.
"""

from __future__ import annotations

import argparse
import array
import shutil
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "demos" / "persona"))

from _shared import paths
from _shared.placeholder import RATE, write_standin_wav

MAX_SECONDS = 3.0


def _read_wav(path: Path) -> tuple[int, array.array]:
    with wave.open(str(path), "rb") as wav:
        ch = wav.getnchannels()
        sw = wav.getsampwidth()
        rate = wav.getframerate()
        n = wav.getnframes()
        raw = wav.readframes(n)
    if sw != 2:
        raise SystemExit(f"{path} must be 16-bit PCM (got sampwidth={sw})")
    samples = array.array("h")
    samples.frombytes(raw)
    if ch == 2:
        samples = array.array("h", ((samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples) - 1, 2)))
    if rate != RATE:
        samples = _resample(samples, rate, RATE)
    return RATE, samples


def _resample(samples: array.array, src: int, dest: int) -> array.array:
    if src == dest:
        return samples
    out = array.array("h")
    n_out = max(1, int(len(samples) * dest / src))
    for i in range(n_out):
        pos = i * (len(samples) - 1) / (n_out - 1) if n_out > 1 else 0
        lo = int(pos)
        hi = min(lo + 1, len(samples) - 1)
        frac = pos - lo
        out.append(int(samples[lo] * (1 - frac) + samples[hi] * frac))
    return out


def _trim_fade(samples: array.array, max_s: float) -> array.array:
    cap = int(RATE * max_s)
    if len(samples) > cap:
        samples = array.array("h", samples[:cap])
    thresh = 400
    start = 0
    while start < len(samples) and abs(samples[start]) < thresh:
        start += 1
    end = len(samples) - 1
    while end > start and abs(samples[end]) < thresh:
        end -= 1
    cut = array.array("h", samples[start : end + 1])
    fade = min(int(RATE * 0.03), max(8, len(cut) // 16))
    for i in range(fade):
        cut[i] = int(cut[i] * i / fade)
        cut[-1 - i] = int(cut[-1 - i] * i / fade)
    return cut


def _write_wav(path: Path, samples: array.array) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(samples.tobytes())


def _ffmpeg_mic(dest: Path, seconds: float) -> None:
    exe = shutil.which("ffmpeg")
    if not exe:
        raise SystemExit("ffmpeg not on PATH (needed for --mic)")
    cmd = [
        exe,
        "-y",
        "-f",
        "avfoundation",
        "-i",
        ":0",
        "-t",
        f"{seconds:.2f}",
        "-ac",
        "1",
        "-ar",
        str(RATE),
        "-sample_fmt",
        "s16",
        str(dest),
    ]
    print("->", " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=str(ROOT))
    if proc.returncode != 0 or not dest.is_file():
        raise SystemExit("mic capture failed")


def main() -> int:
    parser = argparse.ArgumentParser(description="Greeting WAV: file, mic, or stand-in hum.")
    parser.add_argument("--in", dest="src", help="Existing wav/aiff (ffmpeg if not wav)")
    parser.add_argument("--mic", action="store_true")
    parser.add_argument("--seconds", type=float, default=2.0)
    parser.add_argument("--out", type=Path, default=paths.GREETING)
    args = parser.parse_args()
    paths.ensure_data()
    dest = args.out
    seconds = min(MAX_SECONDS, max(0.3, args.seconds))

    if args.src:
        src = Path(args.src).expanduser()
        if not src.is_file():
            print(f"FAIL missing {src}")
            return 1
        if src.suffix.lower() != ".wav":
            exe = shutil.which("ffmpeg")
            if not exe:
                raise SystemExit("ffmpeg needed to convert non-wav --in")
            tmp = dest.with_suffix(".tmp.wav")
            subprocess.check_call(
                [exe, "-y", "-i", str(src), "-ac", "1", "-ar", str(RATE), "-sample_fmt", "s16", str(tmp)]
            )
            src = tmp
        _, samples = _read_wav(src)
        samples = _trim_fade(samples, seconds)
        _write_wav(dest, samples)
        print(f"imported {args.src} -> {dest} ({len(samples) / RATE:.2f}s)")
    elif args.mic:
        _ffmpeg_mic(dest, seconds)
        _, samples = _read_wav(dest)
        samples = _trim_fade(samples, MAX_SECONDS)
        _write_wav(dest, samples)
        print(f"mic -> {dest} ({len(samples) / RATE:.2f}s)")
    else:
        write_standin_wav(dest, seconds=0.9)
        print(f"stand-in hum -> {dest} (not your voice; pass --mic or --in later)")

    if not dest.is_file() or dest.stat().st_size < 100:
        print("FAIL empty wav")
        return 1
    print("-- PASS a03")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
