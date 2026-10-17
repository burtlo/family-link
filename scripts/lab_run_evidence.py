"""Shared v2 evidence writer for registry-driven host runs.

This module intentionally has no dependency on any lab adapter. Keep the
sanitization, bounded event journal, partial-run finalization, and artifact
containment rules aligned with the LAB-02 compatibility writer.
"""
from __future__ import annotations

import json
import os
import re
import threading
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

GENERIC_DOCUMENT = "family-link.lab-run-manifest/v2"
_RUN_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
_SECRET_KEY = re.compile(r"password|secret|token|credential|ssid|wifi|(?:^|_)serial(?:$|_number|_binding)|url|endpoint|auth", re.I)
_SECRET_TEXT = re.compile(r"(?i)https?://[^\s\"']+|(?:ssid|password|token|authorization)\s*[=:]\s*[^\s,;]+")
_SAFE_CMAKE = re.compile(r"^(?:LAB02_[A-Z0-9_]+|CONFIG_[A-Z0-9_]+)$")
_SAFE_FIELDS = {
    "lab_id", "lab_revision", "lab_version_string", "evidence_slug", "verification_levels",
    "firmware", "server", "fixture", "hardware", "scenarios", "comparison_run_id",
    "operator", "source", "build", "capture", "workflow_status", "experiment_verdict",
    "terminal_reason", "artifacts", "event_loss",
}
_MAX_EVENT_BYTES = 16 * 1024
_EVENT_SOURCES = {"host", "server", "device", "unknown"}


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
    """Sanitized, bounded and atomic writer for generic v2 host-run evidence."""

    def __init__(self, repo_root: Path, run_id: str, context: dict, *,
                 started_at_utc: str | None = None, max_events: int = 2000,
                 lab_id: str, evidence_slug: str,
                 evidence_root: str | Path | None = None,
                 serial_root: str | Path | None = None):
        if not isinstance(run_id, str) or not _RUN_ID.fullmatch(run_id) or run_id in {".", ".."}:
            raise ValueError("run_id must be a safe single path segment")
        if max_events < 1:
            raise ValueError("max_events must be positive")
        if not isinstance(lab_id, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", lab_id):
            raise ValueError("lab_id must be a safe registered identifier")
        if not isinstance(evidence_slug, str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", evidence_slug):
            raise ValueError("evidence_slug must be a safe registered slug")
        self.repo_root = Path(repo_root).resolve()
        self.run_id = run_id
        self.lab_id = lab_id
        self.evidence_slug = evidence_slug
        self.evidence_root = self._approved_root(evidence_root or f"docs/labs/evidence/{evidence_slug}/runs")
        self.serial_root = self._approved_root(serial_root or f"logs/device/labs/{evidence_slug}")
        self.run_dir = self.evidence_root / run_id
        if self.run_dir.exists():
            raise FileExistsError(f"evidence run already exists: {run_id}")
        self.run_dir.mkdir(parents=True, exist_ok=False)
        self.events_path = self.run_dir / "events.jsonl"
        self.manifest_path = self.run_dir / "run-manifest.json"
        self.context = self._normalize_context(context)
        self.context["lab_id"] = lab_id
        self.context["evidence_slug"] = evidence_slug
        self.started_at_utc = _utc(started_at_utc)
        self.max_events = max_events
        self._events = []
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
            return out
        out.setdefault("lab_revision", None)
        out.setdefault("lab_version_string", None)
        out.setdefault("verification_levels", {})
        out["verification_levels"] = out["verification_levels"] if isinstance(out["verification_levels"], dict) else {}
        for key, default in (("firmware", {}), ("server", {}), ("fixture", {}),
                             ("hardware", {}), ("scenarios", []), ("source", {}),
                             ("build", {}), ("capture", {}), ("artifacts", {})):
            value = out.get(key)
            out[key] = value if isinstance(value, type(default)) else default
        out.setdefault("comparison_run_id", None)
        out.setdefault("operator", None)
        out["server"].setdefault("path", None)
        out["server"].setdefault("revision", None)
        out["server"].setdefault("source_sha256", None)
        for key in ("schema", "revision", "sha256"):
            out["fixture"].setdefault(key, None)
        out["fixture"].setdefault("file", None)
        for key in ("board", "kit_nickname"):
            out["hardware"].setdefault(key, None)
        out["source"].setdefault("revision", None)
        out["source"].setdefault("dirty", None)
        out["source"].setdefault("files", {})
        out["build"].setdefault("status", "unknown")
        out["capture"].setdefault("status", "not_started")
        event_loss = out.get("event_loss") if isinstance(out.get("event_loss"), dict) else {}
        if not isinstance(event_loss.get("firmware_trace"), dict):
            event_loss["firmware_trace"] = {"status": "UNKNOWN", "dropped_events": None}
        out["event_loss"] = event_loss
        fw = out["firmware"]
        for key in ("build_id", "source_revision", "source_sha256", "binary_sha256",
                    "build_stamp", "project_path", "idf_version", "sdkconfig_sha256"):
            fw.setdefault(key, None)
        # A generic runner may not know how the firmware was built. Preserve
        # that as an explicit unknown while keeping the v2 field present.
        if not isinstance(fw.get("build_fingerprint"), str):
            fw["build_fingerprint"] = None
        fw.setdefault("cmake_options", {})
        if isinstance(fw.get("cmake_options"), dict):
            fw["cmake_options"] = {k: v for k, v in fw["cmake_options"].items()
                                   if isinstance(k, str) and _SAFE_CMAKE.fullmatch(k)}
        return out

    def update_context(self, facts: dict) -> None:
        if not isinstance(facts, dict):
            raise ValueError("evidence facts must be an object")
        with self._lock:
            if self._pending_final is not None:
                raise RuntimeError("evidence context is frozen at finalization")
            self.context = _merge(self.context, self._normalize_context(facts, defaults=False))
            fw = self.context.get("firmware")
            if isinstance(fw, dict):
                if not isinstance(fw.get("build_fingerprint"), str):
                    fw["build_fingerprint"] = None
                if isinstance(fw.get("cmake_options"), dict):
                    fw["cmake_options"] = {k: v for k, v in fw["cmake_options"].items()
                                           if isinstance(k, str) and _SAFE_CMAKE.fullmatch(k)}

    def event_sink(self, event: dict) -> None:
        row = _clean(event if isinstance(event, dict) else {"kind": "invalid_event"})
        if row.get("source") not in _EVENT_SOURCES:
            row["source"] = "unknown"
        encoded = json.dumps(row, sort_keys=True, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > _MAX_EVENT_BYTES:
            row = {"source": row["source"], "kind": "event_oversized"}
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
            self._append_event({"source": "host", "kind": "events_truncated"},
                               '{"kind":"events_truncated","source":"host"}')

    def _validated_artifact(self, name: str, value: Any) -> str | None:
        if not isinstance(value, str) or not value or "\\" in value:
            return None
        candidate = PurePosixPath(value)
        if candidate.is_absolute() or any(part in {"", ".", ".."} for part in candidate.parts):
            return None
        expected = PurePosixPath(self.evidence_root.relative_to(self.repo_root).as_posix()) / self.run_id
        serial = PurePosixPath(self.serial_root.relative_to(self.repo_root).as_posix()) / self.run_id
        allowed = ((serial / "serial.txt",) if name in {"serial_log", "device_log"} else
                   (expected / "metrics.json",) if name == "metrics" else
                   (expected / "server.log",) if name == "server_log" else ())
        if candidate not in allowed:
            return None
        actual = self.repo_root.joinpath(*candidate.parts)
        approved = self.repo_root.joinpath(*(serial.parts if name in {"serial_log", "device_log"} else expected.parts))
        try:
            actual.resolve(strict=True).relative_to(approved.resolve(strict=True))
            actual.resolve(strict=True).relative_to(self.repo_root)
        except (OSError, ValueError):
            return None
        return candidate.as_posix() if actual.is_file() else None

    def finalize(self, *, workflow_status: str, experiment_verdict: str,
                 caller_completed: bool, cleanup_succeeded: bool,
                 ended_at_utc: str | None = None, terminal_reason: str | None = None) -> dict:
        if workflow_status not in {"PASS", "FAIL", "INCOMPLETE"}:
            raise ValueError("invalid workflow_status")
        if experiment_verdict not in {"PASS", "FAIL", "UNMEASURED", "INCOMPLETE"}:
            raise ValueError("invalid experiment_verdict")
        if workflow_status == "PASS" and not (caller_completed is True and cleanup_succeeded is True):
            raise ValueError("workflow PASS requires caller_completed and cleanup_succeeded")
        with self._lock:
            if self._final is not None:
                return self._final
            reason, reason_truncated = self._bounded_terminal_reason(
                terminal_reason, workflow_status, experiment_verdict)
            if self._pending_final is None:
                if not self._terminal_written:
                    terminal = {"source": "host", "kind": "run_terminal", "workflow_status": workflow_status,
                                "experiment_verdict": experiment_verdict, "reason": reason}
                    if reason_truncated:
                        terminal["reason_truncated"] = True
                    self._append_event(terminal, json.dumps(terminal, sort_keys=True, separators=(",", ":")))
                    self._terminal_written = True
                supplied = self.context.get("artifacts", {})
                artifacts = {"events": self.events_path.resolve().relative_to(self.repo_root).as_posix()}
                for name in ("serial_log", "metrics", "device_log", "server_log"):
                    artifacts[name] = self._validated_artifact(name, supplied.get(name))
                loss_context = self.context.get("event_loss")
                firmware_trace = (loss_context.get("firmware_trace")
                                  if isinstance(loss_context, dict) else None)
                if not isinstance(firmware_trace, dict):
                    firmware_trace = {"status": "UNKNOWN", "dropped_events": None}
                self._pending_final = {
                    "document": GENERIC_DOCUMENT, "run_id": self.run_id,
                    "started_at_utc": self.started_at_utc, "ended_at_utc": _utc(ended_at_utc),
                    **self.context, "workflow_status": workflow_status,
                    "experiment_verdict": experiment_verdict, "terminal_reason": reason,
                    "terminal_reason_truncated": reason_truncated,
                    "event_loss": {
                        "host_journal": {"status": "OBSERVED" if self._dropped_events else "NONE",
                                         "dropped_events": self._dropped_events},
                        "firmware_trace": firmware_trace,
                    }, "artifacts": artifacts,
                }
            _atomic_json(self.manifest_path, self._pending_final)
            self._final = self._pending_final
            return self._final

    def _bounded_terminal_reason(self, reason: Any, workflow_status: str,
                                 experiment_verdict: str) -> tuple[str, bool]:
        cleaned = _clean(reason or "")
        if not isinstance(cleaned, str):
            cleaned = "[UNSUPPORTED]"

        def encoded_size(value: str, truncated: bool) -> int:
            row = {"source": "host", "kind": "run_terminal", "workflow_status": workflow_status,
                   "experiment_verdict": experiment_verdict, "reason": value}
            if truncated:
                row["reason_truncated"] = True
            return len(json.dumps(row, sort_keys=True, separators=(",", ":")).encode("utf-8"))

        if encoded_size(cleaned, False) <= _MAX_EVENT_BYTES:
            return cleaned, False
        marker = "[truncated]"
        low, high = 0, len(cleaned)
        while low < high:
            mid = (low + high + 1) // 2
            if encoded_size(cleaned[:mid] + marker, True) <= _MAX_EVENT_BYTES:
                low = mid
            else:
                high = mid - 1
        return cleaned[:low] + marker, True

    def _approved_root(self, value: str | Path) -> Path:
        raw = str(value)
        posix = PurePosixPath(raw)
        if not posix.parts or posix.is_absolute() or "\\" in raw or any(part in {"", ".", ".."} for part in posix.parts):
            raise ValueError("artifact roots must be safe repo-relative paths")
        candidate = self.repo_root.joinpath(*posix.parts)
        try:
            candidate.resolve().relative_to(self.repo_root)
        except ValueError as exc:
            raise ValueError("artifact root must resolve beneath repo_root") from exc
        return candidate
