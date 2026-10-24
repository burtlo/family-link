#!/usr/bin/env python3
"""Product v2 desk wire server (P1 core-journey contract revisions)."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import secrets
import signal
import ssl
import struct
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field, replace
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit
from ipaddress import ip_address

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from server_log import BoundedRedactingLog, redact_line  # noqa: E402

DEVICE_DIR = Path(__file__).resolve().parent.parent
FIXTURE_DIR = DEVICE_DIR / "fixtures"
MANIFEST_PATH = FIXTURE_DIR / "manifest.json"
SERVER_MANIFEST = DEVICE_DIR / "manifest.device.json"
DEFAULT_DEPLOYMENT_PATH = FIXTURE_DIR / "deployment.yaml"
FIXTURE_NAME = "p1-core-journey/v1"
PART_SIZE = 16384
# The delivery contract permits 180 seconds of 16 kHz, mono, signed 16-bit PCM.
MAX_AUDIO_BYTES = 180_000 * 16_000 * 2 // 1000
# The pilot's sketch buffer is capped at 32,768 eight-byte flsk2 points.
MAX_SKETCH_POINTS = 32_768
MAX_SKETCH_BYTES = MAX_SKETCH_POINTS * 8
MAX_TOTAL_MEDIA_BYTES = MAX_AUDIO_BYTES + MAX_SKETCH_BYTES
MAX_AUDIO_PARTS = (MAX_AUDIO_BYTES + PART_SIZE - 1) // PART_SIZE
MAX_SKETCH_PARTS = (MAX_SKETCH_BYTES + PART_SIZE - 1) // PART_SIZE
# Four simultaneous open uploads bounds the four-user pilot's disk reservation.
MAX_OPEN_UPLOADS = 4
MAX_TOTAL_OPEN_UPLOAD_BYTES = MAX_OPEN_UPLOADS * MAX_TOTAL_MEDIA_BYTES
MEDIA_IO_CHUNK_SIZE = PART_SIZE
INBOX_MAX_BYTES = 262144
KNOWN_CARD_BACKGROUNDS = frozenset(f"card-bg-{index:02d}" for index in range(1, 9))
PIN_RETENTION_S = 60
LAST_VIEWED_RETENTION_S = 60
SESSION_TTL_S = 180
OPEN_UPLOAD_TTL_S = 24 * 60 * 60
# Maximum time a connected client may leave an HTTP request body incomplete.
# This bounds handler lifetime during restart without changing normal requests.
REQUEST_TIMEOUT_S = 5.0
HEALTH_REASONS = frozenset({"boot", "periodic", "connectivity"})
PIN_RE = re.compile(r"^[1-9]{4}$")
UUID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
    re.I,
)
MESSAGE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,38}$")
TELEMETRY_SCHEMA = "family-link.server-telemetry/1"
TELEMETRY_MAX_FIELD_BYTES = 128
TELEMETRY_REASON_RE = re.compile(r"[^a-z0-9_]+")


class IdentityError(RuntimeError):
    """Fixture bytes changed without a revision bump."""


class StorageReadError(OSError):
    """A required durable media read failed and should be retried."""


class DeploymentLoadError(ValueError):
    """The server deployment roster could not be loaded or was not pilot-safe."""


def verify_fixture_tree(fixture_dir: Path = FIXTURE_DIR) -> dict:
    manifest_path = fixture_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    revision = manifest.get("fixture_revision")
    if not revision:
        raise IdentityError("fixture manifest has no fixture_revision")
    for name, meta in (manifest.get("files") or {}).items():
        data = (fixture_dir / name).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != meta["bytes"] or digest != meta["sha256"]:
            raise IdentityError(f"fixture byte change without revision bump: {name} revision {revision}")
    return manifest


def _utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _b64url_token(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def _redact_text(text: str) -> str:
    text = redact_line(text.encode("utf-8")).decode("utf-8", errors="replace")
    text = re.sub(r'"pin"\s*:\s*"[^"]*"', '"pin":"[REDACTED]"', text)
    text = re.sub(r"(?i)(pin|password)\s*[=:]\s*\S+", r"\1=[REDACTED]", text)
    return text


def _canonical_json(value: dict) -> str:
    """Stable identity representation for replay comparisons."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _normalized_reason(value: Any) -> str | None:
    """Return a short, stable reason suitable for a diagnostic event."""
    if not isinstance(value, str):
        return None
    normalized = TELEMETRY_REASON_RE.sub("_", value.strip().lower()).strip("_")
    if not normalized:
        return None
    return normalized[:64]


def _normalized_identifier(value: Any, pattern: re.Pattern[str]) -> str | None:
    if not isinstance(value, str) or len(value) > TELEMETRY_MAX_FIELD_BYTES:
        return None
    if pattern.fullmatch(value) is None:
        return None
    return value.lower()


def _normalized_user_id(value: Any) -> str | None:
    if not isinstance(value, str) or len(value) > TELEMETRY_MAX_FIELD_BYTES:
        return None
    return value if _roster_member_for_telemetry(value) else None


def _roster_member_for_telemetry(user_id: str) -> bool:
    """Keep arbitrary request values out of user identity telemetry."""
    return any(row.get("user_id") == user_id for row in ROSTER.get("users", []))


def _telemetry_route(method: str, path: str) -> str:
    if path == "/health":
        return "/health"
    if path == "/ready":
        return "/ready"
    if path == "/internal/health-clock":
        return "/internal/health-clock"
    if path == "/v1/control":
        return "/v1/control"
    fixed_routes = {
        "/v2/endpoint/health": "/v2/endpoint/health",
        "/v2/hangout": "/v2/hangout",
        "/v2/inbox": "/v2/inbox",
        "/v2/inbox/last-viewed": "/v2/inbox/last-viewed",
        "/v2/session/login": "/v2/session/login",
        "/v2/session/invalidate": "/v2/session/invalidate",
        "/v2/session/refresh": "/v2/session/refresh",
        "/v2/outgoing": "/v2/outgoing",
    }
    if path in fixed_routes:
        return fixed_routes[path]
    if re.fullmatch(r"/v2/outgoing/by-client-id/[^/]+", path):
        return "/v2/outgoing/by-client-id/{client_message_id}"
    if re.fullmatch(r"/v2/outgoing/[^/]+/complete", path):
        return "/v2/outgoing/{message_id}/complete"
    if re.fullmatch(r"/v2/outgoing/[^/]+/parts/(audio|sketch)/\d+", path):
        return "/v2/outgoing/{message_id}/parts/{kind}/{sequence}"
    if re.fullmatch(r"/v2/messages/[^/]+/media/(audio|sketch)", path):
        return "/v2/messages/{message_id}/media/{kind}"
    return "/unknown"


def _telemetry_event_name(method: str, route: str) -> str:
    names = {
        "/health": "health",
        "/ready": "readiness",
        "/internal/health-clock": "health_clock",
        "/v1/control": "control",
        "/v2/endpoint/health": "endpoint_health",
        "/v2/hangout": "roster",
        "/v2/inbox": "inbox",
        "/v2/inbox/last-viewed": "inbox_last_viewed",
        "/v2/session/login": "pin_login",
        "/v2/session/invalidate": "session_invalidate",
        "/v2/session/refresh": "session_refresh",
        "/v2/outgoing": "send_create",
        "/v2/outgoing/by-client-id/{client_message_id}": "send_lookup",
        "/v2/outgoing/{message_id}/complete": "send_complete",
        "/v2/outgoing/{message_id}/parts/{kind}/{sequence}": "send_part",
        "/v2/messages/{message_id}/media/{kind}": "media_read",
    }
    return names.get(route, "http_request")


MANIFEST = verify_fixture_tree()
MANIFEST_BYTES = MANIFEST_PATH.read_bytes()
FIXTURE_SHA256 = hashlib.sha256(MANIFEST_BYTES).hexdigest()
SERVER_REVISION = json.loads(SERVER_MANIFEST.read_text(encoding="utf-8"))["lab_revision"]
# Deployment-bound state is populated only by load_server_deployment() during
# explicit server construction/startup. Legacy JSON fixtures are not a
# production fallback.
ACCOUNTS: dict[str, Any] = {}
ROSTER: dict[str, Any] = {}
DEPLOYMENT_PATH: Path | None = None
INBOX_MAYA = json.loads((FIXTURE_DIR / "inbox-maya.json").read_text(encoding="utf-8"))
MEDIA_AUDIO = (FIXTURE_DIR / "media-audio.pcm").read_bytes()
MEDIA_SKETCH = (FIXTURE_DIR / "media-sketch.flsk2").read_bytes()
RUNTIME_DIR = DEVICE_DIR / "runtime"
_runtime_override = os.environ.get("FAMILY_LINK_RUNTIME_DIR", "").strip()
if _runtime_override:
    RUNTIME_DIR = Path(_runtime_override).expanduser().resolve()
TOMBSTONE_PATH = RUNTIME_DIR / "accepted-tombstones.json"
LAST_VIEWED_PATH = RUNTIME_DIR / "last-viewed.json"


def _pilot_user_presentation() -> tuple[None, str]:
    """Return the supported wire presentation defaults for the pilot roster."""
    # The generic deployment schema currently carries the display name but not
    # avatar/card catalog selection. Keep the wire fields present and valid;
    # future presentation selection can extend the shared schema once it has a
    # product contract.
    return None, "card-bg-01"


def load_server_deployment(path: Path | str) -> None:
    """Load one static YAML deployment and bind server auth/accounts/roster.

    This is deliberately an explicit startup operation. It is not called from
    request handling, so editing the file has no effect until the process is
    restarted (or a test explicitly invokes this function before startup).
    """
    global ACCOUNTS, ROSTER, DEPLOYMENT_PATH

    deployment_path = Path(path).expanduser().resolve()
    try:
        from deployment_config import DeploymentConfigError, load_deployment, yaml
    except (ImportError, SystemExit) as exc:
        raise DeploymentLoadError(
            "server deployment requires the repository YAML/schema dependencies"
        ) from exc
    try:
        deployment = load_deployment(deployment_path, source="server")
    except (DeploymentConfigError, OSError, ValueError, TypeError) as exc:
        raise DeploymentLoadError(f"invalid server deployment {deployment_path}: {exc}") from exc

    try:
        raw_document = yaml.safe_load(deployment_path.read_text(encoding="utf-8")) or {}
    except (OSError, yaml.YAMLError, TypeError) as exc:
        raise DeploymentLoadError(
            f"invalid server deployment {deployment_path}: {exc}"
        ) from exc
    raw_users = raw_document.get("users") if isinstance(raw_document, dict) else None
    if not isinstance(raw_users, list) or len(raw_users) != 4:
        raise DeploymentLoadError("server deployment must define exactly four users")
    for index, row in enumerate(raw_users):
        if not isinstance(row, dict) or not isinstance(row.get("name"), str) or not row["name"].strip():
            raise DeploymentLoadError(f"users[{index}].name is required as the display name")
        if not isinstance(row.get("pin"), str) or PIN_RE.fullmatch(row["pin"]) is None:
            raise DeploymentLoadError(f"users[{index}].pin must be a four-digit fixture PIN")

    if len(deployment.users) != 4:
        raise DeploymentLoadError(
            f"server deployment must define exactly four pilot users; got {len(deployment.users)}"
        )
    if len({user.id for user in deployment.users}) != 4:
        raise DeploymentLoadError("server deployment user ids must be unique")
    for user in deployment.users:
        if not user.name.strip():
            raise DeploymentLoadError(f"user {user.id!r} requires a display name")
        if PIN_RE.fullmatch(user.pin) is None:
            raise DeploymentLoadError(
                f"user {user.id!r} requires a four-digit PIN using digits 1-9"
            )

    if len(deployment.endpoints) != 1:
        raise DeploymentLoadError(
            f"server deployment must define exactly one server endpoint; got {len(deployment.endpoints)}"
        )
    endpoint = deployment.endpoints[0]
    if not endpoint.token.strip():
        raise DeploymentLoadError("server deployment endpoint token is required")
    if not endpoint.hangout_id.strip():
        raise DeploymentLoadError("server deployment endpoint hangout id is required")
    if endpoint.hangout_id != deployment.meta.id:
        raise DeploymentLoadError(
            "server deployment endpoint hangout_id must match deployment.id"
        )

    roster_users = []
    account_users = []
    for user in deployment.users:
        avatar_id, card_background_id = _pilot_user_presentation()
        roster_users.append({
            "user_id": user.id,
            "display_name": user.name,
            "avatar_id": avatar_id,
            "card_background_id": card_background_id,
        })
        account_users.append({
            "user_id": user.id,
            "display_name": user.name,
            "pin": user.pin,
            "avatar_id": avatar_id,
            "card_background_id": card_background_id,
        })

    # A content-derived revision changes only after a restarted process loads a
    # changed file; it never creates a hot-reload path.
    revision = int(hashlib.sha256(deployment_path.read_bytes()).hexdigest()[:8], 16)
    ROSTER = {
        "schema": "fl.hangout-roster/2",
        "hangout_id": endpoint.hangout_id,
        "revision": revision,
        "users": roster_users,
    }
    ACCOUNTS = {
        "desk_token": endpoint.token,
        "endpoint_id": endpoint.id,
        "hangout_id": endpoint.hangout_id,
        "users": account_users,
    }
    DEPLOYMENT_PATH = deployment_path


