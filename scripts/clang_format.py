#!/usr/bin/env python3
"""Run clang-format on active-repo C/C++ sources (see docs/standards/code-formatting.md)."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {".c", ".cpp", ".cc", ".cxx", ".h", ".hpp"}
SKIP_DIR_NAMES = {
    "poc-v1",
    "managed_components",
    "external",
    "build",
    ".git",
    ".venv",
    "node_modules",
}
SCAN_ROOTS = (ROOT / "client", ROOT / "server")


def clang_format_exe() -> Path:
    venv = ROOT / ".venv"
    if sys.platform == "win32":
        candidate = venv / "Scripts" / "clang-format.exe"
    else:
        candidate = venv / "bin" / "clang-format"
    if candidate.is_file():
        return candidate
    return Path("clang-format")


def iter_sources() -> list[Path]:
    files: list[Path] = []
    for scan_root in SCAN_ROOTS:
        if not scan_root.is_dir():
            continue
        for path in scan_root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix.lower() not in EXTENSIONS:
                continue
            if any(part in SKIP_DIR_NAMES for part in path.parts):
                continue
            files.append(path)
    return sorted(files)


def run_format(paths: list[Path], check: bool) -> int:
    exe = clang_format_exe()
    if not paths:
        print("clang_format: no files in scope", file=sys.stderr)
        return 0

    args = [str(exe), f"-style=file:{ROOT / '.clang-format'}"]
    if check:
        args.extend(["--dry-run", "--Werror"])
    else:
        args.append("-i")

    failed = 0
    for path in paths:
        cmd = args + [str(path)]
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            failed += 1
            rel = path.relative_to(ROOT)
            print(f"clang-format failed: {rel}", file=sys.stderr)
            if proc.stdout:
                print(proc.stdout, file=sys.stderr)
            if proc.stderr:
                print(proc.stderr, file=sys.stderr)
    if check and failed:
        print(f"clang-format: {failed} file(s) need formatting", file=sys.stderr)
    elif not check:
        print(f"clang-format: formatted {len(paths)} file(s)")
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify formatting without writing (non-zero if changes needed)",
    )
    args = parser.parse_args()
    return run_format(iter_sources(), check=args.check)


if __name__ == "__main__":
    raise SystemExit(main())
