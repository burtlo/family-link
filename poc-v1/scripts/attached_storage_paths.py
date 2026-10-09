"""Private attached-storage experiment directories (outside the git repo)."""
from __future__ import annotations

import json
import os
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXPERIMENTS_ROOT = Path.home() / "family-link-storage-experiments"

ENV_H35_CAPTURE_DIR = "FAMILY_LINK_H35_CAPTURE_DIR"
CURRENT_H35_CAPTURE_POINTER = EXPERIMENTS_ROOT / "current-h35-capture.json"

# Historical 64 GB-era captures (moved under EXPERIMENTS_ROOT).
H35_CAPTURE_DIR = EXPERIMENTS_ROOT / "family-link-h35-sd-discovery-preflight-20261005"
DEFAULT_BOX_BACKUP_DIR = EXPERIMENTS_ROOT / "family-link-attached-discovery-backup-20261005"


def validate_private_dir(path: Path, *, require_exists: bool = False) -> Path:
    """Resolve path and enforce private-dir policy (outside repo, mode 0700, no symlinks)."""
    path = path.expanduser().resolve()
    if path.is_relative_to(ROOT.resolve()) or path.is_symlink():
        raise ValueError("private directory outside repository required")
    if path.exists():
        if path.stat().st_mode & 0o077:
            raise ValueError("private directory permissions must be 0700")
    elif require_exists:
        raise ValueError("private directory does not exist")
    return path


def ensure_experiments_root() -> Path:
    EXPERIMENTS_ROOT.mkdir(mode=0o700, exist_ok=True)
    return EXPERIMENTS_ROOT


def default_h35_run_dir() -> Path:
    """Suggested private H35 epoch directory: ``EXPERIMENTS_ROOT/h35-YYYYMMDD``."""
    ensure_experiments_root()
    return EXPERIMENTS_ROOT / time.strftime("h35-%Y%m%d")


# Suggested layout for new epochs (operators may pass explicit --run-dir / --backup-dir).
def h35_run_dir(name: str) -> Path:
    return EXPERIMENTS_ROOT / name


def h37_run_dir(name: str) -> Path:
    return EXPERIMENTS_ROOT / name


def h38_run_dir(name: str) -> Path:
    return EXPERIMENTS_ROOT / name


def resolve_h35_capture_dir(explicit: Path | str | None = None) -> Path:
    """Resolve the active private H35 capture directory.

    Precedence: explicit argument, ``FAMILY_LINK_H35_CAPTURE_DIR``, pointer file
    ``current-h35-capture.json`` under ``EXPERIMENTS_ROOT``, then legacy default.
    """
    if explicit is not None:
        return validate_private_dir(Path(explicit), require_exists=True)
    env = os.environ.get(ENV_H35_CAPTURE_DIR)
    if env:
        return validate_private_dir(Path(env), require_exists=True)
    if CURRENT_H35_CAPTURE_POINTER.is_file():
        try:
            data = json.loads(CURRENT_H35_CAPTURE_POINTER.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise ValueError(f"current H35 capture pointer unreadable: {exc}") from exc
        capture = data.get("capture_dir")
        if not capture:
            raise ValueError("current H35 capture pointer missing capture_dir")
        return validate_private_dir(Path(capture), require_exists=True)
    return validate_private_dir(H35_CAPTURE_DIR, require_exists=True)


def write_current_h35_capture_pointer(capture_dir: Path | str) -> Path:
    """Record ``capture_dir`` as the default for tools that resolve H35 evidence."""
    cap = validate_private_dir(Path(capture_dir), require_exists=True)
    ensure_experiments_root()
    payload = json.dumps({"capture_dir": str(cap)}, sort_keys=True) + "\n"
    tmp = CURRENT_H35_CAPTURE_POINTER.with_suffix(".json.tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        with os.fdopen(fd, "w") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp, CURRENT_H35_CAPTURE_POINTER)
        CURRENT_H35_CAPTURE_POINTER.chmod(0o600)
    except BaseException:
        try:
            tmp.unlink()
        except OSError:
            pass
        raise
    return cap
