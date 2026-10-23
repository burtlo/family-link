#!/usr/bin/env python3
"""Run P10 L1 scripted overlap scenarios (no HTTP)."""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JOURNEY = ROOT / "client/shared/product_journey"
LOGIN = ROOT / "client/shared/login_session/fl_product_login"
INBOX = ROOT / "client/shared/inbox_carousel/fl_product_inbox"
VOLUME = ROOT / "client/shared/desk_volume/fl_product_volume"
SEND = ROOT / "client/shared/message_delivery/fl_product_send"
SHELL = ROOT / "client/shared/stage_shell/fl_shell_model"

INCLUDES = [
    JOURNEY / "include",
    LOGIN / "include",
    INBOX / "include",
    VOLUME / "include",
    SEND / "include",
    SHELL / "include",
]

SOURCES = [
    JOURNEY / "journey.cpp",
    LOGIN / "session.cpp",
    LOGIN / "pin_session.cpp",
    LOGIN / "scoped_session.cpp",
    LOGIN / "credential_session.cpp",
    LOGIN / "pollers.cpp",
    LOGIN / "p1_wire.cpp",
    LOGIN / "workers.cpp",
    INBOX / "carousel.cpp",
    INBOX / "p1_wire.cpp",
    INBOX / "presentation.cpp",
    INBOX / "flsk2.cpp",
    INBOX / "workers.cpp",
    VOLUME / "journey.cpp",
    VOLUME / "session.cpp",
    VOLUME / "stops.cpp",
    VOLUME / "storage.cpp",
    SEND / "classify.cpp",
    SEND / "journey.cpp",
    SEND / "outbox.cpp",
    SEND / "p1_wire.cpp",
    SEND / "picker.cpp",
    SEND / "sha256.cpp",
    SHELL / "fl_shell_model.c",
    JOURNEY / "tests/product_journey_scripted_test.cpp",
]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no C++ compiler on PATH")


def build(binary: Path) -> None:
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if cc is None:
        raise RuntimeError("no C compiler on PATH")
    cxx = find_cxx()
    includes = []
    for path in INCLUDES:
        includes.extend(["-I", str(path)])
    objects = []
    obj_dir = binary.parent / "objs"
    obj_dir.mkdir(parents=True, exist_ok=True)
    for src in SOURCES:
        rel = src.relative_to(ROOT)
        obj = obj_dir / (str(rel).replace("/", "_").replace("\\", "_") + ".o")
        if src.suffix == ".c":
            subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", *includes, "-c", str(src), "-o", str(obj)],
                check=True,
                cwd=ROOT,
            )
        else:
            subprocess.run(
                [cxx, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic", *includes, "-c", str(src), "-o", str(obj)],
                check=True,
                cwd=ROOT,
            )
        objects.append(obj)
    subprocess.run([cxx, "-std=c++14", *map(str, objects), "-o", str(binary)], check=True, cwd=ROOT)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", nargs="?", default="all", help="scenario id or 'all'")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="p10-l1-") as tmp:
        binary = Path(tmp) / "product_journey_scripted_test"
        build(binary)
        completed = subprocess.run([str(binary), args.scenario], cwd=ROOT)
        return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
