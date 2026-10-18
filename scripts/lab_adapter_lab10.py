"""LAB-10 runner adapter: bounded local capture and structured evidence."""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path
from typing import Any

from lab_runner import RunHooks, run_with_adapter


ADAPTER_IDENTITY = "lab10"
ADAPTER_VERSION = "v1"
MAX_SERIAL_BYTES = 2 * 1024 * 1024
MAX_CAPTURE_SECONDS = 180
SCRIPTED_STARTUP_TIMEOUT_SECONDS = 60
# Bound differential marker receipt latency to two 250 ms serial-read intervals.
SCRIPTED_COMPLETION_EARLY_TOLERANCE_SECONDS = 0.5
SCRIPTED_COMPLETION_GRACE_SECONDS = 5
SCRIPTED_WORKLOAD_IDENTITY = "scripted-human-like-multicolor-180s-v1"
SCRIPTED_REQUIRED_COLOR_MASK = 0x3E
_SCRIPTED_DRAWING_PREFIX = b"LAB10_SCRIPTED_DRAWING "
_CAPTURE_TERMINAL_PREFIX = b"LAB10_CAPTURE_TERMINAL "
_CAPTURE_TERMINAL_OUTCOMES = frozenset(
    {
        b"completed",
        b"stopped",
        b"cancelled",
        b"duration_cap",
        b"point_cap",
        b"queue_overflow",
        b"failed",
    }
)
_SCENARIO_TERMINAL_STATUSES = frozenset(
    {"passed", "failed", "cancelled", "not_configured"}
)


def _parser_module(host, spec):
    tools = (host.ROOT / spec.project_dir / "tools").resolve()
    if str(tools) not in sys.path:
        sys.path.insert(0, str(tools))
    import parse_lab_trace

    return parse_lab_trace


def _workload(mode: str) -> str:
    if mode in {"scripted", "deterministic"}:
        # Synthetic identities belong to the separate host trace generator.
        return SCRIPTED_WORKLOAD_IDENTITY
    default = "manual-no-stroke-audio-control"
    value = os.environ.get("LAB10_WORKLOAD", default).strip()
    if value:
        return value
    return default


def _marker(name: str, **fields: object) -> bytes:
    encoded = " ".join(f"{key}={value}" for key, value in fields.items())
    return f"{name} {encoded}\n".encode("utf-8")


def _scripted_drawing_fields(line: bytes) -> dict[bytes, bytes] | None:
    if not line.startswith(_SCRIPTED_DRAWING_PREFIX):
        return None
    fields: dict[bytes, bytes] = {}
    for token in line[len(_SCRIPTED_DRAWING_PREFIX):].split():
        if b"=" not in token:
            return None
        key, value = token.split(b"=", 1)
        if not key or not value or key in fields:
            return None
        fields[key] = value
    return fields


def _scripted_start_line_valid(line: bytes) -> bool:
    fields = _scripted_drawing_fields(line)
    return fields is not None and all(
        fields.get(key) == value
        for key, value in {
            b"status": b"started",
            b"source": b"ui_task",
            b"hardware_touch": b"0",
            b"duration_cap_ms": b"180000",
            b"cadence_min_ms": b"36",
            b"cadence_profile": b"scripted-cadence-36-36-36-40-40-v2",
            b"cadence_max_ms": b"52",
            b"stroke_points": b"200",
            b"planned_strokes": b"18",
            b"planned_palette_changes": b"17",
            b"required_color_mask": b"0x3e",
            b"pen_up_gap_ms": b"400",
            b"pause_ms": b"780",
            b"accepted_point_cap": b"4096",
        }.items()
    )


