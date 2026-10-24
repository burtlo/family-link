"""Shared serial log checks (ACC-NEG)."""
from __future__ import annotations

import re

FORBIDDEN_PATTERNS = (
    (re.compile(r"Guru Meditation Error", re.I), "Guru Meditation"),
    (re.compile(r"Stack overflow", re.I), "stack overflow"),
    (re.compile(r"\babort\(\)", re.I), "abort()"),
    (re.compile(r"Brownout detector", re.I), "brownout"),
    (re.compile(r"panic", re.I), "panic"),
)


def serial_forbidden_errors(text: str) -> list[str]:
    errors: list[str] = []
    for pattern, label in FORBIDDEN_PATTERNS:
        if pattern.search(text):
            errors.append(f"forbidden serial pattern: {label}")
    return errors


# Lab 16 scripted PCM is 16-bit mono. 6_720_000 bytes at 16 kHz is 210 seconds.
_STREAM_DONE = re.compile(
    r"elapsed_ms=(\d+)\s+pcm_bytes=(\d+)\b(?:(?!elapsed_ms=).){0,240}?sample_rate=(\d+)",
    re.S,
)


def stream_duration_errors(text: str, *, bytes_per_sample: int = 2) -> list[str]:
    """Reject a capture whose wall clock contradicts PCM duration."""
    errors: list[str] = []
    for match in _STREAM_DONE.finditer(text):
        elapsed_ms = int(match.group(1))
        pcm_bytes = int(match.group(2))
        sample_rate = int(match.group(3))
        if sample_rate <= 0 or bytes_per_sample <= 0 or pcm_bytes <= 0:
            errors.append("LAB16 stream duration: sample_rate or pcm_bytes is not usable")
            continue
        expected_ms = (pcm_bytes / (sample_rate * bytes_per_sample)) * 1000.0
        if elapsed_ms < expected_ms * 0.5 or elapsed_ms > expected_ms * 2.0:
            errors.append(
                "LAB16 stream duration contradicts payload: "
                f"elapsed_ms={elapsed_ms} pcm_bytes={pcm_bytes} "
                f"sample_rate={sample_rate} expected_ms={expected_ms:.0f}"
            )
    return errors


def require_substrings(text: str, required: tuple[str, ...]) -> list[str]:
    errors: list[str] = []
    for item in required:
        if item not in text:
            errors.append(f"missing required log substring: {item}")
    return errors
