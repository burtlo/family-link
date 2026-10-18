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
_SCRIPTED_DRAWING_PREFIX = b"LAB10_SCRIPTED_DRAWING "
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
    default = (
        "synthetic-sparse-multicolor-v1"
        if mode in {"scripted", "deterministic"}
        else "manual-no-stroke-audio-control"
    )
    value = os.environ.get("LAB10_WORKLOAD", default).strip()
    if value:
        return value
    return default


def _marker(name: str, **fields: object) -> bytes:
    encoded = " ".join(f"{key}={value}" for key, value in fields.items())
    return f"{name} {encoded}\n".encode("utf-8")


def _scripted_completion_observed(path: Path) -> bool:
    """Return true only for a well-formed observed scripted completion line."""

    try:
        payload = path.read_bytes()
    except OSError:
        return False
    for line in payload.splitlines():
        if not line.startswith(_SCRIPTED_DRAWING_PREFIX):
            continue
        fields: dict[bytes, bytes] = {}
        malformed = False
        for token in line[len(_SCRIPTED_DRAWING_PREFIX):].split():
            if b"=" not in token:
                malformed = True
                break
            key, value = token.split(b"=", 1)
            if not key or not value or key in fields:
                malformed = True
                break
            fields[key] = value
        if not malformed and fields.get(b"status") == b"complete":
            return True
    return False


def _scenario_stage_from_parsed(parsed: dict[str, Any], mode: str, workload: str) -> str:
    """Map parsed terminal scenario evidence to the manifest stage.

    The stage is deliberately narrower than the parser's workflow verdict:
    only a parsed, valid scripted completion may pass it.  Manual runs and
    scripted runs without a valid terminal marker remain unconfigured (or
    incomplete for an explicitly failed/cancelled scenario); none of these
    states promote the experiment verdict.
    """

    if mode != "scripted":
        return "not_configured"

    markers = parsed.get("markers")
    scenarios = markers.get("scenarios") if isinstance(markers, dict) else None
    if not isinstance(scenarios, list):
        return "incomplete"

    terminal = [
        item for item in scenarios
        if isinstance(item, dict)
        and item.get("id") == workload
        and item.get("status") in _SCENARIO_TERMINAL_STATUSES
    ]
    if not terminal:
        return "incomplete"

    status = terminal[-1]["status"]
    if status == "passed":
        diagnostics = parsed.get("diagnostics")
        completion_observed = (
            isinstance(diagnostics, dict)
            and diagnostics.get("scripted_completion_observed") is True
        )
        if parsed.get("workflow_status") == "PASS" and completion_observed:
            return "passed"
        return "incomplete"
    if status == "failed":
        return "failed"
    if status == "cancelled":
        return "incomplete"
    return "not_configured"


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
            "Scripted mode: the UI task injects three palette changes and three "
            "bounded canvas strokes; no operator touch or hardware-touch claim; "
            "recording remains active for inspection.",
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
    try:
        connection = serial.Serial(target.device, 115200, timeout=0.25)
        connection.dtr = False
        connection.rts = False
        connection.reset_input_buffer()
        with log_path.open("wb") as log:
            log.write(prefix)
            total = len(prefix)
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                chunk = connection.read(4096)
                if not chunk:
                    continue
                if total + len(chunk) > MAX_SERIAL_BYTES:
                    raise RuntimeError(
                        f"LAB-10 serial log exceeded {MAX_SERIAL_BYTES} byte bound"
                    )
                log.write(chunk)
                total += len(chunk)
            # A scripted workflow is complete only when the device emitted its
            # exact completion marker. Manual captures and scripted captures
            # without that marker remain explicitly unconfigured.
            log.flush()
            scenario_status = (
                "passed"
                if mode == "scripted" and _scripted_completion_observed(log_path)
                else "not_configured"
            )
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
                    build_result = host.ensure_lab_built(
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
                scenario_status = (
                    "passed"
                    if requested_mode == "scripted" and _scripted_completion_observed(serial_path)
                    else "not_configured"
                )
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
                stages["scenario"] = _scenario_stage_from_parsed(
                    parsed, requested_mode, workload
                )
                event("stage", stages["scenario"], stage="scenario",
                      mode=requested_mode)
                workflow = parsed["workflow_status"]
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