def _scripted_completion_line_valid(line: bytes) -> bool:
    fields = _scripted_drawing_fields(line)
    if fields is None:
        return False
    if fields.get(b"status") != b"complete":
        return False
    try:
        accepted_points = int(fields.get(b"accepted_points", b""))
        strokes = int(fields.get(b"strokes", b""))
        palette_changes = int(fields.get(b"palette_changes", b""))
        accepted_color_mask = int(fields.get(b"accepted_color_mask", b""), 0)
    except ValueError:
        return False
    return (
        fields.get(b"source") == b"ui_task"
        and fields.get(b"hardware_touch") == b"0"
        and fields.get(b"terminal") == b"duration_cap"
        and fields.get(b"duration_cap_ms") == b"180000"
        and 3400 <= accepted_points <= 4095
        and 6 <= strokes <= 18
        and 5 <= palette_changes <= 17
        and strokes == palette_changes + 1
        and 0 <= accepted_color_mask <= 0x3F
        and accepted_color_mask & SCRIPTED_REQUIRED_COLOR_MASK
        == SCRIPTED_REQUIRED_COLOR_MASK
    )


def _capture_terminal_outcome(line: bytes) -> bytes | None:
    """Return the outcome from an exact, one-event firmware terminal marker."""

    if line.endswith(b"\r"):
        line = line[:-1]
    if not line.startswith(_CAPTURE_TERMINAL_PREFIX):
        return None
    fields: dict[bytes, bytes] = {}
    for token in line[len(_CAPTURE_TERMINAL_PREFIX):].split():
        if b"=" not in token:
            return None
        key, value = token.split(b"=", 1)
        if not key or not value or key in fields:
            return None
        fields[key] = value
    if (
        set(fields) != {b"outcome", b"terminal_event_count"}
        or fields[b"outcome"] not in _CAPTURE_TERMINAL_OUTCOMES
        or fields[b"terminal_event_count"] != b"1"
    ):
        return None
    return fields[b"outcome"]


def _scripted_completion_observed(path: Path) -> bool:
    """Return true only for a well-formed observed scripted completion line."""

    try:
        payload = path.read_bytes()
    except OSError:
        return False
    return any(_scripted_completion_line_valid(line) for line in payload.splitlines())


def _scenario_terminal_marker_status(path: Path, workload: str) -> str:
    """Read the host-appended terminal scenario status from the serial artifact."""

    try:
        terminal_line = path.read_bytes().splitlines()[-1]
    except (OSError, IndexError):
        return "not_configured"
    for status in _SCENARIO_TERMINAL_STATUSES:
        expected = _marker("LAB10_SCENARIO", id=workload, status=status).rstrip(b"\n")
        if terminal_line == expected:
            return status
    return "not_configured"


def _scenario_stage_from_status(status: str, mode: str) -> str:
    """Mirror the host-appended terminal marker in the scenario stage.

    Parser/readiness failures remain reflected by the overall workflow status;
    they must not rewrite the separately recorded terminal scenario outcome.
    """

    if mode != "scripted":
        return "not_configured"
    if status == "passed":
        return "passed"
    if status == "failed":
        return "failed"
    if status == "cancelled":
        return "incomplete"
    if status == "not_configured":
        return "not_configured"
    return "incomplete"


def _capture_command(mode: str) -> str:
    return "make lab.lab10.scripted" if mode == "scripted" else "make lab.lab10"


def _print_capture_active(target, args, mode: str, seconds: int) -> None:
    """Print the operator banner immediately before bounded serial capture."""

    backend = getattr(args, "backend", "managed-http")
    kit = getattr(target, "kit_id", "unknown")
    print(
        f"CAPTURE ACTIVE — command={_capture_command(mode)}; backend={backend}; "
        f"mode={mode}; kit={kit}; remaining={seconds}s",
        flush=True,
    )
    if mode == "scripted":
        print(
            "Scripted mode: the UI task draws 18 paced strokes with real "
            "palette selections, deterministic 36/36/36/40/40 ms cadence "
            "(within allowed 36–52 ms bounds), and bounded "
            "gaps/pauses through the 180-second capture; no synthetic "
            "hardware-touch evidence.",
            flush=True,
        )
    else:
        print(
            "Manual mode: draw or exercise the selected LAB10_WORKLOAD gesture "
            "case during capture.",
            flush=True,
        )