_RUNTIME_REEXEC_ENV = "FAMILY_LINK_DEPLOYMENT_RUNTIME_REEXEC"


def _deployment_dependencies_available() -> bool:
    """Return whether this interpreter can load the deployment dependencies."""
    try:
        import yaml  # noqa: F401
        import jsonschema  # noqa: F401
    except ImportError:
        return False
    return True


def _running_in_repository_venv(candidate: Path) -> bool:
    """Avoid retrying a repository venv that is missing its dependencies."""
    try:
        return (
            sys.prefix != sys.base_prefix
            and Path(sys.prefix).resolve() == candidate.parent.parent.resolve()
        )
    except (OSError, RuntimeError):
        return False


def _ensure_deployment_runtime() -> None:
    """Use the repository environment when host Python lacks YAML packages."""
    if _deployment_dependencies_available():
        return

    candidate = ROOT / ".venv" / "bin" / "python"
    if not candidate.is_file():
        return
    if _running_in_repository_venv(candidate):
        return
    if os.environ.get(_RUNTIME_REEXEC_ENV) == str(candidate):
        return

    child_environment = os.environ.copy()
    child_environment[_RUNTIME_REEXEC_ENV] = str(candidate)
    os.execve(
        str(candidate),
        [str(candidate), str(Path(__file__).resolve()), *sys.argv[1:]],
        child_environment,
    )


def _pin_operations_path() -> Path:
    """Resolve the path at use time so tests can isolate RUNTIME_DIR."""
    return RUNTIME_DIR / "pin-operations.json"


def _load_tombstones() -> list[dict]:
    try:
        if TOMBSTONE_PATH.is_file():
            payload = json.loads(TOMBSTONE_PATH.read_text(encoding="utf-8"))
        else:
            seed = FIXTURE_DIR / "accepted-tombstones.json"
            payload = json.loads(seed.read_text(encoding="utf-8"))
    except (OSError, TypeError, ValueError):
        return []
    records = payload.get("records", []) if isinstance(payload, dict) else []
    return records if isinstance(records, list) else []


def _tombstone_recovery_issues() -> list[str]:
    """Report durable receipt corruption instead of treating it as empty state."""
    if not TOMBSTONE_PATH.is_file():
        return []
    try:
        payload = json.loads(TOMBSTONE_PATH.read_text(encoding="utf-8"))
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        return ["accepted_tombstone_unreadable"]
    if not isinstance(payload, dict) or payload.get("schema") != "p1-accepted-tombstones/v1":
        return ["accepted_tombstone_unreadable"]
    records = payload.get("records")
    if not isinstance(records, list):
        return ["accepted_tombstone_unreadable"]
    if any(_accepted_from_row(row) is None for row in records):
        return ["accepted_tombstone_record_invalid"]
    return []


def _atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=f".{path.name}.", suffix=".tmp", delete=False,
        ) as handle:
            temporary = Path(handle.name)
            json.dump(payload, handle, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        temporary = None
        _fsync_directory(path.parent)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _fsync_directory(directory: Path) -> None:
    """Make an atomic rename durable when the local filesystem supports it."""
    descriptor = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _atomic_write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", dir=path.parent,
            prefix=f".{path.name}.", suffix=".tmp", delete=False,
        ) as handle:
            temporary = Path(handle.name)
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        temporary = None
        _fsync_directory(path.parent)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _snapshot_file(path: Path) -> tuple[bool, bytes | None]:
    """Capture the exact durable contents needed to roll back a file write."""
    try:
        return True, path.read_bytes()
    except FileNotFoundError:
        return False, None


def _restore_file(path: Path, snapshot: tuple[bool, bytes | None]) -> None:
    """Restore a file snapshot using the same atomic replacement convention."""
    existed, data = snapshot
    if existed:
        if data is None:
            raise OSError(f"missing snapshot data for {path}")
        _atomic_write_bytes(path, data)
    else:
        path.unlink(missing_ok=True)


def _save_tombstones(records: list[dict]) -> None:
    _atomic_write_json(TOMBSTONE_PATH, {
        "schema": "p1-accepted-tombstones/v1", "records": records,
    })


def _tombstones_match(records: list[dict]) -> bool:
    """Return whether the complete transaction's receipt is durably present."""
    try:
        payload = json.loads(TOMBSTONE_PATH.read_text(encoding="utf-8"))
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        return False
    return payload == {
        "schema": "p1-accepted-tombstones/v1",
        "records": records,
    }


def _load_last_viewed_state() -> tuple[dict[str, str], dict[str, dict]]:
    if not LAST_VIEWED_PATH.is_file():
        return {}, {}
    payload = json.loads(LAST_VIEWED_PATH.read_text(encoding="utf-8"))
    if payload.get("schema") != "p1-last-viewed/v1":
        raise IdentityError("unknown last-viewed state schema")
    return payload.get("values", {}), payload.get("operations", {})


def _save_last_viewed_state(values: dict[str, str], operations: dict[str, dict]) -> None:
    _atomic_write_json(LAST_VIEWED_PATH, {
        "schema": "p1-last-viewed/v1", "values": values, "operations": operations,
    })


def _load_pin_records() -> dict[str, "PinRecord"]:
    path = _pin_operations_path()
    if not path.is_file():
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema") != "p1-pin-operations/v1":
        raise IdentityError("unknown pin operation state schema")
    loaded: dict[str, PinRecord] = {}
    for row in payload.get("records", []):
        if not isinstance(row, dict):
            raise IdentityError("invalid pin operation record")
        endpoint_binding = row.get("endpoint_binding")
        operation_id = row.get("operation_id")
        fingerprint = row.get("fingerprint")
        accepted_at = row.get("accepted_at")
        if (
            not isinstance(endpoint_binding, str)
            or not isinstance(operation_id, str)
            or not UUID_RE.fullmatch(operation_id)
            or not isinstance(fingerprint, str)
            or isinstance(accepted_at, bool)
            or not isinstance(accepted_at, (int, float))
        ):
            raise IdentityError("invalid pin operation record")
        body = row.get("body")
        if body is not None and not isinstance(body, dict):
            raise IdentityError("invalid pin operation result")
        session_issued_at = row.get("session_issued_at")
        session_last_refresh_at = row.get("session_last_refresh_at")
        if any(
            value is not None
            and (isinstance(value, bool) or not isinstance(value, (int, float)))
            for value in (session_issued_at, session_last_refresh_at)
        ):
            raise IdentityError("invalid pin operation session timestamps")
        session_revoked = row.get("session_revoked", False)
        if not isinstance(session_revoked, bool):
            raise IdentityError("invalid pin operation session revocation")
        loaded[_pin_record_key(endpoint_binding, operation_id)] = PinRecord(
            operation_id=operation_id,
            endpoint_binding=endpoint_binding,
            fingerprint=fingerprint,
            body=body,
            accepted_at=float(accepted_at),
            session_issued_at=(
                None if session_issued_at is None else float(session_issued_at)
            ),
            session_last_refresh_at=(
                None if session_last_refresh_at is None else float(session_last_refresh_at)
            ),
            session_revoked=session_revoked,
        )
    return loaded


def _save_pin_records(records: dict[str, "PinRecord"]) -> None:
    rows = []
    for record in sorted(records.values(), key=lambda item: (item.endpoint_binding, item.accepted_at)):
        rows.append({
            "endpoint_binding": record.endpoint_binding,
            "operation_id": record.operation_id,
            "fingerprint": record.fingerprint,
            "body": record.body,
            "accepted_at": record.accepted_at,
            "session_issued_at": record.session_issued_at,
            "session_last_refresh_at": record.session_last_refresh_at,
            "session_revoked": record.session_revoked,
        })
    _atomic_write_json(_pin_operations_path(), {
        "schema": "p1-pin-operations/v1", "records": rows,
    })


def _pin_record_key(endpoint_binding: str, operation_id: str) -> str:
    return f"{endpoint_binding}:{operation_id}"


def _runtime_path(name: str) -> Path:
    """Return a runtime path only when it resolves inside RUNTIME_DIR."""
    root = RUNTIME_DIR.resolve()
    candidate = RUNTIME_DIR / name
    try:
        candidate.resolve(strict=False).relative_to(root)
    except (OSError, RuntimeError, ValueError) as exc:
        raise ValueError("runtime path escapes RUNTIME_DIR") from exc
    return candidate


def _validated_id(value: Any, pattern: re.Pattern[str], label: str) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise ValueError(f"invalid {label}")
    return value


@dataclass
class PinRecord:
    operation_id: str
    endpoint_binding: str
    fingerprint: str
    body: dict | None
    accepted_at: float
    session_issued_at: float | None = None
    session_last_refresh_at: float | None = None
    session_revoked: bool = False


@dataclass
class OpenUpload:
    client_message_id: str
    message_id: str
    sender_user_id: str
    create_body: dict
    create_fingerprint: str
    frozen_recipient_user_ids: list[str]
    created_at: float
    last_acknowledged_at: float
    audio_parts: dict[int, Path] = field(default_factory=dict)
    sketch_parts: dict[int, Path] = field(default_factory=dict)


@dataclass
class AcceptedSend:
    client_message_id: str
    message_id: str
    sender_user_id: str
    accepted_at: str
    recipient_user_ids: list[str]
    # Optional persisted delivery payload.  Older tombstones omit these
    # fields; they remain receipt-only records after a restart.
    delivery_item: dict | None = None
    audio_b64: str | None = None
    sketch_b64: str | None = None
    create_fingerprint: str | None = None
    complete_fingerprint: str | None = None


def _open_metadata_path(client_message_id: str) -> Path:
    client_message_id = _validated_id(client_message_id, UUID_RE, "client message id")
    return _runtime_path(f"open-upload-{client_message_id}.json")


def _open_part_path(client_message_id: str, kind: str, sequence: int) -> Path:
    client_message_id = _validated_id(client_message_id, UUID_RE, "client message id")
    if kind not in {"audio", "sketch"}:
        raise ValueError("invalid upload kind")
    if isinstance(sequence, bool) or not isinstance(sequence, int) or sequence < 0:
        raise ValueError("invalid part sequence")
    return _runtime_path(f"open-upload-{client_message_id}.{kind}.{sequence:08d}.part")


def _accepted_media_path(message_id: str, kind: str) -> Path:
    message_id = _validated_id(message_id, MESSAGE_ID_RE, "message id")
    if kind not in {"audio", "sketch"}:
        raise ValueError("invalid media kind")
    return _runtime_path(f"accepted-media-{message_id}.{kind}.bin")


