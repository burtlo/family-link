"""Filesystem commit primitives for the host message-store demo.

Successful file fsync plus parent-directory fsync is requested on each commit.
Some filesystems reject directory fsync; on those hosts the process-crash
recovery protocol still works, but power-loss durability is unqualified.
"""

from __future__ import annotations

import errno
import json
import os
import uuid
from pathlib import Path

DIRECTORY_FSYNC_SUPPORTED = True


def crash_after(boundary: str) -> None:
    """Fault hook for isolated qualification subprocesses, inert by default."""
    if os.environ.get("FAMILY_LINK_H34_CRASH_AFTER") == boundary:
        os._exit(86)


def sync_dir(path: Path) -> bool:
    global DIRECTORY_FSYNC_SUPPORTED
    try:
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    except OSError as exc:
        if exc.errno not in (errno.EINVAL, errno.ENOTSUP, errno.EBADF, errno.EISDIR):
            raise
        DIRECTORY_FSYNC_SUPPORTED = False
    return DIRECTORY_FSYNC_SUPPORTED


def mkdir_durable(path: Path) -> None:
    missing: list[Path] = []
    cursor = path
    while not cursor.exists():
        missing.append(cursor)
        cursor = cursor.parent
    for folder in reversed(missing):
        folder.mkdir()
        sync_dir(folder.parent)


def atomic_bytes(path: Path, payload: bytes, *, boundary: str | None = None,
                 before_replace: str | None = None,
                 after_replace: str | None = None) -> None:
    mkdir_durable(path.parent)
    tmp = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        with tmp.open("xb") as fh:
            fh.write(payload)
            fh.flush()
            os.fsync(fh.fileno())
        if before_replace:
            crash_after(before_replace)
        os.replace(tmp, path)
        if after_replace:
            crash_after(after_replace)
        sync_dir(path.parent)
        if boundary:
            crash_after(boundary)
    finally:
        tmp.unlink(missing_ok=True)


def atomic_json(path: Path, obj: object, *, boundary: str | None = None,
                before_replace: str | None = None,
                after_replace: str | None = None) -> None:
    atomic_bytes(path, (json.dumps(obj, sort_keys=True, separators=(",", ":")) + "\n").encode(),
                 boundary=boundary, before_replace=before_replace, after_replace=after_replace)


def move_dir(src: Path, dst: Path, *, boundary: str | None = None,
             after_parent_create: str | None = None,
             after_rename: str | None = None,
             after_source_sync: str | None = None) -> None:
    mkdir_durable(dst.parent)
    if after_parent_create:
        crash_after(after_parent_create)
    if dst.exists():
        raise FileExistsError(dst)
    os.rename(src, dst)
    if after_rename:
        crash_after(after_rename)
    sync_dir(src.parent)
    if after_source_sync:
        crash_after(after_source_sync)
    sync_dir(dst.parent)
    if boundary:
        crash_after(boundary)


def durable_unlink(path: Path) -> None:
    path.unlink(missing_ok=True)
    sync_dir(path.parent)


def append_jsonl(path: Path, row: dict) -> None:
    mkdir_durable(path.parent)
    with path.open("ab") as fh:
        fh.write((json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n").encode())
        fh.flush()
        os.fsync(fh.fileno())
    sync_dir(path.parent)