def _capture_serial_bounded(host, target, run_id: str, seconds: int, mode: str) -> Path:
    """Capture serial without allowing a noisy device to grow evidence forever."""

    if seconds < 1 or seconds > MAX_CAPTURE_SECONDS:
        raise ValueError(f"LAB-10 capture seconds must be 1..{MAX_CAPTURE_SECONDS}")
    try:
        import serial
    except ImportError as exc:  # pragma: no cover - exercised on an uninstalled host
        raise RuntimeError("pyserial is required for LAB-10 capture") from exc

    log_path = host.ROOT / "logs/device/labs/lab-10" / run_id / "serial.txt"
    log_path.parent.mkdir(parents=True, exist_ok=False)
    workload = _workload(mode)
    prefix = (
        _marker(
            "LAB10_RUN",
            run_id=run_id,
            mode=("scripted" if mode == "deterministic" else mode),
            workload=workload,
            adapter_version=ADAPTER_VERSION,
        )
        + _marker("LAB10_SCENARIO", id=workload, status="started")
    )
    if len(prefix) > MAX_SERIAL_BYTES:
        raise RuntimeError("LAB-10 lifecycle marker prefix exceeds serial log bound")

    connection = None
    total = 0
    completed = False
    scripted_completion_at = None
    scripted_start_count = 0
    capture_terminal_outcome = None
    pending_line = b""
    try:
        connection = serial.Serial(target.device, 115200, timeout=0.25)
        connection.dtr = False
        connection.rts = False
        connection.reset_input_buffer()
        with log_path.open("wb") as log:
            log.write(prefix)
            total = len(prefix)
            now = time.monotonic()
            capture_started_at = now if mode != "scripted" else None
            startup_deadline = now + SCRIPTED_STARTUP_TIMEOUT_SECONDS
            deadline = now + seconds
            while True:
                chunk = connection.read(4096)
                if chunk:
                    if total + len(chunk) > MAX_SERIAL_BYTES:
                        raise RuntimeError(
                            f"LAB-10 serial log exceeded {MAX_SERIAL_BYTES} byte bound"
                        )
                    log.write(chunk)
                    total += len(chunk)
                if chunk:
                    lines = (pending_line + chunk).split(b"\n")
                    pending_line = lines.pop()
                    for line in lines:
                        terminal_outcome = _capture_terminal_outcome(line)
                        if terminal_outcome is not None:
                            capture_terminal_outcome = terminal_outcome
                        if mode == "scripted":
                            if _scripted_start_line_valid(line):
                                scripted_start_count += 1
                                if scripted_start_count == 1:
                                    capture_started_at = time.monotonic()
                                    deadline = capture_started_at + seconds
                            elif _scripted_completion_line_valid(line):
                                scripted_completion_at = time.monotonic()
                now = time.monotonic()
                if capture_terminal_outcome is not None:
                    if mode != "scripted":
                        break
                    if capture_terminal_outcome != b"duration_cap":
                        # User stop/cancel and every abnormal terminal outcome
                        # remain promptly observable and can never qualify as
                        # a complete scripted workload.
                        break
                    if scripted_completion_at is not None:
                        # Duration-cap and completion markers can arrive in
                        # separate reads; retain both before closing capture.
                        break
                if mode == "scripted":
                    if scripted_start_count > 1:
                        break
                    if capture_started_at is None and now >= startup_deadline:
                        break
                    if capture_started_at is not None:
                        duration_deadline = capture_started_at + seconds
                        completion_deadline = (
                            duration_deadline + SCRIPTED_COMPLETION_GRACE_SECONDS
                        )
                        if now >= completion_deadline:
                            break
                elif now >= deadline:
                    break
            # Scripted success requires exact full-duration completion evidence.
            log.flush()
            scripted_completion_qualified = (
                scripted_start_count == 1
                and capture_started_at is not None
                and scripted_completion_at is not None
                and capture_terminal_outcome in {None, b"duration_cap"}
                and capture_started_at + seconds
                - SCRIPTED_COMPLETION_EARLY_TOLERANCE_SECONDS
                <= scripted_completion_at
                < capture_started_at + seconds + SCRIPTED_COMPLETION_GRACE_SECONDS
            )
            if mode != "scripted":
                scenario_status = "not_configured"
            elif scripted_completion_qualified:
                scenario_status = "passed"
            elif capture_terminal_outcome in {b"stopped", b"cancelled"}:
                scenario_status = "cancelled"
            else:
                scenario_status = "not_configured"
            terminal_marker = _marker("LAB10_SCENARIO", id=workload, status=scenario_status)
            if total + len(terminal_marker) > MAX_SERIAL_BYTES:
                raise RuntimeError(
                    "LAB-10 serial log has no room for its terminal scenario marker"
                )
            log.write(terminal_marker)
            total += len(terminal_marker)
            completed = True
    except KeyboardInterrupt:
        # The outer shared runner finalizes this as INCOMPLETE.  Keep an
        # explicit cancellation marker in the raw artifact when possible.
        try:
            with log_path.open("ab") as log:
                marker = _marker("LAB10_CANCEL", requested="1", reason="keyboard_interrupt")
                if total + len(marker) <= MAX_SERIAL_BYTES:
                    log.write(marker)
        except OSError:
            pass
        raise
    finally:
        if connection is not None:
            connection.close()

    if not completed:
        raise RuntimeError("LAB-10 capture did not reach its bounded deadline")
    return log_path


