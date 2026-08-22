"""Load the manual device registry. Used by server demos; not a framework.

Looks for devices.local.yaml then devices.example.yaml at the repo root.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML missing. Run: make install-server") from exc


@dataclass(frozen=True)
class Device:
    id: str
    token: str
    role: str
    peer: str


def registry_path() -> Path:
    local = ROOT / "devices.local.yaml"
    if local.is_file():
        return local
    example = ROOT / "devices.example.yaml"
    if example.is_file():
        return example
    raise FileNotFoundError("devices.example.yaml missing at repo root")


def load_devices(path: Path | None = None) -> dict[str, Device]:
    dest = path or registry_path()
    raw = yaml.safe_load(dest.read_text(encoding="utf-8")) or {}
    out: dict[str, Device] = {}
    for row in raw.get("devices") or []:
        dev = Device(
            id=str(row["id"]),
            token=str(row["token"]),
            role=str(row.get("role") or "child"),
            peer=str(row.get("peer") or ""),
        )
        out[dev.id] = dev
    if not out:
        raise ValueError(f"no devices in {dest}")
    return out


def device_for_token(token: str, devices: dict[str, Device] | None = None) -> Device | None:
    table = devices if devices is not None else load_devices()
    for dev in table.values():
        if dev.token == token:
            return dev
    return None


def parse_bearer(header: str | None) -> str | None:
    if not header:
        return None
    parts = header.split(None, 1)
    if len(parts) != 2 or parts[0].lower() != "bearer":
        return None
    token = parts[1].strip()
    return token or None