def _kind_limit(kind: str) -> int:
    if kind == "audio":
        return MAX_AUDIO_BYTES
    if kind == "sketch":
        return MAX_SKETCH_BYTES
    raise ValueError("invalid media kind")


def _kind_part_limit(kind: str) -> int:
    if kind == "audio":
        return MAX_AUDIO_PARTS
    if kind == "sketch":
        return MAX_SKETCH_PARTS
    raise ValueError("invalid media kind")


def _part_bytes(parts: dict[int, Path]) -> int:
    total = 0
    try:
        for path in parts.values():
            total += path.stat().st_size
    except OSError as exc:
        raise StorageReadError(str(exc)) from exc
    return total


def _open_upload_bytes(upload: OpenUpload) -> int:
    return _part_bytes(upload.audio_parts) + _part_bytes(upload.sketch_parts)


def _total_open_upload_bytes_locked() -> int:
    return sum(_open_upload_bytes(upload) for upload in STORE.opens.values())


def _part_manifest(parts: dict[int, Path]) -> list[dict[str, int]]:
    manifest = []
    for sequence, path in sorted(parts.items()):
        try:
            byte_count = path.stat().st_size
        except OSError:
            continue
        manifest.append({"sequence": sequence, "byte_count": byte_count})
    return manifest


def _open_payload(upload: OpenUpload) -> dict:
    return {
        "schema": "p2-open-upload/v1",
        "client_message_id": upload.client_message_id,
        "message_id": upload.message_id,
        "sender_user_id": upload.sender_user_id,
        "create_body": upload.create_body,
        "create_fingerprint": upload.create_fingerprint,
        "frozen_recipient_user_ids": upload.frozen_recipient_user_ids,
        "created_at": upload.created_at,
        "last_acknowledged_at": upload.last_acknowledged_at,
        "audio_parts": _part_manifest(upload.audio_parts),
        "sketch_parts": _part_manifest(upload.sketch_parts),
    }


def _save_open_upload(upload: OpenUpload) -> None:
    _atomic_write_json(_open_metadata_path(upload.client_message_id), _open_payload(upload))


def _valid_part_manifest(row: Any) -> bool:
    return (
        isinstance(row, dict)
        and isinstance(row.get("sequence"), int)
        and not isinstance(row.get("sequence"), bool)
        and row["sequence"] >= 0
        and isinstance(row.get("byte_count"), int)
        and not isinstance(row.get("byte_count"), bool)
        and 1 <= row["byte_count"] <= PART_SIZE
    )


def _load_open_uploads() -> dict[str, OpenUpload]:
    loaded: dict[str, OpenUpload] = {}
    if not RUNTIME_DIR.is_dir():
        return loaded
    for metadata_path in sorted(RUNTIME_DIR.glob("open-upload-*.json")):
        if len(loaded) >= MAX_OPEN_UPLOADS:
            break
        try:
            payload = json.loads(metadata_path.read_text(encoding="utf-8"))
            client_id = payload["client_message_id"]
            message_id = payload["message_id"]
            sender_user_id = payload["sender_user_id"]
            create_body = payload["create_body"]
            create_fingerprint = payload["create_fingerprint"]
            recipients = payload["frozen_recipient_user_ids"]
            created_at = payload["created_at"]
            last_acknowledged_at = payload["last_acknowledged_at"]
            if (
                not isinstance(client_id, str) or not UUID_RE.fullmatch(client_id)
                or not isinstance(message_id, str) or not MESSAGE_ID_RE.fullmatch(message_id)
                or not isinstance(sender_user_id, str) or not isinstance(create_body, dict)
                or not isinstance(create_fingerprint, str) or not isinstance(recipients, list)
                or any(not isinstance(item, str) for item in recipients)
                or isinstance(created_at, bool) or not isinstance(created_at, (int, float))
                or isinstance(last_acknowledged_at, bool)
                or not isinstance(last_acknowledged_at, (int, float))
                or create_fingerprint != _canonical_json(create_body)
            ):
                continue
            if time.time() - float(last_acknowledged_at) > OPEN_UPLOAD_TTL_S:
                _remove_open_files(client_id)
                continue
            upload = OpenUpload(
                client_message_id=client_id,
                message_id=message_id,
                sender_user_id=sender_user_id,
                create_body=create_body,
                create_fingerprint=create_fingerprint,
                frozen_recipient_user_ids=list(recipients),
                created_at=float(created_at),
                last_acknowledged_at=float(last_acknowledged_at),
            )
            for kind, target in (("audio", upload.audio_parts), ("sketch", upload.sketch_parts)):
                seen: set[int] = set()
                for row in payload.get(f"{kind}_parts", []):
                    if (
                        not _valid_part_manifest(row)
                        or row["sequence"] in seen
                        or row["sequence"] >= _kind_part_limit(kind)
                    ):
                        continue
                    sequence = row["sequence"]
                    part_path = _open_part_path(client_id, kind, sequence)
                    try:
                        if part_path.stat().st_size != row["byte_count"]:
                            continue
                    except OSError:
                        continue
                    seen.add(sequence)
                    target[sequence] = part_path
            if (
                _part_bytes(upload.audio_parts) > _kind_limit("audio")
                or _part_bytes(upload.sketch_parts) > _kind_limit("sketch")
                or _open_upload_bytes(upload) > MAX_TOTAL_MEDIA_BYTES
                or _total_loaded_open_bytes(loaded) + _open_upload_bytes(upload)
                > MAX_TOTAL_OPEN_UPLOAD_BYTES
            ):
                continue
            loaded[client_id] = upload
        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
            # A torn or unknown open-upload record is not an accepted send.
            # Leave it untouched for inspection; it cannot become source of truth.
            continue
    return loaded


def _total_loaded_open_bytes(uploads: dict[str, OpenUpload]) -> int:
    total = 0
    for upload in uploads.values():
        try:
            total += _open_upload_bytes(upload)
        except StorageReadError:
            continue
    return total


def _remove_open_files(client_message_id: str) -> None:
    metadata_path = _open_metadata_path(client_message_id)
    try:
        metadata_path.unlink(missing_ok=True)
    except OSError:
        pass
    for path in RUNTIME_DIR.glob(f"open-upload-{client_message_id}.*.part"):
        try:
            path.unlink(missing_ok=True)
        except OSError:
            pass


def _expire_open_locked(client_id: str, upload: OpenUpload | None = None) -> bool:
    if upload is None:
        upload = STORE.opens.get(client_id)
    if upload is None:
        return False
    if time.time() - upload.last_acknowledged_at <= OPEN_UPLOAD_TTL_S:
        return False
    STORE.opens.pop(client_id, None)
    _remove_open_files(client_id)
    return True


def _accepted_from_row(row: Any) -> AcceptedSend | None:
    """Load current rows and tolerate the P1 base64 fields during migration."""
    if not isinstance(row, dict):
        return None
    try:
        client_message_id = _validated_id(
            row.get("client_message_id"), UUID_RE, "client message id",
        )
        message_id = _validated_id(row.get("message_id"), MESSAGE_ID_RE, "message id")
        sender_user_id = row.get("sender_user_id")
        accepted_at = row.get("accepted_at")
        recipients = row.get("recipient_user_ids", [])
        delivery_item = row.get("delivery_item")
        audio_b64 = row.get("audio_b64")
        sketch_b64 = row.get("sketch_b64")
        create_fingerprint = row.get("create_fingerprint")
        complete_fingerprint = row.get("complete_fingerprint")
        delivery_media = delivery_item.get("media") if isinstance(delivery_item, dict) else None
        if (
            not isinstance(sender_user_id, str)
            or not isinstance(accepted_at, str)
            or not isinstance(recipients, list)
            or any(not isinstance(item, str) for item in recipients)
            or (delivery_item is not None and not isinstance(delivery_item, dict))
            or (delivery_item is not None and delivery_item.get("message_id") != message_id)
            or (delivery_item is not None and delivery_media is not None
                and not isinstance(delivery_media, dict))
            or (audio_b64 is not None and not isinstance(audio_b64, str))
            or (sketch_b64 is not None and not isinstance(sketch_b64, str))
            or (create_fingerprint is not None and not isinstance(create_fingerprint, str))
            or (complete_fingerprint is not None and not isinstance(complete_fingerprint, str))
        ):
            return None
        return AcceptedSend(
            client_message_id=client_message_id,
            message_id=message_id,
            sender_user_id=sender_user_id,
            accepted_at=accepted_at,
            recipient_user_ids=list(recipients),
            delivery_item=delivery_item,
            audio_b64=audio_b64,
            sketch_b64=sketch_b64,
            create_fingerprint=create_fingerprint,
            complete_fingerprint=complete_fingerprint,
        )
    except (TypeError, ValueError):
        return None


def _accepted_row_payload(accepted: AcceptedSend) -> dict:
    """Persist receipt and delivery metadata, never accepted media bytes."""
    return {
        "client_message_id": accepted.client_message_id,
        "message_id": accepted.message_id,
        "sender_user_id": accepted.sender_user_id,
        "accepted_at": accepted.accepted_at,
        "recipient_user_ids": accepted.recipient_user_ids,
        "delivery_item": accepted.delivery_item,
        "create_fingerprint": accepted.create_fingerprint,
        "complete_fingerprint": accepted.complete_fingerprint,
    }


class Store:
    def __init__(self, tombstones: list[dict] | None = None) -> None:
        self.lock = threading.Lock()
        self.recovery_issues: list[str] = []
        self.last_successful_beat_at: float | None = None
        self.drop_next_health_ack = 0
        self.oversized_inbox_once = 0
        self.pin_records: dict[str, PinRecord] = _load_pin_records()
        self.invalidated_operations: set[tuple[str, str]] = set()
        self.sessions: dict[str, dict] = {}
        self.opens: dict[str, OpenUpload] = _load_open_uploads()
        self.accepted: dict[str, AcceptedSend] = {}
        # Runtime delivery state.  The fixture remains the seed inbox for Maya;
        # accepted sends are published into these per-user snapshots and media
        # stores instead of disappearing at complete time.
        self.inboxes: dict[str, dict] = {}
        # Durable files are the source of truth. This index contains paths only
        # and is populated lazily from accepted metadata at startup.
        self.media: dict[str, dict[str, Path]] = {}
        self.last_viewed, self.last_viewed_ops = _load_last_viewed_state()
        if tombstones:
            for row in tombstones:
                accepted = _accepted_from_row(row)
                if accepted is not None:
                    self.accepted[accepted.client_message_id] = accepted
        self._restore_persisted_pin_sessions_locked()

    def _restore_persisted_pin_sessions_locked(self) -> None:
        """Restore only recently accepted sessions needed for login replay."""
        # A replayed authenticated login must return a usable credential even
        # when the request crossed a process restart. The credential remains
        # subject to the normal three-minute lifetime and endpoint binding.
        for record in self.pin_records.values():
            if time.time() - record.accepted_at > PIN_RETENTION_S:
                continue
            if not record.body or record.body.get("outcome") != "authenticated":
                continue
            session = record.body.get("session")
            user_id = record.body.get("user_id")
            operation_id = record.body.get("operation_id")
            if (
                isinstance(session, str)
                and isinstance(user_id, str)
                and isinstance(operation_id, str)
            ):
                issued_at = record.session_issued_at
                if issued_at is None:
                    issued_at = record.accepted_at
                last_refresh_at = record.session_last_refresh_at
                if last_refresh_at is None:
                    last_refresh_at = issued_at
                self.sessions[session] = {
                    "user_id": user_id,
                    "operation_id": operation_id,
                    "endpoint_binding": record.endpoint_binding,
                    "issued_at": issued_at,
                    "last_refresh_at": last_refresh_at,
                    "revoked": record.session_revoked,
                }

    def reset_runtime(self) -> None:
        with self.lock:
            self.recovery_issues = []
            self.last_successful_beat_at = None
            self.drop_next_health_ack = 0
            self.oversized_inbox_once = 0
            self.pin_records.clear()
            self.invalidated_operations.clear()
            self.sessions.clear()
            self.opens = _load_open_uploads()
            self.pin_records = _load_pin_records()
            self._restore_persisted_pin_sessions_locked()
            self.inboxes.clear()
            self.media.clear()
            self.last_viewed, self.last_viewed_ops = _load_last_viewed_state()
            self.accepted.clear()
            for row in _load_tombstones():
                accepted = _accepted_from_row(row)
                if accepted is not None:
                    self.accepted[accepted.client_message_id] = accepted
            _restore_persisted_deliveries_locked()
            # Accepted receipts, delivered media, and last-viewed state survive.

    def observe_health_clock(self) -> dict:
        with self.lock:
            return {
                "last_successful_beat_at": self.last_successful_beat_at,
                "fixture": FIXTURE_NAME,
            }

    def readiness(self) -> dict:
        with self.lock:
            issues = list(self.recovery_issues)
        return {
            "ready": not issues,
            "issues": issues,
            "fixture": FIXTURE_NAME,
        }


