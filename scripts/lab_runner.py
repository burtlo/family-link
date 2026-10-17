"""Shared host run context and stage lifecycle for registered device labs."""
from __future__ import annotations

import time
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from pathlib import Path
from typing import Callable

class StageStatus(str, Enum):
    NOT_CONFIGURED = "not_configured"
    NOT_STARTED = "not_started"
    RUNNING = "running"
    PASSED = "passed"
    FAILED = "failed"
    INCOMPLETE = "incomplete"


@dataclass
class RunContext:
    """Facts frozen for one run; secrets and private endpoints are omitted."""

    run_id: str
    lab_id: str
    evidence_slug: str
    mode: str
    variant: str
    started_at_utc: str
    stages: dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class RunHooks:
    """Host operations supplied to the engine by the CLI; adapters add behavior."""

    resolve_target: Callable[[], object]
    build: Callable[[object], object]
    capture: Callable[[object, str], object]
    server: Callable[[RunContext], object] | None = None
    scenario: Callable[[RunContext], object] | None = None
    cleanup: Callable[[RunContext], object] | None = None


def new_run_id(variant: str, requested: str = "") -> str:
    if requested:
        return requested
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d")
    return f"{stamp}-{variant}-run-{uuid.uuid4().hex[:8]}"


def run_with_adapter(*, repo_root: Path, lab_id: str, evidence_slug: str,
                     variant: str, mode: str, requested_run_id: str,
                     hooks: RunHooks, project_path: str | None = None,
                     firmware_target: str | None = None,
                     capture_seconds: int | None = None,
                     execute: Callable[[RunContext, object], dict] | None = None) -> int:
    """Run shared preflight/build/capture stages and preserve a partial envelope."""
    # Generic runs own their evidence writer; they must remain executable when
    # no LAB-02 compatibility module is installed or imported.
    from lab_run_evidence import EvidenceRun

    if mode not in {"manual", "scripted"}:
        raise ValueError("mode must be manual or scripted")
    rid = new_run_id(variant, requested_run_id)
    context = RunContext(rid, lab_id, evidence_slug, mode, variant,
                         datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"))
    # The evidence directory is allocated before target selection/preflight.
    evidence = EvidenceRun(
        repo_root, rid,
        {"capture": {"mode": mode, "variant": variant, "seconds": capture_seconds,
                      "status": "not_started"},
         "build": {"status": "not_started"}},
        lab_id=lab_id, evidence_slug=evidence_slug,
    )

    def event(kind: str, outcome: str, **details: object) -> None:
        evidence.event_sink({
            "source": "host", "kind": kind,
            "monotonic_ns": time.monotonic_ns(), "outcome": outcome,
            **details,
        })

    if execute is not None:
        workflow, experiment, reason = "FAIL", "UNMEASURED", "run_failed"
        completed = cleanup_ok = False
        try:
            result = execute(context, evidence)
            workflow = result.get("workflow_status", "FAIL")
            experiment = result.get("experiment_verdict", "UNMEASURED")
            completed = result.get("caller_completed") is True
            cleanup_ok = result.get("cleanup_succeeded") is True
            reason = result.get("terminal_reason", "run_failed")
            context.stages.update(result.get("stages", {}))
            if workflow == "PASS" and not (completed and cleanup_ok):
                workflow, reason = "FAIL", "invalid_success_result"
            if not cleanup_ok:
                workflow = "FAIL"
            evidence.update_context({"capture": {"stages": context.stages}})
        except (Exception, KeyboardInterrupt) as exc:
            reason = type(exc).__name__
            workflow = "INCOMPLETE" if isinstance(exc, KeyboardInterrupt) else "FAIL"
            try:
                event("stage", "failed", stage="run", reason=reason)
            except Exception:
                pass
        try:
            evidence.finalize(workflow_status=workflow, experiment_verdict=experiment,
                              caller_completed=completed, cleanup_succeeded=cleanup_ok,
                              terminal_reason=reason)
        except Exception:
            workflow = "FAIL"
        print(f"workflow_status={workflow} experiment_verdict={experiment} run_id={rid}")
        return 0 if workflow == "PASS" else 1

    cleanup_ok = True
    completed = False
    workflow = "FAIL"
    reason = "run_failed"
    try:
        context.stages["service"] = (StageStatus.NOT_CONFIGURED.value if hooks.server is None
                                      else StageStatus.NOT_STARTED.value)
        if hooks.server is None:
            event("stage", "not_configured", stage="service")
        context.stages["scenario"] = (StageStatus.NOT_CONFIGURED.value
                                       if hooks.scenario is None else StageStatus.NOT_STARTED.value)
        if hooks.scenario is None:
            event("stage", "not_configured", stage="scenario")

        event("stage", "running", stage="preflight")
        target = hooks.resolve_target()
        context.stages["preflight"] = StageStatus.PASSED.value
        kit_id = getattr(target, "kit_id", None)
        evidence.update_context({
            "hardware": {"kit_nickname": kit_id},
            "firmware": {"project_path": project_path, "target": firmware_target},
        })
        event("stage", "passed", stage="preflight")

        if hooks.server is not None:
            event("stage", "running", stage="service")
            hooks.server(context)
            context.stages["service"] = StageStatus.PASSED.value
            event("stage", "passed", stage="service")

        event("stage", "running", stage="build")
        build_result = hooks.build(target)
        context.stages["build"] = StageStatus.PASSED.value
        evidence.update_context({"build": {"status": "passed"}})
        event("stage", "passed", stage="build", result=str(build_result))

        event("stage", "running", stage="activity", mode=mode)
        serial_path = hooks.capture(target, rid)
        context.stages["activity"] = StageStatus.PASSED.value
        serial_artifact = _serial_artifact_path(repo_root, serial_path)
        evidence.update_context({
            "capture": {"status": "captured", "mode": mode, "variant": variant},
            "artifacts": {"serial_log": serial_artifact},
        })
        event("stage", "passed", stage="activity", mode=mode)
        if hooks.scenario is not None:
            context.stages["scenario"] = StageStatus.RUNNING.value
            event("stage", "running", stage="scenario", mode=mode)
            hooks.scenario(context)
            context.stages["scenario"] = StageStatus.PASSED.value
            event("stage", "passed", stage="scenario", mode=mode)
        completed = True
        workflow = "PASS"
        reason = "completed"
    except (Exception, KeyboardInterrupt) as exc:
        reason = type(exc).__name__
        event("stage", "failed", stage="run", reason=reason)
        workflow = "INCOMPLETE" if isinstance(exc, KeyboardInterrupt) else "FAIL"
        if isinstance(exc, KeyboardInterrupt):
            # Preserve partial evidence but let the CLI report an interrupted run.
            pass
    finally:
        try:
            if hooks.cleanup is not None:
                hooks.cleanup(context)
            context.stages["cleanup"] = StageStatus.PASSED.value
            event("stage", "passed", stage="cleanup")
        except Exception:
            context.stages["cleanup"] = StageStatus.FAILED.value
            cleanup_ok = False
            workflow = "FAIL"
            completed = False
            reason = "cleanup_failed"
            try:
                event("stage", "failed", stage="cleanup", reason=reason)
            except Exception:
                pass
        try:
            evidence.update_context({"capture": {"stages": context.stages}})
            evidence.finalize(
                workflow_status=workflow,
                experiment_verdict="UNMEASURED",
                caller_completed=completed,
                cleanup_succeeded=cleanup_ok,
                terminal_reason=reason,
            )
        except Exception:
            # Evidence failures must not be hidden behind a successful run.
            workflow = "FAIL"
    print(f"workflow_status={workflow} experiment_verdict=UNMEASURED run_id={rid}")
    return 0 if workflow == "PASS" else 1


def _serial_artifact_path(repo_root: Path, serial_path: str | Path) -> str:
    """Express an in-repository serial capture as a safe repo-relative path.

    Keep paths with explicit traversal, and paths outside the repository, in
    their original form so the evidence writer can reject them as before.
    """
    raw_path = Path(serial_path)
    if ".." in raw_path.parts:
        return str(serial_path)

    root = Path(repo_root).resolve()
    candidate = raw_path if raw_path.is_absolute() else root / raw_path
    try:
        relative = candidate.resolve().relative_to(root)
    except (OSError, ValueError):
        return str(serial_path)
    return relative.as_posix()