def _write_failure_metrics(parser_module, destination: Path, reason: str) -> dict[str, Any]:
    aggregation = parser_module.aggregator.aggregate_observations({
        "observation_schema": parser_module.aggregator.OBSERVATION_SCHEMA,
        "provenance": "unmeasured/blocked",
        "fields": {},
    }).to_dict()
    output = {
        "document": parser_module.PARSER_DOCUMENT,
        "parser": {
            "identity": parser_module.PARSER_IDENTITY,
            "version": parser_module.PARSER_VERSION,
            "adapter_identity": ADAPTER_IDENTITY,
            "adapter_version": ADAPTER_VERSION,
            "input_limit_bytes": parser_module.MAX_INPUT_BYTES,
            "output_limit_bytes": parser_module.MAX_OUTPUT_BYTES,
        },
        "run_id": None,
        "workflow_status": "FAIL",
        "experiment_verdict": "INCOMPLETE",
        "evidence_provenance": "unmeasured/blocked",
        "markers": {"ready": [], "run": [], "scenarios": [],
                    "scripted_drawing": [], "cancel": []},
        "diagnostics": {"errors": [reason], "trace_present": False},
        **aggregation,
    }
    encoded = json.dumps(output, ensure_ascii=False, allow_nan=False)
    if len(encoded.encode("utf-8")) > parser_module.MAX_OUTPUT_BYTES:
        raise RuntimeError("LAB-10 parser failure output exceeded its bound")
    destination.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return output