STORE = Store(_load_tombstones())


def _empty_inbox() -> dict:
    return {
        "schema": "fl.inbox-page/2",
        "inbox_revision": 0,
        "total_count": 0,
        "last_viewed_message_id": None,
        "items": [],
        "next_cursor": None,
        "has_more": False,
    }


def _inbox_for_user_locked(user_id: str) -> dict:
    snapshot = STORE.inboxes.get(user_id)
    if snapshot is None:
        # Copy the fixture so later publication never mutates the module-level
        # seed shared by tests and other requests.
        snapshot = json.loads(json.dumps(INBOX_MAYA if user_id == "user-maya" else _empty_inbox()))
        STORE.inboxes[user_id] = snapshot
    _project_inbox_presentation_locked(snapshot)
    if user_id in STORE.last_viewed:
        selected = STORE.last_viewed[user_id]
        snapshot["last_viewed_message_id"] = selected if any(
            item.get("message_id") == selected for item in snapshot["items"]
        ) else None
    return snapshot


def _roster_member(user_id: str) -> dict | None:
    return next((row for row in ROSTER.get("users", []) if row.get("user_id") == user_id), None)


def _frozen_recipients_are_current(upload: OpenUpload) -> bool:
    """Return whether every recipient frozen at open time is still in the roster."""
    return all(
        _roster_member(user_id) is not None
        for user_id in upload.frozen_recipient_user_ids
    )


def _validate_create_body(body: dict, sender_user_id: str) -> tuple[int, str] | None:
    recipients = body.get("recipient_user_ids")
    if not isinstance(recipients, list) or not recipients:
        return 400, "invalid_request"
    if len(recipients) > 7:
        return 422, "invalid_request"
    if any(not isinstance(recipient, str) for recipient in recipients):
        return 400, "invalid_request"
    if len(set(recipients)) != len(recipients) or sender_user_id in recipients:
        return 422, "recipient_unavailable"
    if any(_roster_member(recipient) is None for recipient in recipients):
        return 422, "recipient_unavailable"

    audio = body.get("audio")
    if not isinstance(audio, dict):
        return 400, "invalid_request"
    if audio.get("codec") != "pcm_s16le" or audio.get("sample_rate_hz") != 16000 or audio.get("channels") != 1:
        return 400, "invalid_request"
    duration_ms = audio.get("duration_ms")
    if isinstance(duration_ms, bool) or not isinstance(duration_ms, int) or not 1 <= duration_ms <= 180000:
        return 413, "media_too_large" if isinstance(duration_ms, int) and duration_ms > 180000 else "invalid_request"

    if "sketch" in body:
        sketch = body.get("sketch")
        if not isinstance(sketch, dict) or sketch.get("present") is not True or sketch.get("format_id") != "flsk2":
            return 400, "invalid_request"
    return None


def _inspect_media_parts(parts: dict[int, Path], expected_parts: Any,
                         kind: str, expected_byte_count: int | None = None) -> dict | None:
    if (
        isinstance(expected_parts, bool)
        or not isinstance(expected_parts, int)
        or expected_parts <= 0
        or expected_parts > _kind_part_limit(kind)
        or len(parts) != expected_parts
        or sorted(parts) != list(range(expected_parts))
    ):
        return None
    digest = hashlib.sha256()
    byte_count = 0
    previous_t = -1
    pending = b""
    for sequence in range(expected_parts):
        part_path = parts.get(sequence)
        if part_path is None:
            return None
        try:
            part_size = part_path.stat().st_size
            if not 1 <= part_size <= PART_SIZE:
                return None
            if sequence < expected_parts - 1 and part_size != PART_SIZE:
                return None
            with part_path.open("rb") as handle:
                while True:
                    chunk = handle.read(MEDIA_IO_CHUNK_SIZE)
                    if not chunk:
                        break
                    byte_count += len(chunk)
                    if byte_count > _kind_limit(kind):
                        return None
                    digest.update(chunk)
                    if kind == "sketch":
                        pending += chunk
                        complete_bytes = len(pending) - (len(pending) % 8)
                        for offset in range(0, complete_bytes, 8):
                            x, y, _color, timestamp = struct.unpack_from(
                                "<BBHI", pending, offset
                            )
                            if x > 119 or y > 119 or timestamp < previous_t:
                                return None
                            previous_t = timestamp
                        pending = pending[complete_bytes:]
        except OSError as exc:
            raise StorageReadError(str(exc)) from exc
    if kind == "audio" and byte_count % 2 != 0:
        return None
    if kind == "sketch" and pending:
        return None
    if expected_byte_count is not None and byte_count != expected_byte_count:
        return None
    return {"byte_count": byte_count, "sha256": digest.hexdigest()}


def _atomic_copy_parts(parts: dict[int, Path], destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", dir=destination.parent,
            prefix=f".{destination.name}.", suffix=".tmp", delete=False,
        ) as handle:
            temporary = Path(handle.name)
            for sequence in sorted(parts):
                with parts[sequence].open("rb") as source:
                    while True:
                        chunk = source.read(MEDIA_IO_CHUNK_SIZE)
                        if not chunk:
                            break
                        handle.write(chunk)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, destination)
        temporary = None
        _fsync_directory(destination.parent)
    except OSError:
        raise
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _sender_presentation(sender: dict) -> tuple[str | None, str]:
    avatar_id = sender.get("avatar_id")
    if not isinstance(avatar_id, str) or not avatar_id:
        avatar_id = None
    background_id = sender.get("card_background_id")
    if background_id not in KNOWN_CARD_BACKGROUNDS:
        background_id = "card-bg-01"
    return avatar_id, background_id


def _build_accepted_send_locked(upload: OpenUpload, complete_body: dict,
                                media: dict, accepted_at: str) -> AcceptedSend | None:
    """Build a complete delivery record without changing visible state."""
    recipients = upload.create_body.get("recipient_user_ids")
    if not isinstance(recipients, list) or not recipients or len(recipients) > 7:
        return None
    if len(set(recipients)) != len(recipients) or upload.sender_user_id in recipients:
        return None
    if any(_roster_member(str(user_id)) is None for user_id in recipients):
        return None

    sketch_present = bool(upload.create_body.get("sketch", {}).get("present"))
    audio_summary = media.get("audio")
    sketch_summary = media.get("sketch")
    if not isinstance(audio_summary, dict) or (sketch_present and not isinstance(sketch_summary, dict)):
        return None
    accepted = AcceptedSend(
        client_message_id=upload.client_message_id,
        message_id=upload.message_id,
        sender_user_id=upload.sender_user_id,
        accepted_at=accepted_at,
        recipient_user_ids=[str(user_id) for user_id in recipients],
        create_fingerprint=_canonical_json(upload.create_body),
        complete_fingerprint=_canonical_json(complete_body),
    )
    sender = _roster_member(upload.sender_user_id) or {}
    sender_avatar_id, sender_card_background_id = _sender_presentation(sender)
    item = {
        "message_id": upload.message_id,
        "sender_user_id": upload.sender_user_id,
        "sender_name": sender.get("display_name", upload.sender_user_id),
        "sender_avatar_id": sender_avatar_id,
        "sender_card_background_id": sender_card_background_id,
        "received_at": accepted_at,
        "kind": "audio_sketch" if sketch_present else "audio",
        "duration_ms": int((complete_body.get("audio") or {}).get("duration_ms", 0)),
        "media": {
            "audio": {
                "format_id": "pcm_s16le",
                "byte_count": audio_summary["byte_count"],
                "sha256": audio_summary["sha256"],
            }
        },
    }
    if sketch_present:
        item["media"]["sketch"] = {
            "format_id": "flsk2",
            "byte_count": sketch_summary["byte_count"],
            "sha256": sketch_summary["sha256"],
        }

    accepted.delivery_item = item
    return accepted


def _project_inbox_presentation_locked(snapshot: dict) -> None:
    for item in snapshot.get("items", []):
        if not isinstance(item, dict):
            continue
        sender = _roster_member(str(item.get("sender_user_id", "")))
        if sender is not None:
            avatar_id, background_id = _sender_presentation(sender)
        else:
            avatar_id = item.get("sender_avatar_id")
            if not isinstance(avatar_id, str) or not avatar_id:
                avatar_id = None
            background_id = item.get("sender_card_background_id")
            if background_id not in KNOWN_CARD_BACKGROUNDS:
                background_id = "card-bg-01"
        item["sender_avatar_id"] = avatar_id
        item["sender_card_background_id"] = background_id


def _inbox_snapshot_bytes(snapshot: dict) -> bytes:
    return json.dumps(snapshot, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def _accepted_inboxes_fit_locked(accepted: AcceptedSend) -> bool:
    if not accepted.delivery_item:
        return False
    for recipient in accepted.recipient_user_ids:
        snapshot = STORE.inboxes.get(str(recipient))
        if snapshot is None:
            snapshot = json.loads(json.dumps(
                INBOX_MAYA if str(recipient) == "user-maya" else _empty_inbox()
            ))
        else:
            _project_inbox_presentation_locked(snapshot)
        if any(item.get("message_id") == accepted.message_id for item in snapshot["items"]):
            continue
        candidate = json.loads(json.dumps(snapshot))
        candidate["items"].append(json.loads(json.dumps(accepted.delivery_item)))
        candidate["total_count"] = len(candidate["items"])
        candidate["inbox_revision"] = int(candidate.get("inbox_revision", 0)) + 1
        if len(_inbox_snapshot_bytes(candidate)) > INBOX_MAX_BYTES:
            return False
    return True


def _apply_accepted_delivery_locked(accepted: AcceptedSend) -> None:
    if not accepted.delivery_item:
        return
    audio_path = _accepted_media_path(accepted.message_id, "audio")
    sketch_path = _accepted_media_path(accepted.message_id, "sketch")
    # P1 rows may still contain base64 media. Migrate those rows once, but all
    # P2 rows use files and never retain media bytes in the accepted index.
    if not audio_path.is_file() and accepted.audio_b64:
        try:
            _atomic_write_bytes(audio_path, base64.b64decode(accepted.audio_b64))
            if accepted.sketch_b64:
                _atomic_write_bytes(sketch_path, base64.b64decode(accepted.sketch_b64))
        except (OSError, ValueError):
            return
    if not audio_path.is_file():
        return
    for recipient in accepted.recipient_user_ids:
        snapshot = _inbox_for_user_locked(str(recipient))
        _project_inbox_presentation_locked(snapshot)
        if any(item.get("message_id") == accepted.message_id for item in snapshot["items"]):
            continue
        snapshot["items"].append(json.loads(json.dumps(accepted.delivery_item)))
        snapshot["total_count"] = len(snapshot["items"])
        snapshot["inbox_revision"] = int(snapshot.get("inbox_revision", 0)) + 1
        selected = STORE.last_viewed.get(str(recipient))
        if selected == accepted.message_id:
            snapshot["last_viewed_message_id"] = selected
    STORE.media[accepted.message_id] = {"audio": audio_path}
    if accepted.delivery_item.get("media", {}).get("sketch") and sketch_path.is_file():
        STORE.media[accepted.message_id]["sketch"] = sketch_path


def _restore_persisted_deliveries_locked() -> None:
    STORE.recovery_issues = _tombstone_recovery_issues()
    for accepted in STORE.accepted.values():
        _apply_accepted_delivery_locked(accepted)
        if accepted.delivery_item:
            audio_path = _accepted_media_path(accepted.message_id, "audio")
            if not audio_path.is_file() and "accepted_media_missing" not in STORE.recovery_issues:
                # The restore helper may have migrated legacy base64 media. Check
                # the resulting durable file so failed migration is visible too.
                STORE.recovery_issues.append("accepted_media_missing")


def _restore_persisted_deliveries() -> None:
    with STORE.lock:
        _restore_persisted_deliveries_locked()


_restore_persisted_deliveries()


def reload_persisted_tombstones() -> None:
    with STORE.lock:
        for row in _load_tombstones():
            accepted = _accepted_from_row(row)
            if accepted is not None:
                STORE.accepted[accepted.client_message_id] = accepted
        STORE.opens = _load_open_uploads()
    _restore_persisted_deliveries()


class LabServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, handler, run_id: str, log: BoundedRedactingLog | None,
                 tls_enabled: bool = False):
        super().__init__(address, handler)
        self.run_id = run_id
        self.log = log
        self.tls_enabled = tls_enabled


