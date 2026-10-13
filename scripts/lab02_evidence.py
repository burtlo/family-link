"""Safe, append-only evidence primitives for the LAB-02 host workflow.

Callers supply facts they have actually observed. This module deliberately does
not infer a successful workflow or experiment from a parser exit code.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import threading
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

DOCUMENT = "family-link.lab-run-manifest/v1"
RUN_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
_SAFE_FIELDS = {
    "lab_id", "lab_revision", "lab_version_string", "verification_levels",
    "firmware", "server", "fixture", "hardware", "scenarios",
    "comparison_run_id", "operator", "source", "build", "capture",
    "workflow_status", "experiment_verdict", "terminal_reason",
    "artifacts",
}
_SECRET_KEY = re.compile(r"password|secret|token|credential|ssid|wifi|(?:^|_)serial(?:$|_number|_binding)|url|endpoint|auth", re.I)
_SECRET_TEXT = re.compile(r"(?i)https?://[^\s\"']+|(?:ssid|password|token|authorization)\s*[=:]\s*[^\s,;]+")
_SAFE_CMAKE = re.compile(r"^(?:LAB02_[A-Z0-9_]+|CONFIG_[A-Z0-9_]+)$")
MAX_EVENT_BYTES = 16 * 1024


def _utc(value: str | None = None) -> str:
    return value or datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _clean(value: Any, key: str = "") -> Any:
    if _SECRET_KEY.search(key):
        return "[REDACTED]"
    if isinstance(value, dict):
        return {str(k): _clean(v, str(k)) for k, v in value.items() if isinstance(k, str)}
    if isinstance(value, (list, tuple)):
        return [_clean(item) for item in value]
    if isinstance(value, str):
        return _SECRET_TEXT.sub("[REDACTED]", value)
    if value is None or isinstance(value, (bool, int, float)):
        return value
    # Unknown objects may have reprs containing credentials or deployment data.
    return "[UNSUPPORTED]"


def _merge(current: dict, incoming: dict) -> dict:
    result = dict(current)
    for key, value in incoming.items():
        if isinstance(result.get(key), dict) and isinstance(value, dict):
            result[key] = _merge(result[key], value)
        else:
            result[key] = value
    return result


def _atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".{os.getpid()}.tmp")
    try:
        with temporary.open("w", encoding="utf-8") as handle:
            json.dump(value, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


class EvidenceRun:
    """Thread-safe bounded event sink and exactly-once terminal manifest writer."""

    def __init__(self, repo_root: Path, run_id: str, context: dict,
                 *, started_at_utc: str | None = None, max_events: int = 2000):
        if not isinstance(run_id, str) or not RUN_ID.fullmatch(run_id) or run_id in {".", ".."}:
            raise ValueError("run_id must be a safe single path segment")
        if max_events < 1:
            raise ValueError("max_events must be positive")
        self.repo_root = Path(repo_root).resolve()
        self.run_id = run_id
        self.run_dir = self.repo_root / "docs/labs/evidence/lab-02/runs" / run_id
        if self.run_dir.exists():
            raise FileExistsError(f"evidence run already exists: {run_id}")
        self.run_dir.mkdir(parents=True, exist_ok=False)
        self.events_path = self.run_dir / "events.jsonl"
        self.manifest_path = self.run_dir / "run-manifest.json"
        self.context = self._normalize_context(context)
        self.started_at_utc = _utc(started_at_utc)
        self.max_events = max_events
        self.max_event_bytes = MAX_EVENT_BYTES
        self._events: list[dict] = []
        self._regular_events = 0
        self._dropped_events = 0
        self._truncated = False
        self._lock = threading.RLock()
        self._final: dict | None = None
        self._pending_final: dict | None = None
        self._terminal_written = False

    @staticmethod
    def _normalize_context(context: dict, *, defaults: bool = True) -> dict:
        if not isinstance(context, dict):
            raise ValueError("evidence context must be an object")
        out = {key: _clean(value, key) for key, value in context.items() if key in _SAFE_FIELDS}
        if not defaults:
            fw = out.get("firmware")
            if isinstance(fw, dict) and isinstance(fw.get("cmake_options"), dict):
                fw["cmake_options"] = {k: v for k, v in fw["cmake_options"].items()
                                       if _SAFE_CMAKE.fullmatch(k)}
            return out
        # Preserve the manifest shape when preflight/build stops before a fact
        # exists. Null means unknown; it is never filled from a guessed default.
        out.setdefault("lab_id", "LAB-02")
        out.setdefault("lab_revision", None)
        out.setdefault("lab_version_string", None)
        out.setdefault("verification_levels", {})
        out["verification_levels"] = out["verification_levels"] if isinstance(out["verification_levels"], dict) else {}
        out["firmware"] = out.get("firmware") if isinstance(out.get("firmware"), dict) else {}
        for key in ("build_id", "source_revision", "source_sha256", "binary_sha256",
                    "build_stamp", "project_path", "idf_version", "sdkconfig_sha256",
                    "cmake_options"):
            if isinstance(out["firmware"], dict):
                out["firmware"].setdefault(key, {} if key == "cmake_options" else None)
        out["server"] = out.get("server") if isinstance(out.get("server"), dict) else {}
        out["server"].setdefault("path", None)
        out["server"].setdefault("revision", None)
        out["server"].setdefault("source_sha256", None)
        out["fixture"] = out.get("fixture") if isinstance(out.get("fixture"), dict) else {}
        for key in ("schema", "revision", "sha256"):
            out["fixture"].setdefault(key, None)
        out["fixture"].setdefault("file", None)
        out["hardware"] = out.get("hardware") if isinstance(out.get("hardware"), dict) else {}
        for key in ("board", "kit_nickname"):
            out["hardware"].setdefault(key, None)
        out["scenarios"] = out.get("scenarios") if isinstance(out.get("scenarios"), list) else []
        out.setdefault("comparison_run_id", None)
        out.setdefault("operator", None)
        out["source"] = out.get("source") if isinstance(out.get("source"), dict) else {}
        out["source"].setdefault("revision", None)
        out["source"].setdefault("dirty", None)
        out["source"].setdefault("files", {})
        out["build"] = out.get("build") if isinstance(out.get("build"), dict) else {}
        out["build"].setdefault("status", "unknown")
        out["capture"] = out.get("capture") if isinstance(out.get("capture"), dict) else {}
        out["capture"].setdefault("status", "not_started")
        fw = out.get("firmware")
        if isinstance(fw, dict) and isinstance(fw.get("cmake_options"), dict):
            fw["cmake_options"] = {k: v for k, v in fw["cmake_options"].items()
                                   if _SAFE_CMAKE.fullmatch(k)}
        out["artifacts"] = out.get("artifacts") if isinstance(out.get("artifacts"), dict) else {}
        return out

    def update_context(self, facts: dict) -> None:
        """Merge newly observed safe facts before finalization."""
        if not isinstance(facts, dict):
            raise ValueError("evidence facts must be an object")
        with self._lock:
            if self._pending_final is not None:
                raise RuntimeError("evidence context is frozen at finalization")
            safe = self._normalize_context(facts, defaults=False)
            self.context = _merge(self.context, safe)
            fw = self.context.get("firmware")
            if isinstance(fw, dict) and isinstance(fw.get("cmake_options"), dict):
                fw["cmake_options"] = {k: v for k, v in fw["cmake_options"].items()
                                        if _SAFE_CMAKE.fullmatch(k)}

    def event_sink(self, event: dict) -> None:
        """Persist one sanitized event; safe for worker callbacks."""
        if not isinstance(event, dict):
            event = {"kind": "invalid_event"}
        row = _clean(event)
        encoded = json.dumps(row, sort_keys=True, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > self.max_event_bytes:
            row = {"kind": "event_oversized"}
            encoded = json.dumps(row, separators=(",", ":"))
        with self._lock:
            if self._pending_final is not None:
                return
            if row.get("kind") == "event_oversized" or self._regular_events >= self.max_events:
                self._dropped_events += 1
                self._add_truncation_marker()
                return
            self._regular_events += 1
            self._append_event(row, encoded)

    def _append_event(self, row: dict, encoded: str) -> None:
        self._events.append(row)
        with self.events_path.open("a", encoding="utf-8") as handle:
            handle.write(encoded + "\n")
            handle.flush()

    def _add_truncation_marker(self) -> None:
        if not self._truncated:
            self._truncated = True
            self._append_event({"kind": "events_truncated"}, '{"kind":"events_truncated"}')

    def _bounded_terminal_reason(self, reason: Any, workflow_status: str,
                                 experiment_verdict: str) -> tuple[str, bool]:
        """Fit the sanitized reason and terminal marker inside the encoded row cap."""
        cleaned = _clean(reason or "")
        if not isinstance(cleaned, str):
            cleaned = "[UNSUPPORTED]"

        def encoded_size(value: str, truncated: bool) -> int:
            row = {"kind": "run_terminal", "workflow_status": workflow_status,
                   "experiment_verdict": experiment_verdict, "reason": value}
            if truncated:
                row["reason_truncated"] = True
            return len(json.dumps(row, sort_keys=True, separators=(",", ":")).encode("utf-8"))

        if encoded_size(cleaned, False) <= self.max_event_bytes:
            return cleaned, False
        marker = "[truncated]"
        low, high = 0, len(cleaned)
        while low < high:
            mid = (low + high + 1) // 2
            if encoded_size(cleaned[:mid] + marker, True) <= self.max_event_bytes:
                low = mid
            else:
                high = mid - 1
        bounded = cleaned[:low] + marker
        # Fixed row fields plus marker fit the documented cap even for an empty prefix.
        return bounded, True

    def _validated_artifact(self, name: str, value: Any) -> str | None:
        if not isinstance(value, str) or not value or "\\" in value:
            return None
        candidate = PurePosixPath(value)
        if candidate.is_absolute() or any(part in {"", ".", ".."} for part in candidate.parts):
            return None
        expected_dir = PurePosixPath("docs/labs/evidence/lab-02/runs") / self.run_id
        if name in {"serial_log", "device_log"}:
            allowed = (PurePosixPath("logs/device/labs/lab-02") / self.run_id / "serial.txt",)
        elif name == "metrics":
            allowed = (expected_dir / "metrics.json",)
        else:
            return None
        if candidate not in allowed:
            return None
        actual = self.repo_root.joinpath(*candidate.parts)
        try:
            actual.resolve(strict=True).relative_to(self.repo_root)
        except (OSError, ValueError):
            return None
        return candidate.as_posix() if actual.is_file() else None

    def finalize(self, *, workflow_status: str, experiment_verdict: str,
                 caller_completed: bool, cleanup_succeeded: bool,
                 ended_at_utc: str | None = None, terminal_reason: str | None = None) -> dict:
        """Write one atomic manifest. PASS requires explicit completed/cleanup facts."""
        if workflow_status not in {"PASS", "FAIL", "INCOMPLETE"}:
            raise ValueError("invalid workflow_status")
        if experiment_verdict not in {"PASS", "FAIL", "UNMEASURED", "INCOMPLETE"}:
            raise ValueError("invalid experiment_verdict")
        if workflow_status == "PASS" and not (caller_completed is True and cleanup_succeeded is True):
            raise ValueError("workflow PASS requires caller_completed and cleanup_succeeded")
        with self._lock:
            if self._final is not None:
                return self._final
            if self._pending_final is None:
                bounded_reason, reason_truncated = self._bounded_terminal_reason(
                    terminal_reason, workflow_status, experiment_verdict)
                if not self._terminal_written:
                    terminal = {"kind": "run_terminal", "workflow_status": workflow_status,
                                "experiment_verdict": experiment_verdict,
                                "reason": bounded_reason}
                    if reason_truncated:
                        terminal["reason_truncated"] = True
                    encoded_terminal = json.dumps(terminal, sort_keys=True, separators=(",", ":"))
                    self._append_event(terminal, encoded_terminal)
                    self._terminal_written = True
                supplied = self.context.get("artifacts", {})
                artifacts = {"events": self._relative(self.events_path)}
                for name in ("serial_log", "metrics", "device_log"):
                    artifacts[name] = self._validated_artifact(name, supplied.get(name))
                loss = {"dropped_events": self._dropped_events}
                self._pending_final = {
                    "document": DOCUMENT, "run_id": self.run_id,
                    "started_at_utc": self.started_at_utc, "ended_at_utc": _utc(ended_at_utc),
                    **self.context, "workflow_status": workflow_status,
                    "experiment_verdict": experiment_verdict,
                    "terminal_reason": bounded_reason,
                    "terminal_reason_truncated": reason_truncated,
                    "event_loss": loss, "artifacts": artifacts,
                }
            _atomic_json(self.manifest_path, self._pending_final)
            self._final = self._pending_final
            return self._final

    def _relative(self, path: Path) -> str:
        try:
            return path.resolve().relative_to(self.repo_root).as_posix()
        except ValueError:
            return "unknown"


def sha256_file(path: Path) -> str | None:
    """Return a source/artifact hash, or None when provenance is unavailable."""
    try:
        digest = hashlib.sha256()
        with Path(path).open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError:
        return None
