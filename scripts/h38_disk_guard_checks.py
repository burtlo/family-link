#!/usr/bin/env python3
"""Compile and run host fixtures against the production H38 C disk guard."""

from __future__ import annotations

import os
import pathlib
import shlex
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
GUARD_C = ROOT / "firmware" / "common" / "h38_disk_guard.c"
GUARD_H = ROOT / "firmware" / "common"
FIXTURE_C = ROOT / "scripts" / "fixtures" / "h38_disk_guard_checks.c"


def main() -> int:
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        print("CC resolved to an empty compiler command", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="h38-disk-guard-") as temp_dir:
        executable = pathlib.Path(temp_dir) / "h38_disk_guard_checks"
        build = subprocess.run(
            [
                *compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(GUARD_H),
                str(GUARD_C),
                str(FIXTURE_C),
                "-o",
                str(executable),
            ],
            cwd=ROOT,
            check=False,
        )
        if build.returncode != 0:
            return build.returncode

        run = subprocess.run([str(executable)], cwd=ROOT, check=False)
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