class Handler(BaseHTTPRequestHandler):
    server: LabServer
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        # Bound incomplete request bodies and stalled response reads so a client
        # cannot keep an owned handler alive indefinitely during restart.
        self.connection.settimeout(REQUEST_TIMEOUT_S)
        self._telemetry_started = time.monotonic()
        self._telemetry_emitted = False
        self._telemetry_status: int | None = None
        self._telemetry_operation_id: str | None = None
        self._telemetry_client_message_id: str | None = None
        self._telemetry_user_id: str | None = None
        self._telemetry_endpoint_id: str | None = None
        self._telemetry_reason: str | None = None
        self._telemetry_error_code: str | None = None
        self._telemetry_retryable: bool | None = None
        self._telemetry_outcome: str | None = None
        self._telemetry_fields: dict[str, Any] = {}

    def log_message(self, fmt, *args):
        """Suppress unstructured BaseHTTPRequestHandler access logs.

        The request-scoped event emitted by finish() carries the same useful
        status and route information without copying request lines, headers,
        or arbitrary paths into the bounded log.
        """
        return

    def send_response(self, code: int, message: str | None = None):
        self._telemetry_status = int(code)
        super().send_response(code, message)

    def _set_telemetry(self, **fields: Any) -> None:
        for key, value in fields.items():
            if value is not None:
                self._telemetry_fields[key] = value

    def _capture_safe_identifiers(self, payload: dict) -> None:
        operation_id = _normalized_identifier(payload.get("operation_id"), UUID_RE)
        if operation_id is not None:
            self._telemetry_operation_id = operation_id
        client_message_id = _normalized_identifier(payload.get("client_message_id"), UUID_RE)
        if client_message_id is not None:
            self._telemetry_client_message_id = client_message_id
        user_id = _normalized_user_id(payload.get("user_id"))
        if user_id is not None:
            self._telemetry_user_id = user_id

    def _write_telemetry_line(self, payload: dict[str, Any]) -> None:
        """Write one already-structured event through the existing bounded sink."""
        line = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
        line = _redact_text(line)
        encoded = line.encode("utf-8", errors="replace")
        try:
            sys.stdout.buffer.write(encoded + b"\n")
            sys.stdout.buffer.flush()
            if self.server.log is not None:
                self.server.log.feed("stdout", encoded + b"\n")
        except OSError:
            # Diagnostics must never change the HTTP result for a client.
            return

    def _telemetry_category(self, status: int, reason: str | None,
                            retryable: bool | None) -> str:
        if status == 0:
            return "retryable_failure"
        if status < 400:
            return "success"
        if reason in {"storage_unavailable", "storage_failure"}:
            return "storage"
        if reason in {"resource_exhausted", "inbox_too_large"}:
            return "resource"
        if retryable:
            return "retryable_failure"
        return "refusal"

    def _emit_telemetry(self) -> None:
        if self._telemetry_emitted:
            return
        self._telemetry_emitted = True
        try:
            path = urlsplit(self.path).path
            route = _telemetry_route(self.command, path)
            status = self._telemetry_status or 0
            reason = self._telemetry_reason
            if reason is None:
                reason = "ok" if 200 <= status < 400 else "connection_closed"
            elapsed_ms = round(max(0.0, time.monotonic() - self._telemetry_started) * 1000, 3)
            event: dict[str, Any] = {
                "schema": TELEMETRY_SCHEMA,
                "event": _telemetry_event_name(self.command, route),
                "method": self.command,
                "route": route,
                "run_id": self.server.run_id,
                "fixture": FIXTURE_NAME,
                "fixture_version": MANIFEST["fixture_version"],
                "status": status,
                "outcome": _normalized_reason(self._telemetry_outcome)
                or self._telemetry_category(status, reason, self._telemetry_retryable),
                "status_class": self._telemetry_category(status, reason, self._telemetry_retryable),
                "reason": _normalized_reason(reason) or "unknown",
                "elapsed_ms": elapsed_ms,
                "attempt": 1,
            }
            if self._telemetry_operation_id is not None:
                event["operation_id"] = self._telemetry_operation_id
            if self._telemetry_client_message_id is not None:
                event["client_message_id"] = self._telemetry_client_message_id
            if self._telemetry_user_id is not None:
                event["user_id"] = self._telemetry_user_id
            if self._telemetry_endpoint_id is not None:
                event["endpoint_id"] = self._telemetry_endpoint_id
            if self._telemetry_error_code is not None:
                event["error_code"] = self._telemetry_error_code
            event.update(self._telemetry_fields)
            self._write_telemetry_line(event)
        except Exception:
            # A malformed diagnostic field must not alter HTTP semantics.
            return

    def finish(self):
        try:
            self._emit_telemetry()
        finally:
            super().finish()

    def _send_raw(self, status: int, body: bytes, content_type: str = "application/json",
                  extra_headers: dict[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Fixture-SHA256", FIXTURE_SHA256)
        self.send_header("X-Run-ID", self.server.run_id)
        if extra_headers:
            for key, value in extra_headers.items():
                self.send_header(key, value)
        if self.server.tls_enabled:
            self.send_header("Strict-Transport-Security", "max-age=31536000")
        self.send_header("Connection", "close")
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _json(self, status: int, payload: dict | list | str) -> None:
        if isinstance(payload, str):
            body = payload.encode("utf-8")
        else:
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self._send_raw(status, body)

    def _empty_json(self, status: int = 200) -> None:
        self._json(status, {})

    def _parse_byte_range(self, total: int) -> tuple[int, int] | None:
        header = self.headers.get("Range", "")
        if not header.startswith("bytes="):
            return None
        spec = header[6:].split(",", 1)[0].strip()
        if "-" not in spec:
            return None
        start_s, end_s = spec.split("-", 1)
        if start_s == "":
            try:
                suffix = int(end_s)
            except ValueError:
                return None
            if suffix <= 0:
                return None
            start = max(0, total - suffix)
            end = total - 1
        else:
            try:
                start = int(start_s)
            except ValueError:
                return None
            if end_s == "":
                end = total - 1
            else:
                try:
                    end = int(end_s)
                except ValueError:
                    return None
        if start < 0 or end < start or start >= total:
            return None
        end = min(end, total - 1)
        return start, end

    def _serve_ranged_bytes(self, body: bytes, content_type: str, log_kind: str,
                            message_id: str, kind: str) -> None:
        self._set_telemetry(media_kind=kind, media_byte_count=len(body))
        span = self._parse_byte_range(len(body))
        extra = {"Accept-Ranges": "bytes"}
        if span is None:
            self._send_raw(200, body, content_type, extra_headers=extra)
            return
        start, end = span
        self._set_telemetry(range_start=start, range_end=end, range_requested=True)
        slice_body = body[start : end + 1]
        extra["Content-Range"] = f"bytes {start}-{end}/{len(body)}"
        self._send_raw(206, slice_body, content_type, extra_headers=extra)

    def _serve_ranged_file(self, path: Path, content_type: str,
                           message_id: str, kind: str) -> None:
        self._set_telemetry(media_kind=kind)
        try:
            total = path.stat().st_size
            if total < 0 or total > _kind_limit(kind):
                raise OSError("media file exceeds deployment limit")
            handle = path.open("rb")
        except OSError:
            self._error(503, "storage_unavailable", True)
            return
        try:
            span = self._parse_byte_range(total)
            if span is None:
                start, end, status = 0, total - 1, 200
            else:
                start, end = span
                status = 206
            if total == 0:
                start, end = 0, -1
            content_length = max(0, end - start + 1)
            self._set_telemetry(
                media_byte_count=total,
                range_start=start,
                range_end=end,
                range_requested=span is not None,
            )
            # Read the complete bounded response before committing success
            # headers. A durable read failure must remain a retryable 503,
            # rather than becoming a truncated 200/206 response.
            handle.seek(start)
            response_body = bytearray()
            remaining = content_length
            while remaining:
                chunk = handle.read(min(MEDIA_IO_CHUNK_SIZE, remaining))
                if not chunk:
                    raise OSError("media file ended before declared size")
                response_body.extend(chunk)
                remaining -= len(chunk)
        except (OSError, ValueError):
            self._error(503, "storage_unavailable", True)
            return

        try:
            extra = {"Accept-Ranges": "bytes"}
            if status == 206:
                extra["Content-Range"] = f"bytes {start}-{end}/{total}"
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(content_length))
            self.send_header("X-Fixture-SHA256", FIXTURE_SHA256)
            self.send_header("X-Run-ID", self.server.run_id)
            for key, value in extra.items():
                self.send_header(key, value)
            if self.server.tls_enabled:
                self.send_header("Strict-Transport-Security", "max-age=31536000")
            self.send_header("Connection", "close")
            self.end_headers()
            if response_body:
                self.wfile.write(response_body)
        except OSError:
            # The body was fully read before success headers were committed.
            # A write failure is a disconnected client, not a storage result.
            pass
        finally:
            handle.close()

    def _error(self, status: int, code: str, retryable: bool, operation_id: str = "") -> None:
        normalized_code = _normalized_reason(code) or "unknown"
        self._telemetry_reason = normalized_code
        self._telemetry_error_code = normalized_code
        self._telemetry_retryable = bool(retryable)
        self._telemetry_outcome = normalized_code
        if operation_id:
            self._telemetry_operation_id = _normalized_identifier(operation_id, UUID_RE)
        payload: dict[str, Any] = {
            "schema": "fl.error/2",
            "code": code,
            "message": code,
            "retryable": retryable,
        }
        if operation_id:
            payload["operation_id"] = operation_id
        self._json(status, payload)

    def _read_json(self, max_len: int = 65536) -> dict | None:
        try:
            length = int(self.headers.get("Content-Length", "0") or "0")
        except ValueError:
            return None
        if length < 0 or length > max_len:
            return None
        try:
            raw = self.rfile.read(length) if length else b""
        except (OSError, ValueError):
            return None
        if not raw and length:
            return None
        try:
            value = json.loads(raw.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError):
            return None
        if isinstance(value, dict):
            self._capture_safe_identifiers(value)
            return value
        return None

    def _desk_token(self) -> str | None:
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            return None
        token = header[7:].strip()
        if token != ACCOUNTS["desk_token"]:
            return None
        self._telemetry_endpoint_id = self._endpoint_binding(token)[:16]
        return token

    @staticmethod
    def _endpoint_binding(token: str) -> str:
        """Stable non-secret identity for the authenticated desk credential."""
        return hashlib.sha256(token.encode("utf-8")).hexdigest()

    def _session_user(self) -> tuple[str | None, str]:
        header = self.headers.get("Authorization", "")
        if not header.startswith("Bearer "):
            return None, "missing"
        token = header[7:].strip()
        with STORE.lock:
            record = STORE.sessions.get(token)
        if record is None:
            return None, "missing"
        if record.get("endpoint_binding") != self._endpoint_binding(ACCOUNTS["desk_token"]):
            return None, "missing"
        self._telemetry_endpoint_id = self._endpoint_binding(ACCOUNTS["desk_token"])[:16]
        known_user_id = _normalized_user_id(record.get("user_id"))
        if known_user_id is not None:
            self._telemetry_user_id = known_user_id
        if record.get("revoked"):
            return None, "revoked"
        last_refresh_at = float(record.get("last_refresh_at", record.get("issued_at", 0.0)))
        if time.time() - last_refresh_at > SESSION_TTL_S:
            return None, "expired"
        user_id = known_user_id
        if user_id is None:
            return None, "missing"
        return user_id, "ok"

    def _session_error(self, state: str) -> None:
        if state == "expired":
            self._error(403, "session_expired", False)
        else:
            self._error(401, "unauthenticated", False)

    def _issue_session(self, user_id: str, operation_id: str) -> str:
        token = _b64url_token(secrets.token_bytes(32))
        now = time.time()
        endpoint_binding = self._endpoint_binding(ACCOUNTS["desk_token"])
        with STORE.lock:
            self._store_session_locked(token, user_id, operation_id, endpoint_binding, now)
        return token

    @staticmethod
    def _store_session_locked(token: str, user_id: str, operation_id: str,
                              endpoint_binding: str, now: float) -> None:
        STORE.sessions[token] = {
            "user_id": user_id,
            "operation_id": operation_id,
            "endpoint_binding": endpoint_binding,
            "issued_at": now,
            "last_refresh_at": now,
            "revoked": False,
        }

    def _pin_fingerprint(self, body: dict) -> str:
        canonical = json.dumps(
            {
                "schema": body.get("schema"),
                "operation_id": body.get("operation_id"),
                "user_id": body.get("user_id"),
                "pin": body.get("pin"),
            },
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        # The durable replay record needs an equality key, not the PIN itself.
        return hashlib.sha256(canonical).hexdigest()

    @staticmethod
    def _valid_login_body(body: dict) -> bool:
        operation_id = body.get("operation_id")
        user_id = body.get("user_id")
        pin = body.get("pin")
        return (
            isinstance(operation_id, str)
            and UUID_RE.fullmatch(operation_id) is not None
            and isinstance(user_id, str)
            and bool(user_id)
            and isinstance(pin, str)
            and PIN_RE.fullmatch(pin) is not None
        )

    def _expected_pin(self, user_id: str) -> str | None:
        for user in ACCOUNTS["users"]:
            if user["user_id"] == user_id:
                return user["pin"]
        return None

    def do_GET(self):
        parsed = urlsplit(self.path)
        path = parsed.path
        if path == "/health":
            readiness = STORE.readiness()
            self._telemetry_outcome = "live"
            self._json(200, {
                "status": "live",
                "ready": readiness["ready"],
                "run_id": self.server.run_id,
                "fixture_version": MANIFEST["fixture_version"],
                "fixture_sha256": FIXTURE_SHA256,
                "server_version": MANIFEST["server_version"],
                "contracts": MANIFEST.get("contracts", {}),
            })
            return
        if path == "/ready":
            readiness = STORE.readiness()
            status = 200 if readiness["ready"] else 503
            self._telemetry_outcome = "ready" if readiness["ready"] else "degraded"
            self._json(status, {
                "status": "ready" if readiness["ready"] else "degraded",
                "ready": readiness["ready"],
                "issues": readiness["issues"],
                "run_id": self.server.run_id,
            })
            return
        if path == "/internal/health-clock":
            self._json(200, STORE.observe_health_clock())
            return
        if path == "/v2/hangout":
            if self._desk_token() is None:
                self._error(401, "unauthenticated", False)
                return
            self._json(200, ROSTER)
            self._telemetry_outcome = "ready"
            self._set_telemetry(roster_revision=ROSTER.get("revision"))
            return
        if path == "/v2/inbox":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            with STORE.lock:
                if STORE.oversized_inbox_once > 0:
                    STORE.oversized_inbox_once -= 1
                    huge = json.dumps({"schema": "fl.inbox-page/2", "pad": "x" * (INBOX_MAX_BYTES + 1024)})
                    self._send_raw(200, huge.encode("utf-8"))
                    self._telemetry_outcome = "oversized_response"
                    self._set_telemetry(response_bytes=len(huge))
                    return
                snapshot = json.loads(json.dumps(_inbox_for_user_locked(user)))
            body = _inbox_snapshot_bytes(snapshot)
            if len(body) > INBOX_MAX_BYTES:
                self._error(503, "inbox_too_large", True)
                return
            self._send_raw(200, body)
            self._telemetry_outcome = "read"
            self._set_telemetry(
                inbox_revision=snapshot.get("inbox_revision"),
                response_bytes=len(body),
            )
            return
        media_match = re.fullmatch(r"/v2/messages/([^/]+)/media/(audio|sketch)", path)
        if media_match:
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            message_id, kind = media_match.group(1), media_match.group(2)
            self._set_telemetry(
                message_id=_normalized_identifier(message_id, MESSAGE_ID_RE),
                media_kind=kind,
            )
            if MESSAGE_ID_RE.fullmatch(message_id) is None:
                self._error(400, "invalid_request", False)
                return
            with STORE.lock:
                snapshot = _inbox_for_user_locked(user)
                visible = any(item.get("message_id") == message_id for item in snapshot["items"])
                dynamic_path = STORE.media.get(message_id, {}).get(kind)
            if not visible:
                self._error(404, "not_found", False)
                return
            if dynamic_path is not None:
                self._serve_ranged_file(dynamic_path, "application/octet-stream", message_id, kind)
                return
            elif user == "user-maya" and message_id == INBOX_MAYA["items"][0]["message_id"]:
                body = MEDIA_AUDIO if kind == "audio" else MEDIA_SKETCH
            else:
                self._error(404, "not_found", False)
                return
            self._serve_ranged_bytes(body, "application/octet-stream", "media", message_id, kind)
            return
        receipt_match = re.fullmatch(r"/v2/outgoing/by-client-id/([^/]+)", path)
        if receipt_match:
            client_id = receipt_match.group(1)
            self._telemetry_client_message_id = _normalized_identifier(client_id, UUID_RE)
            if UUID_RE.fullmatch(client_id) is None:
                self._error(400, "invalid_request", False)
                return
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            with STORE.lock:
                open_upload = STORE.opens.get(client_id)
                if open_upload is not None and _expire_open_locked(client_id, open_upload):
                    open_upload = None
                accepted = STORE.accepted.get(client_id)
            if accepted is not None and accepted.sender_user_id == user:
                self._json(200, self._receipt_body(accepted))
                self._telemetry_outcome = "accepted"
                return
            if open_upload is not None and open_upload.sender_user_id == user:
                self._json(200, self._open_body(open_upload))
                self._telemetry_outcome = "open"
                return
            self._error(404, "not_found", True)
            return
        self._error(404, "not_found", False)

    def _open_body(self, upload: OpenUpload) -> dict:
        body = {
            "schema": "fl.send-open/2",
            "client_message_id": upload.client_message_id,
            "message_id": upload.message_id,
            "state": "open",
            "audio": {"parts_acknowledged": self._acknowledged_prefix(upload.audio_parts)},
        }
        if upload.create_body.get("sketch", {}).get("present"):
            body["sketch"] = {"parts_acknowledged": self._acknowledged_prefix(upload.sketch_parts)}
        return body

    @staticmethod
    def _acknowledged_prefix(parts: dict[int, Path]) -> int:
        sequence = 0
        while sequence in parts:
            sequence += 1
        return sequence

    def _receipt_body(self, accepted: AcceptedSend) -> dict:
        return {
            "schema": "fl.send-receipt/2",
            "client_message_id": accepted.client_message_id,
            "message_id": accepted.message_id,
            "state": "accepted",
            "accepted_at": accepted.accepted_at,
            "recipients": [
                {"user_id": rid, "inbox_message_id": accepted.message_id}
                for rid in accepted.recipient_user_ids
            ],
        }

    def do_POST(self):
        parsed = urlsplit(self.path)
        path = parsed.path
        if path == "/v1/control":
            if not ip_address(self.client_address[0]).is_loopback:
                self._error(403, "forbidden", False)
                return
            if self._desk_token() is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None:
                self._error(400, "invalid_request", False)
                return
            if body.get("reset_runtime"):
                STORE.reset_runtime()
            with STORE.lock:
                if "drop_next_health_ack" in body:
                    STORE.drop_next_health_ack = int(body.get("drop_next_health_ack") or 0)
                if "oversized_inbox_once" in body:
                    STORE.oversized_inbox_once = int(body.get("oversized_inbox_once") or 0)
            self._json(200, {"result": "ok"})
            return
        if path == "/v2/endpoint/health":
            desk_token = self._desk_token()
            if desk_token is None:
                self._error(401, "unauthenticated", False)
                return
            payload = self._read_json(4096)
            if payload is None or payload.get("schema") != "fl.endpoint-health/2":
                self._error(400, "unsupported_schema", False)
                return
            if (
                set(payload) != {"schema", "reason", "boot_id", "muted"}
                or not isinstance(payload.get("reason"), str)
                or payload.get("reason") not in HEALTH_REASONS
                or not isinstance(payload.get("boot_id"), str)
                or not payload.get("boot_id")
                or not isinstance(payload.get("muted"), bool)
            ):
                self._error(400, "invalid_request", False)
                return
            self._set_telemetry(health_reason=payload.get("reason"), muted=payload.get("muted"))
            with STORE.lock:
                # Validation and endpoint authentication are complete before
                # the server freshness clock can move.
                STORE.last_successful_beat_at = time.time()
                if STORE.drop_next_health_ack > 0:
                    STORE.drop_next_health_ack -= 1
                    self.send_response(200)
                    self.end_headers()
                    self._telemetry_outcome = "ack_lost"
                    self._set_telemetry(health_ack="lost")
                    return
            ack = {
                "schema": "fl.endpoint-health-ack/2",
                "accepted": True,
                "server_time": _utc_now(),
            }
            self._json(200, ack)
            self._telemetry_outcome = "accepted"
            return
        if path == "/v2/session/login":
            desk_token = self._desk_token()
            if desk_token is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None or body.get("schema") != "fl.session-login/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = body.get("operation_id")
            operation_id_for_error = operation_id if isinstance(operation_id, str) else ""
            if not self._valid_login_body(body):
                self._error(400, "invalid_request", False, operation_id_for_error)
                return
            endpoint_binding = self._endpoint_binding(desk_token)
            operation_key = _pin_record_key(endpoint_binding, operation_id)
            fingerprint = self._pin_fingerprint(body)
            with STORE.lock:
                now = time.time()
                existing = STORE.pin_records.get(operation_key)
                if existing is not None:
                    if existing.fingerprint != fingerprint:
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                    if now - existing.accepted_at > PIN_RETENTION_S:
                        self._error(410, "operation_expired", False, operation_id)
                        return
                    if existing.body is None:
                        self._error(410, "operation_expired", False, operation_id)
                        return
                    self._json(200, existing.body)
                    self._telemetry_outcome = _normalized_reason(existing.body.get("outcome"))
                    self._set_telemetry(replay=True)
                    return
                if (endpoint_binding, operation_id) in STORE.invalidated_operations:
                    result = {
                        "schema": "fl.session-login-result/2",
                        "operation_id": operation_id,
                        "outcome": "cancelled",
                    }
                else:
                    user_id = body["user_id"]
                    pin = body["pin"]
                    expected = self._expected_pin(user_id)
                    if expected is None:
                        result = {
                            "schema": "fl.session-login-result/2",
                            "operation_id": operation_id,
                            "outcome": "unknown_user",
                        }
                    elif expected != pin:
                        result = {
                            "schema": "fl.session-login-result/2",
                            "operation_id": operation_id,
                            "outcome": "wrong_pin",
                        }
                    else:
                        session = _b64url_token(secrets.token_bytes(32))
                        self._store_session_locked(
                            session, user_id, operation_id, endpoint_binding, now,
                        )
                        result = {
                            "schema": "fl.session-login-result/2",
                            "operation_id": operation_id,
                            "outcome": "authenticated",
                            "user_id": user_id,
                            "session": session,
                        }
                next_pin_records = dict(STORE.pin_records)
                next_pin_records[operation_key] = PinRecord(
                    operation_id=operation_id,
                    endpoint_binding=endpoint_binding,
                    fingerprint=fingerprint,
                    body=result,
                    accepted_at=now,
                    session_issued_at=now if result.get("outcome") == "authenticated" else None,
                    session_last_refresh_at=now if result.get("outcome") == "authenticated" else None,
                )
                try:
                    _save_pin_records(next_pin_records)
                except OSError:
                    if result.get("session"):
                        STORE.sessions.pop(result["session"], None)
                    self._error(500, "storage_unavailable", True, operation_id)
                    return
                STORE.pin_records = next_pin_records
            self._json(200, result)
            self._telemetry_outcome = _normalized_reason(result.get("outcome"))
            self._set_telemetry(replay=False)
            return
        if path == "/v2/session/invalidate":
            desk_token = self._desk_token()
            if desk_token is None:
                self._error(401, "unauthenticated", False)
                return
            body = self._read_json(4096)
            if body is None or body.get("schema") != "fl.session-invalidate/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = body.get("operation_id")
            session_token = body.get("session")
            if not isinstance(operation_id, str) or not UUID_RE.fullmatch(operation_id):
                self._error(400, "invalid_request", False,
                            operation_id if isinstance(operation_id, str) else "")
                return
            if session_token is not None and (
                not isinstance(session_token, str) or not session_token
            ):
                self._error(400, "invalid_request", False, operation_id)
                return
            endpoint_binding = self._endpoint_binding(desk_token)
            with STORE.lock:
                if session_token:
                    session_record = STORE.sessions.get(session_token)
                    if session_record is not None and (
                        session_record.get("endpoint_binding") != endpoint_binding
                        or session_record.get("operation_id") != operation_id
                    ):
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                existing = STORE.pin_records.get(
                    _pin_record_key(endpoint_binding, operation_id)
                )
                if existing is not None and existing.body is not None:
                    next_pin_records = dict(STORE.pin_records)
                    next_pin_records[_pin_record_key(endpoint_binding, operation_id)] = replace(
                        existing, session_revoked=True,
                    )
                    try:
                        _save_pin_records(next_pin_records)
                    except OSError:
                        self._error(500, "storage_unavailable", True, operation_id)
                        return
                    STORE.pin_records = next_pin_records
                STORE.invalidated_operations.add((endpoint_binding, operation_id))
                for token, record in list(STORE.sessions.items()):
                    if (
                        record.get("endpoint_binding") == endpoint_binding
                        and record.get("operation_id") == operation_id
                    ):
                        record["revoked"] = True
            self._json(200, {
                "schema": "fl.session-invalidate-result/2",
                "operation_id": operation_id,
                "revoked": True,
            })
            self._telemetry_outcome = "revoked"
            return
        if path == "/v2/session/refresh":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            refresh_body = self._read_json(64)
            if refresh_body is None:
                self._error(400, "invalid_request", False)
                return
            if refresh_body != {}:
                self._error(400, "invalid_request", False)
                return
            token = self.headers.get("Authorization", "")[7:].strip()
            with STORE.lock:
                record = STORE.sessions.get(token)
                if record is None or record.get("revoked"):
                    self._error(401, "unauthenticated", False)
                    return
                if time.time() - float(record.get("last_refresh_at", record.get("issued_at", 0.0))) > SESSION_TTL_S:
                    self._error(403, "session_expired", False)
                    return
                now = time.time()
                operation_key = _pin_record_key(
                    record["endpoint_binding"], record["operation_id"]
                )
                existing = STORE.pin_records.get(operation_key)
                if existing is not None and existing.body is not None:
                    next_pin_records = dict(STORE.pin_records)
                    next_pin_records[operation_key] = replace(
                        existing, session_last_refresh_at=now,
                    )
                    try:
                        _save_pin_records(next_pin_records)
                    except OSError:
                        self._error(500, "storage_unavailable", True)
                        return
                    STORE.pin_records = next_pin_records
                record["last_refresh_at"] = now
            self._empty_json(200)
            self._telemetry_outcome = "refreshed"
            return
        if path == "/v2/outgoing":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            body = self._read_json()
            if not isinstance(body, dict) or body.get("schema") != "fl.message-create/2":
                self._error(400, "unsupported_schema", False)
                return
            client_id = body.get("client_message_id")
            if not isinstance(client_id, str) or UUID_RE.fullmatch(client_id) is None:
                self._error(400, "invalid_request", False)
                return
            with STORE.lock:
                accepted = STORE.accepted.get(client_id)
                if accepted is not None:
                    if accepted.sender_user_id != user:
                        self._error(403, "forbidden", False)
                        return
                    if accepted.create_fingerprint is not None and accepted.create_fingerprint != _canonical_json(body):
                        self._error(409, "identity_conflict", False)
                        return
                    self._json(200, self._receipt_body(accepted))
                    self._telemetry_outcome = "accepted"
                    return
                open_upload = STORE.opens.get(client_id)
                if open_upload is not None and _expire_open_locked(client_id, open_upload):
                    open_upload = None
                if open_upload is not None:
                    if open_upload.sender_user_id != user:
                        self._error(403, "forbidden", False)
                        return
                    if _canonical_json(open_upload.create_body) != _canonical_json(body):
                        self._error(409, "identity_conflict", False)
                        return
                    self._json(200, self._open_body(open_upload))
                    self._telemetry_outcome = "open"
                    return
                validation_error = _validate_create_body(body, user)
                if validation_error is not None:
                    self._error(validation_error[0], validation_error[1], False)
                    return
                for open_id, existing_upload in list(STORE.opens.items()):
                    _expire_open_locked(open_id, existing_upload)
                if len(STORE.opens) >= MAX_OPEN_UPLOADS:
                    self._error(429, "resource_exhausted", True)
                    return
                try:
                    if _total_open_upload_bytes_locked() > MAX_TOTAL_OPEN_UPLOAD_BYTES:
                        self._error(429, "resource_exhausted", True)
                        return
                except StorageReadError:
                    self._error(503, "storage_unavailable", True)
                    return
                message_id = f"msg-{secrets.token_hex(8)}"
                now = time.time()
                upload = OpenUpload(
                    client_message_id=client_id,
                    message_id=message_id,
                    sender_user_id=user,
                    create_body=body,
                    create_fingerprint=_canonical_json(body),
                    frozen_recipient_user_ids=[str(item) for item in body["recipient_user_ids"]],
                    created_at=now,
                    last_acknowledged_at=now,
                )
                try:
                    _save_open_upload(upload)
                except OSError:
                    self._error(500, "storage_unavailable", True)
                    return
                STORE.opens[client_id] = upload
            self._json(200, self._open_body(upload))
            self._telemetry_outcome = "open"
            self._set_telemetry(message_id=_normalized_identifier(upload.message_id, MESSAGE_ID_RE))
            return
        complete_match = re.fullmatch(r"/v2/outgoing/([^/]+)/complete", path)
        if complete_match:
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            message_id = complete_match.group(1)
            self._set_telemetry(message_id=_normalized_identifier(message_id, MESSAGE_ID_RE))
            body = self._read_json()
            if not isinstance(body, dict) or body.get("schema") != "fl.message-complete/2":
                self._error(400, "unsupported_schema", False)
                return
            if MESSAGE_ID_RE.fullmatch(message_id) is None:
                self._error(400, "invalid_request", False)
                return
            client_id = body.get("client_message_id")
            if not isinstance(client_id, str) or UUID_RE.fullmatch(client_id) is None:
                self._error(400, "invalid_request", False)
                return
            with STORE.lock:
                accepted = STORE.accepted.get(client_id)
                if accepted is not None and accepted.message_id == message_id:
                    if accepted.sender_user_id != user:
                        self._error(403, "forbidden", False)
                        return
                    if accepted.complete_fingerprint is not None and accepted.complete_fingerprint != _canonical_json(body):
                        self._error(409, "identity_conflict", False)
                        return
                    self._json(200, self._receipt_body(accepted))
                    self._telemetry_outcome = "accepted"
                    return
                upload = STORE.opens.get(client_id)
                if upload is not None and _expire_open_locked(client_id, upload):
                    upload = None
                if upload is None or upload.message_id != message_id or upload.sender_user_id != user:
                    self._error(404, "not_found", False)
                    return
                if not _frozen_recipients_are_current(upload):
                    self._error(422, "recipient_unavailable", False)
                    return
                try:
                    media = self._upload_complete(upload, body)
                except StorageReadError:
                    self._error(503, "storage_unavailable", True)
                    return
                if media is None:
                    self._error(413, "invalid_request", False)
                    return
                if not _frozen_recipients_are_current(upload):
                    self._error(422, "recipient_unavailable", False)
                    return
                built = _build_accepted_send_locked(upload, body, media, _utc_now())
                if built is None or not _accepted_inboxes_fit_locked(built):
                    self._error(413, "inbox_too_large" if built is not None else "invalid_request", False)
                    return
                accepted_row = built
                tombstone_records = [
                    _accepted_row_payload(row) for row in STORE.accepted.values()
                ]
                tombstone_records.append(_accepted_row_payload(accepted_row))
                tombstone_snapshot = _snapshot_file(TOMBSTONE_PATH)
                media_snapshots = {}
                audio_path = _accepted_media_path(message_id, "audio")
                media_snapshots[audio_path] = _snapshot_file(audio_path)
                sketch_path = None
                if media.get("sketch") is not None:
                    sketch_path = _accepted_media_path(message_id, "sketch")
                    media_snapshots[sketch_path] = _snapshot_file(sketch_path)
                try:
                    _atomic_copy_parts(upload.audio_parts, audio_path)
                    if media.get("sketch") is not None:
                        assert sketch_path is not None
                        _atomic_copy_parts(upload.sketch_parts, sketch_path)
                    _save_tombstones(tombstone_records)
                except OSError:
                    # A failure after os.replace may still leave a complete,
                    # restart-reconstructible receipt. Keep that state only when
                    # the exact new tombstone is present; otherwise restore the
                    # old receipt and every media path byte-for-byte.
                    if not _tombstones_match(tombstone_records):
                        _restore_file(TOMBSTONE_PATH, tombstone_snapshot)
                        for media_path, snapshot in media_snapshots.items():
                            _restore_file(media_path, snapshot)
                    self._error(500, "storage_unavailable", True)
                    return
                STORE.accepted[client_id] = accepted_row
                STORE.opens.pop(client_id, None)
                _remove_open_files(client_id)
                _apply_accepted_delivery_locked(accepted_row)
            self._json(200, self._receipt_body(accepted_row))
            self._telemetry_outcome = "accepted"
            return
        self._error(404, "not_found", False)

    def _upload_complete(self, upload: OpenUpload, complete_body: dict) -> dict | None:
        audio_meta = complete_body.get("audio")
        if not isinstance(audio_meta, dict):
            return None
        create_audio = upload.create_body.get("audio") or {}
        duration_ms = audio_meta.get("duration_ms")
        if isinstance(duration_ms, bool) or not isinstance(duration_ms, int):
            return None
        audio = _inspect_media_parts(
            upload.audio_parts,
            audio_meta.get("parts"),
            "audio",
            expected_byte_count=duration_ms * 32,
        )
        if audio is None or duration_ms != create_audio.get("duration_ms"):
            return None
        if not self._matches_media_metadata(audio, audio_meta):
            return None

        sketch_present = upload.create_body.get("sketch", {}).get("present") is True
        sketch_meta = complete_body.get("sketch")
        if not sketch_present:
            return None if sketch_meta is not None or upload.sketch_parts else {"audio": audio}
        if not isinstance(sketch_meta, dict):
            return None
        sketch = _inspect_media_parts(upload.sketch_parts, sketch_meta.get("parts"), "sketch")
        if sketch is None or not self._matches_media_metadata(sketch, sketch_meta):
            return None
        return {"audio": audio, "sketch": sketch}

    @staticmethod
    def _matches_media_metadata(summary: dict, metadata: dict) -> bool:
        byte_count = metadata.get("byte_count")
        digest = metadata.get("sha256")
        return (
            isinstance(byte_count, int)
            and not isinstance(byte_count, bool)
            and byte_count == summary.get("byte_count")
            and isinstance(digest, str)
            and re.fullmatch(r"[0-9a-f]{64}", digest) is not None
            and digest == summary.get("sha256")
        )

    @staticmethod
    def _valid_sketch_bytes(data: bytes) -> bool:
        previous_t = -1
        for offset in range(0, len(data), 8):
            x, y = data[offset], data[offset + 1]
            timestamp = struct.unpack_from("<I", data, offset + 4)[0]
            if x > 119 or y > 119 or timestamp < previous_t:
                return False
            previous_t = timestamp
        return True

    def do_PUT(self):
        parsed = urlsplit(self.path)
        path = parsed.path
        if path == "/v2/inbox/last-viewed":
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            body = self._read_json(4096)
            if not isinstance(body, dict) or body.get("schema") != "fl.last-viewed-update/2":
                self._error(400, "unsupported_schema", False)
                return
            operation_id = str(body.get("operation_id", ""))
            message_id = body.get("message_id")
            if (not UUID_RE.fullmatch(operation_id)
                    or not isinstance(message_id, str)
                    or MESSAGE_ID_RE.fullmatch(message_id) is None):
                self._error(400, "invalid_request", False, operation_id)
                return
            with STORE.lock:
                prior = STORE.last_viewed_ops.get(operation_id)
                if prior is not None:
                    if prior.get("user_id") != user:
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                    if prior.get("message_id") != message_id:
                        self._error(409, "identity_conflict", False, operation_id)
                        return
                    if time.time() - float(prior["accepted_at"]) > LAST_VIEWED_RETENTION_S:
                        self._error(410, "operation_expired", False, operation_id)
                        return
                    self._json(200, prior["result"])
                    return
                snapshot = _inbox_for_user_locked(user)
                if not any(item.get("message_id") == message_id for item in snapshot["items"]):
                    self._error(404, "not_found", False, operation_id)
                    return
                result = {
                    "schema": "fl.last-viewed/2",
                    "operation_id": operation_id,
                    "last_viewed_message_id": message_id,
                }
                next_values = {**STORE.last_viewed, user: message_id}
                next_operations = dict(STORE.last_viewed_ops)
                next_operations[operation_id] = {
                    "user_id": user,
                    "message_id": message_id,
                    "result": result,
                    "accepted_at": time.time(),
                }
                try:
                    _save_last_viewed_state(next_values, next_operations)
                except OSError:
                    self._error(500, "storage_unavailable", True, operation_id)
                    return
                STORE.last_viewed = next_values
                STORE.last_viewed_ops = next_operations
                snapshot["last_viewed_message_id"] = message_id
            self._json(200, result)
            return
        part_match = re.fullmatch(r"/v2/outgoing/([^/]+)/parts/(audio|sketch)/(\d+)", path)
        if part_match:
            user, state = self._session_user()
            if state != "ok" or user is None:
                self._session_error(state)
                return
            message_id, kind, sequence_s = part_match.group(1), part_match.group(2), part_match.group(3)
            if MESSAGE_ID_RE.fullmatch(message_id) is None:
                self._error(400, "invalid_request", False)
                return
            try:
                sequence = int(sequence_s)
            except ValueError:
                self._error(400, "invalid_request", False)
                return
            self._set_telemetry(
                message_id=_normalized_identifier(message_id, MESSAGE_ID_RE),
                media_kind=kind,
                sequence=sequence if 0 <= sequence <= 1_000_000 else None,
            )
            try:
                length = int(self.headers.get("Content-Length", "0") or "0")
            except ValueError:
                self._error(400, "invalid_request", False)
                return
            if length < 0 or length > PART_SIZE:
                self._error(400, "invalid_request", False)
                return
            if length == 0:
                self._error(400, "invalid_request", False)
                return
            chunk = self.rfile.read(length)
            if len(chunk) != length:
                self._error(400, "invalid_request", False)
                return
            with STORE.lock:
                upload = None
                for open_upload in STORE.opens.values():
                    if open_upload.message_id == message_id and open_upload.sender_user_id == user:
                        upload = open_upload
                        break
                if upload is None or _expire_open_locked(upload.client_message_id, upload):
                    self._error(404, "not_found", False)
                    return
                self._telemetry_client_message_id = _normalized_identifier(
                    upload.client_message_id, UUID_RE
                )
                parts = upload.audio_parts if kind == "audio" else upload.sketch_parts
                if sequence >= _kind_part_limit(kind):
                    self._error(413, "media_too_large", False)
                    return
                part_path = _open_part_path(upload.client_message_id, kind, sequence)
                is_new = sequence not in parts
                if is_new:
                    try:
                        kind_bytes = _part_bytes(parts)
                        open_bytes = _total_open_upload_bytes_locked()
                    except StorageReadError:
                        self._error(503, "storage_unavailable", True)
                        return
                    if kind_bytes + length > _kind_limit(kind):
                        self._error(413, "media_too_large", False)
                        return
                    if kind == "audio":
                        declared_duration = (upload.create_body.get("audio") or {}).get("duration_ms")
                        declared_bytes = (
                            declared_duration * 32
                            if isinstance(declared_duration, int)
                            and not isinstance(declared_duration, bool)
                            else 0
                        )
                        if kind_bytes + length > declared_bytes:
                            self._error(413, "media_too_large", False)
                            return
                    if open_bytes + length > MAX_TOTAL_OPEN_UPLOAD_BYTES:
                        self._error(429, "resource_exhausted", True)
                        return
                if not is_new:
                    try:
                        if parts[sequence].read_bytes() != chunk:
                            self._error(409, "identity_conflict", False)
                            return
                    except OSError:
                        self._error(503, "storage_unavailable", True)
                        return
                prior_acknowledged_at = upload.last_acknowledged_at
                metadata_path = _open_metadata_path(upload.client_message_id)
                metadata_snapshot = _snapshot_file(metadata_path)
                part_snapshot = _snapshot_file(part_path)
                try:
                    if is_new:
                        _atomic_write_bytes(part_path, chunk)
                        parts[sequence] = part_path
                    upload.last_acknowledged_at = time.time()
                    _save_open_upload(upload)
                except OSError:
                    if is_new:
                        parts.pop(sequence, None)
                    upload.last_acknowledged_at = prior_acknowledged_at
                    # _save_open_upload can report an error after replacing its
                    # metadata file. Restore both durable files, not just the
                    # in-memory timestamp and part index.
                    _restore_file(metadata_path, metadata_snapshot)
                    _restore_file(part_path, part_snapshot)
                    self._error(500, "storage_unavailable", True)
                    return
            self._empty_json(200)
            self._telemetry_outcome = "accepted"
            return
        self._error(404, "not_found", False)


def create_server(bind: str, port: int, run_id: str, log_path: str | None = None,
                  reload_tombstones: bool = False, tls_cert: str | None = None,
                  tls_key: str | None = None, production: bool = False,
                  deployment_path: Path | str | None = None) -> LabServer:
    # All server construction paths bind the static YAML deployment before the
    # socket is created; an explicit path is supported for private/test deployments.
    load_server_deployment(
        DEFAULT_DEPLOYMENT_PATH if deployment_path is None else deployment_path
    )
    if bool(tls_cert) != bool(tls_key):
        raise ValueError("--tls-cert and --tls-key must be supplied together")
    if production and not (tls_cert and tls_key):
        raise ValueError("production mode requires --tls-cert and --tls-key")
    if reload_tombstones:
        reload_persisted_tombstones()
    log = BoundedRedactingLog(log_path) if log_path else None
    httpd = LabServer((bind, port), Handler, run_id, log, tls_enabled=bool(tls_cert))
    if tls_cert and tls_key:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(certfile=tls_cert, keyfile=tls_key)
        httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
    return httpd


def server_identity(root: Path) -> dict:
    manifest_path = root / "server/device/fixtures/manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    return {
        "revision": SERVER_REVISION,
        "fixture_sha256": hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        "fixture_version": manifest["fixture_version"],
        "contracts": manifest.get("contracts", {}),
    }


def parse_flsk2(data: bytes) -> list[tuple[int, int, int, int]]:
    points = []
    if len(data) % 8 != 0:
        raise ValueError("flsk2 length")
    for offset in range(0, len(data), 8):
        x, y, color, t_ms = struct.unpack_from("<BBHI", data, offset)
        points.append((x, y, color, t_ms))
    return points


def replay_pixels(points: list[tuple[int, int, int, int]]) -> set[tuple[int, int]]:
    return {(x, y) for x, y, _, _ in points}


def chebyshev(a: tuple[int, int], b: tuple[int, int]) -> int:
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def main(argv: list[str] | None = None) -> int:
    _ensure_deployment_runtime()
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=int(MANIFEST.get("default_port", 8777)))
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--log", default="")
    parser.add_argument("--tls-cert", default="",
                        help="PEM server certificate; required with --tls-key")
    parser.add_argument("--tls-key", default="",
                        help="PEM private key; required with --tls-cert")
    parser.add_argument("--production", action="store_true",
                        help="require TLS before serving (for deployed production use)")
    parser.add_argument(
        "--deployment", "--roster", dest="deployment",
        default=str(DEFAULT_DEPLOYMENT_PATH),
        help="static YAML deployment/roster loaded once before serving",
    )
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", args.run_id):
        parser.error("unsafe run ID")
    try:
        httpd = create_server(
            args.bind, args.port, args.run_id, args.log or None,
            tls_cert=args.tls_cert or None, tls_key=args.tls_key or None,
            production=args.production,
            deployment_path=args.deployment,
        )
    except (DeploymentLoadError, OSError, ValueError) as exc:
        parser.error(str(exc))
    try:
        previous_sigint = signal.getsignal(signal.SIGINT)
        previous_sigterm = signal.getsignal(signal.SIGTERM)
        # GNU Make may launch the recipe with SIGINT ignored. Restore Python's
        # normal Ctrl+C handler so the child process participates in shutdown
        # instead of surviving an interrupted `make server` wrapper.
        signal.signal(signal.SIGINT, signal.default_int_handler)

        def _handle_sigterm(signum, frame):
            # Route SIGTERM through the same finally block as Ctrl+C so the
            # listening socket and log are closed deterministically.
            raise KeyboardInterrupt

        signal.signal(signal.SIGTERM, _handle_sigterm)
        httpd.serve_forever(poll_interval=0.1)
    except KeyboardInterrupt:
        # Ctrl+C is a normal operator shutdown, not a server failure.
        return 0
    finally:
        signal.signal(signal.SIGINT, previous_sigint)
        signal.signal(signal.SIGTERM, previous_sigterm)
        httpd.server_close()
        if httpd.log is not None:
            httpd.log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
