#!/usr/bin/env python3
"""Inspect Opus/Ogg media or a length-prefixed raw Opus packet stream."""

from __future__ import annotations

import argparse
import json
import math
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_WAV = ROOT / "demos/server/h30_opus/fixtures/beep_1s_16k_mono.wav"


def _ffmpeg_probe(path: Path) -> dict | None:
    ff = shutil.which("ffprobe")
    if not ff:
        return None
    cmd = [
        ff,
        "-v",
        "quiet",
        "-print_format",
        "json",
        "-show_format",
        "-show_streams",
        str(path),
    ]
    try:
        out = subprocess.check_output(cmd, text=True)
        return json.loads(out)
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return None


def inspect_file(path: Path) -> dict:
    info: dict = {"path": str(path), "kind": "unknown"}
    probe = _ffmpeg_probe(path)
    if probe:
        fmt = probe.get("format", {})
        info["kind"] = fmt.get("format_name", "unknown")
        info["duration_s"] = float(fmt.get("duration", 0) or 0)
        info["bitrate_bps"] = int(float(fmt.get("bit_rate", 0) or 0))
        streams = probe.get("streams") or []
        if streams:
            st = streams[0]
            info["codec"] = st.get("codec_name")
            info["sample_rate"] = st.get("sample_rate")
            info["channels"] = st.get("channels")
        info["decodable"] = info.get("codec") == "opus" or "opus" in str(info["kind"])
        return info

    if path.suffix.lower() == ".wav":
        with wave.open(str(path), "rb") as wf:
            frames = wf.getnframes()
            rate = wf.getframerate()
            info.update(
                {
                    "kind": "wav",
                    "codec": "pcm_s16le",
                    "duration_s": frames / float(rate),
                    "sample_rate": rate,
                    "channels": wf.getnchannels(),
                    "bitrate_bps": rate * wf.getnchannels() * wf.getsampwidth() * 8,
                    "decodable": True,
                }
            )
        return info

    raise SystemExit(
        f"Cannot inspect {path}: install ffmpeg/ffprobe or use .wav / --raw-packets"
    )


def inspect_raw_packets(path: Path, frame_ms: float = 20.0) -> dict:
    data = path.read_bytes()
    off = 0
    frames = 0
    bytes_total = 0
    while off + 2 <= len(data):
        plen = data[off] | (data[off + 1] << 8)
        off += 2
        if plen <= 0 or off + plen > len(data):
            break
        frames += 1
        bytes_total += plen
        off += plen
    duration_s = frames * (frame_ms / 1000.0)
    bitrate_bps = int((bytes_total * 8) / duration_s) if duration_s > 0 else 0
    return {
        "path": str(path),
        "kind": "raw_length_prefixed_opus",
        "frames": frames,
        "duration_s": duration_s,
        "encoded_bytes": bytes_total,
        "bitrate_bps": bitrate_bps,
        "decodable": frames > 0,
    }


def write_beep_wav(path: Path, seconds: float = 1.0, hz: float = 440.0, rate: int = 16000) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    n = int(rate * seconds)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        frames = bytearray()
        for i in range(n):
            sample = int(0.25 * 32767 * math.sin(2 * math.pi * hz * i / rate))
            frames += struct.pack("<h", sample)
        wf.writeframes(frames)


def host_roundtrip_test(wav: Path) -> None:
    """Encode/decode fixture with ffmpeg when available; else PCM duration check only."""
    if not wav.exists():
        write_beep_wav(wav)
    info = inspect_file(wav)
    dur = info.get("duration_s", 0)
    if dur <= 0:
        raise SystemExit("fixture WAV has zero duration")
    ff = shutil.which("ffmpeg")
    if not ff:
        print(f"OK fixture WAV duration={dur:.3f}s (ffmpeg absent; skipped Opus round-trip)")
        return
    ogg = wav.with_suffix(".ogg")
    pcm_out = wav.with_name(wav.stem + "_rt.wav")
    subprocess.check_call(
        [ff, "-y", "-i", str(wav), "-c:a", "libopus", "-b:a", "16k", str(ogg)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    subprocess.check_call(
        [ff, "-y", "-i", str(ogg), str(pcm_out)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    back = inspect_file(pcm_out)
    if abs(back["duration_s"] - dur) > 0.05:
        raise SystemExit(f"duration mismatch: in={dur} out={back['duration_s']}")
    print(f"OK Opus round-trip duration={dur:.3f}s -> {back['duration_s']:.3f}s")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", help="File to inspect (.ogg, .opus, .wav, or raw packets)")
    ap.add_argument("--raw-packets", action="store_true", help="Length-prefixed Opus packet stream")
    ap.add_argument("--self-test", action="store_true", help="Generate fixture and run host round-trip")
    args = ap.parse_args()

    if args.self_test:
        host_roundtrip_test(FIXTURE_WAV)
        return

    if not args.path:
        ap.error("path required unless --self-test")

    path = Path(args.path)
    if not path.is_file():
        raise SystemExit(f"not found: {path}")

    if args.raw_packets:
        report = inspect_raw_packets(path)
    else:
        report = inspect_file(path)

    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
