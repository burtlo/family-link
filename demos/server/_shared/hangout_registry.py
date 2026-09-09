"""Load hangout / user / endpoint registry for v1 product server."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML missing. Run: make install-server") from exc


@dataclass(frozen=True)
class Hangout:
    id: str
    name: str


@dataclass(frozen=True)
class User:
    id: str
    name: str
    pin: str
    web_admin: bool = False
    web_password: str = ""


@dataclass(frozen=True)
class Endpoint:
    id: str
    token: str
    hangout_id: str


@dataclass(frozen=True)
class HangoutRegistry:
    hangout: Hangout
    users: dict[str, User]
    endpoints: dict[str, Endpoint]
    welcome_wav: Path | None


def registry_path() -> Path:
    local = ROOT / "hangout.local.yaml"
    if local.is_file():
        return local
    example = ROOT / "hangout.example.yaml"
    if example.is_file():
        return example
    raise FileNotFoundError("hangout.example.yaml missing at repo root")


def load_registry(path: Path | None = None) -> HangoutRegistry:
    dest = path or registry_path()
    raw = yaml.safe_load(dest.read_text(encoding="utf-8")) or {}
    h = raw.get("hangout") or {}
    hangout = Hangout(
        id=str(h.get("id") or "family"),
        name=str(h.get("name") or "Family"),
    )
    users: dict[str, User] = {}
    for row in raw.get("users") or []:
        uid = str(row["id"])
        users[uid] = User(
            id=uid,
            name=str(row.get("name") or uid),
            pin=str(row.get("pin") or ""),
            web_admin=bool(row.get("web_admin")),
            web_password=str(row.get("web_password") or ""),
        )
    if not users:
        raise ValueError(f"no users in {dest}")
    endpoints: dict[str, Endpoint] = {}
    for row in raw.get("endpoints") or []:
        eid = str(row["id"])
        endpoints[eid] = Endpoint(
            id=eid,
            token=str(row["token"]),
            hangout_id=str(row.get("hangout_id") or hangout.id),
        )
    welcome = raw.get("welcome_wav")
    welcome_path = None
    if welcome:
        p = Path(str(welcome))
        welcome_path = p if p.is_absolute() else ROOT / p
    return HangoutRegistry(
        hangout=hangout,
        users=users,
        endpoints=endpoints,
        welcome_wav=welcome_path,
    )


def endpoint_for_token(
    token: str, registry: HangoutRegistry | None = None
) -> Endpoint | None:
    table = registry if registry is not None else load_registry()
    for ep in table.endpoints.values():
        if ep.token == token:
            return ep
    return None


def user_ids_in_hangout(registry: HangoutRegistry) -> list[str]:
    return list(registry.users.keys())