class Adapter:
    """Run manual and deterministic LAB-10 captures inside the shared envelope."""

    def run(self, host, config, spec, args, *, mode: str) -> int:
        requested_mode = getattr(args, "mode", None) or mode
        if requested_mode == "deterministic":
            requested_mode = "scripted"
        if requested_mode not in {"manual", "scripted"}:
            raise ValueError("LAB-10 mode must be manual or scripted")
        variant = args.variant or requested_mode
        if variant not in spec.variants:
            variant = spec.default_variant
        seconds = args.seconds if args.seconds is not None else spec.capture_seconds_default
        if seconds < 1 or seconds > MAX_CAPTURE_SECONDS:
            raise ValueError(f"LAB-10 capture seconds must be 1..{MAX_CAPTURE_SECONDS}")
        flash = host._flash_from_env_and_args(args, spec, for_run=True)
        parser_module = _parser_module(host, spec)
        result: dict[str, Any] = {}

        def execute(context, evidence):
            workflow = "FAIL"
            experiment = "UNMEASURED"
            completed = False
            cleanup_ok = False
            reason = "run_failed"
            serial_path: Path | None = None
            metrics_path = evidence.run_dir / "metrics.json"
            workload = _workload(requested_mode)
            stages = {
                "preflight": "not_started",
                "build": "not_started",
                "activity": "not_started",
                "parser": "not_started",
                # There is no independent host scenario hook for LAB-10.
                # Keep this unconfigured until the parsed device/adapter
                # terminal marker proves a scripted scenario completion.
                "scenario": "not_configured",
                "cleanup": "not_started",
            }

            def event(kind: str, outcome: str, **details: object) -> None:
                evidence.event_sink({
                    "source": "host",
                    "kind": kind,
                    "outcome": outcome,
                    "adapter_identity": ADAPTER_IDENTITY,
                    "adapter_version": ADAPTER_VERSION,
                    **details,
                })

            try:
                event("lab10_marker", "observed", marker="LAB10_RUN", run_id=context.run_id,
                      mode=requested_mode, workload=workload)
                stages["preflight"] = "running"
                event("stage", "running", stage="preflight")
                target = host.resolve_run_target(config, args.kit, args.port)
                evidence.update_context({
                    "source": {"revision": f"{ADAPTER_IDENTITY}/{ADAPTER_VERSION}",
                               "files": {"adapter": "scripts/lab_adapter_lab10.py",
                                         "parser": "client/labs/lab-10-record-canvas/tools/parse_lab_trace.py"}},
                    "firmware": {"project_path": str(spec.project_dir), "target": spec.target,
                                 "adapter_identity": ADAPTER_IDENTITY,
                                 "adapter_version": ADAPTER_VERSION},
                    "capture": {"mode": requested_mode, "variant": variant,
                                 "seconds": seconds, "status": "preflight"},
                    "scenarios": [workload],
                })
                stages["preflight"] = "passed"
                event("stage", "passed", stage="preflight")

                stages["build"] = "running"
                event("stage", "running", stage="build")
                if getattr(args, "no_build", False):
                    build_result = host.ROOT / host.resolve_variant(spec, variant).build_dir
                else:
                    build_result = host.build_lab(
                        config, spec.make_name, variant,
                        kit=target.kit_id, port=target.device, target=target,
                    )
                evidence.update_context({"build": {"status": "passed"}})
                stages["build"] = "passed"
                event("stage", "passed", stage="build", result=str(build_result))

                if flash:
                    port = host.revalidate_run_target(config, target)
                    host.flash_lab_build(config, build_result, port)
                    time.sleep(1.0)

                capture_candidate = (
                    host.ROOT / "logs/device/labs/lab-10" / context.run_id / "serial.txt"
                )
                serial_path = capture_candidate
                stages["activity"] = "running"
                event("stage", "running", stage="activity", mode=requested_mode)
                _print_capture_active(target, args, requested_mode, seconds)
                serial_path = _capture_serial_bounded(host, target, context.run_id, seconds, requested_mode)
                stages["activity"] = "passed"
                evidence.update_context({
                    "capture": {"status": "captured", "mode": requested_mode,
                                 "variant": variant},
                    "artifacts": {"serial_log": serial_path.relative_to(host.ROOT).as_posix()},
                })
                scenario_status = _scenario_terminal_marker_status(serial_path, workload)
                event("lab10_marker", "observed", marker="LAB10_SCENARIO", id=workload,
                      status=scenario_status, mode=requested_mode)
                event("stage", "passed", stage="activity", mode=requested_mode)

                event("stage", "running", stage="parser")
                stages["parser"] = "running"
                try:
                    parsed = parser_module.parse_file(
                        serial_path,
                        output_path=metrics_path,
                        expected_run_id=context.run_id,
                        expected_mode=requested_mode,
                    )
                except Exception as exc:
                    stages["parser"] = "failed"
                    parsed = _write_failure_metrics(parser_module, metrics_path, str(exc))
                    evidence.update_context({
                        "artifacts": {"metrics": metrics_path.relative_to(host.ROOT).as_posix()},
                        "capture": {"parser_status": "failed",
                                     "parser_identity": parser_module.PARSER_IDENTITY,
                                     "parser_version": parser_module.PARSER_VERSION,
                                     "evidence_provenance": parsed.get("evidence_provenance")},
                    })
                    raise RuntimeError(f"LAB-10 parser failed: {exc}") from exc
                evidence.update_context({
                    "artifacts": {"metrics": metrics_path.relative_to(host.ROOT).as_posix()},
                    "capture": {"parser_status": "parsed",
                                 "parser_identity": parser_module.PARSER_IDENTITY,
                                 "parser_version": parser_module.PARSER_VERSION,
                                 "evidence_provenance": parsed.get("evidence_provenance")},
                    "event_loss": {"firmware_trace": {
                        "status": "OBSERVED" if parsed.get("diagnostics", {}).get("trace_present") else "UNKNOWN",
                        "dropped_events": None,
                    }},
                })
                stages["parser"] = "passed"
                event("stage", "passed", stage="parser")
                stages["scenario"] = _scenario_stage_from_status(
                    scenario_status, requested_mode
                )
                event("stage", stages["scenario"], stage="scenario",
                      mode=requested_mode)
                workflow = parsed["workflow_status"]
                if requested_mode == "scripted" and scenario_status != "passed":
                    workflow = "INCOMPLETE"
                experiment = parsed["experiment_verdict"]
                completed = True
                reason = "completed" if workflow == "PASS" else "markers_or_evidence_incomplete"
            except KeyboardInterrupt:
                for stage_name, stage_status in stages.items():
                    if stage_status == "running":
                        stages[stage_name] = "incomplete"
                if serial_path is not None and serial_path.is_file():
                    try:
                        evidence.update_context({
                            "artifacts": {"serial_log": serial_path.relative_to(host.ROOT).as_posix()},
                        })
                        try:
                            parser_module.parse_file(
                                serial_path,
                                output_path=metrics_path,
                                expected_run_id=context.run_id,
                                expected_mode=requested_mode,
                            )
                        except Exception as exc:
                            _write_failure_metrics(parser_module, metrics_path, str(exc))
                        evidence.update_context({
                            "artifacts": {"metrics": metrics_path.relative_to(host.ROOT).as_posix()},
                            "capture": {"parser_status": "cancelled"},
                        })
                    except (OSError, ValueError):
                        pass
                event("lab10_marker", "observed", marker="LAB10_CANCEL", requested="1")
                workflow, experiment, reason = "INCOMPLETE", "UNMEASURED", "cancelled"
                raise
            except Exception as exc:
                for stage_name, stage_status in stages.items():
                    if stage_status == "running":
                        stages[stage_name] = "failed"
                if serial_path is not None and serial_path.is_file():
                    try:
                        evidence.update_context({
                            "artifacts": {"serial_log": serial_path.relative_to(host.ROOT).as_posix()},
                        })
                        if not metrics_path.exists():
                            failure_metrics = _write_failure_metrics(
                                parser_module, metrics_path, str(exc)
                            )
                            evidence.update_context({
                                "artifacts": {"metrics": metrics_path.relative_to(host.ROOT).as_posix()},
                                "capture": {"parser_status": "not_run",
                                             "evidence_provenance": failure_metrics.get("evidence_provenance")},
                            })
                    except (OSError, ValueError):
                        pass
                workflow = "FAIL"
                experiment = "INCOMPLETE" if serial_path is not None else "UNMEASURED"
                reason = type(exc).__name__
                event("stage", "failed", stage="run", reason=str(exc))
            finally:
                try:
                    evidence.update_context({"capture": {"status": "complete" if completed else "incomplete"}})
                    stages["cleanup"] = "passed"
                    cleanup_ok = True
                    event("stage", "passed", stage="cleanup")
                except Exception as exc:
                    stages["cleanup"] = "failed"
                    reason = "cleanup_failed"
                    workflow = "FAIL"
                    cleanup_ok = False
                    event("stage", "failed", stage="cleanup", reason=str(exc))
                result.update({
                    "workflow_status": workflow,
                    "experiment_verdict": experiment,
                    "caller_completed": completed,
                    "cleanup_succeeded": cleanup_ok,
                    "terminal_reason": reason,
                    "stages": stages,
                })
            return result

        # The custom execute hook keeps parser output inside the same atomic
        # run envelope while leaving the shared generic lifecycle untouched.
        return run_with_adapter(
            repo_root=host.ROOT,
            lab_id=spec.lab_id,
            evidence_slug=spec.evidence_slug,
            variant=variant,
            mode=requested_mode,
            requested_run_id=args.run_id,
            hooks=RunHooks(lambda: None, lambda _target: None, lambda _target, _rid: None),
            project_path=str(spec.project_dir),
            firmware_target=spec.target,
            capture_seconds=seconds,
            execute=execute,
        )
