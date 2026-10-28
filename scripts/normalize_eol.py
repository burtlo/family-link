#!/usr/bin/env python3
"""Normalize tracked text files to LF line endings (see docs/standards/code-formatting.md)."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Keep CRLF on Windows launcher scripts (.gitattributes).
CRLF_SUFFIXES = {".bat", ".cmd"}

# Do not rewrite known binary types (content may contain 0x0D bytes).
BINARY_SUFFIXES = {
    ".png",
    ".wav",
    ".jpg",
    ".jpeg",
    ".gif",
    ".webp",
    ".ico",
    ".bin",
    ".elf",
    ".a",
    ".o",
    ".zip",
    ".gz",
    ".woff",
    ".woff2",
    ".ttf",
    ".mp3",
    ".pyc",
}


def git_ls_files() -> list[Path]:
    proc = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    raw = proc.stdout
    if not raw:
        return []
    return [Path(p.decode("utf-8")) for p in raw.split(b"\0") if p]


def is_binary_path(rel: Path) -> bool:
    return rel.suffix.lower() in BINARY_SUFFIXES


def allows_crlf(rel: Path) -> bool:
    return rel.suffix.lower() in CRLF_SUFFIXES


def normalize_bytes(data: bytes) -> bytes:
    if b"\r" not in data:
        return data
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def audit(check_only: bool) -> int:
    changed: list[Path] = []
    for rel in git_ls_files():
        if allows_crlf(rel) or is_binary_path(rel):
            continue
        path = ROOT / rel
        if not path.is_file():
            continue
        original = path.read_bytes()
        if b"\r" not in original:
            continue
        normalized = normalize_bytes(original)
        if normalized == original:
            continue
        changed.append(rel)
        if not check_only:
            path.write_bytes(normalized)

    if check_only:
        if changed:
            for rel in changed[:20]:
                print(f"CRLF: {rel.as_posix()}", file=sys.stderr)
            if len(changed) > 20:
                print(f"... and {len(changed) - 20} more", file=sys.stderr)
            print(f"eol: {len(changed)} file(s) need LF normalization", file=sys.stderr)
            return 1
        print("eol: all tracked text files use LF")
        return 0

    print(f"eol: normalized {len(changed)} file(s) to LF")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Report files that still contain CR without modifying them",
    )
    args = parser.parse_args()
    return audit(check_only=args.check)


if __name__ == "__main__":
    raise SystemExit(main())
