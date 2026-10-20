"""LAB-05 peak host adapter: reset-gated capture and decided-input evidence.

EvidenceRun is used only through run_with_adapter. This module does not
import the LAB-02 adapter or bind a TCP port.
"""
from __future__ import annotations

import importlib.util
import sys
import time
from pathlib import Path

import server_log
from lab_runner import RunHooks, run_with_adapter
from project_config import ProjectConfigError

ADAPTER_VERSION = "v1"
MAX_CAPTURE_SECONDS = 900
MAX_SERIAL_BYTES = 4 * 1024 * 1024
MAX_LINE_BYTES = 4096
BOOT_TIMEOUT_SECONDS = 20
HARNESS_TIMEOUT_SECONDS = 90
RESET_HOLD_SECONDS = 0.1
# Named host deadlines copied as host limits. They do not start a server.
HOST_DEADLINES_SECONDS = {
    "startup": 10,
    "health": 10,
    "seed": 10,
    "readiness": 15,
    "action": 5,
    "cleanup": 5,
}


def _parser_module(spec):
    parser_path = spec.trace_parser
    if parser_path is None or not Path(parser_path).is_file():
        raise ProjectConfigError("LAB-05 peak trace parser is not configured")
    name = "lab05peak_parse_lab05_peak"
    module = sys.modules.get(name)
    if module is None:
        spec_module = importlib.util.spec_from_file_location(name, parser_path)
        if spec_module is None or spec_module.loader is None:
            raise ProjectConfigError("could not load LAB-05 peak trace parser")
        module = importlib.util.module_from_spec(spec_module)
        sys.modules[name] = module
        spec_module.loader.exec_module(module)
    return module


def _control_for_variant(variant: str) -> str:
    if variant in {"replace", "scripted-replace"}:
        return "replace"
    return "persistent"


def _selected_variant(spec, args, mode: str) -> str:
    requested = getattr(args, "variant", None) or ""
    if requested:
        selected = requested
    elif mode in spec.variants:
        selected = mode
    else:
        selected = spec.default_variant
    if selected not in spec.variants:
        raise ProjectConfigError(f"unknown LAB-05 peak variant: {selected}")
    return selected


def _safe_line(raw: bytes, parser) -> bytes:
    if len(raw) > MAX_LINE_BYTES:
        raise RuntimeError("serial line exceeded byte bound")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise RuntimeError("serial emitted non-UTF8 data") from exc
    parser.validate_evidence_line(text)
    redacted = server_log.redact_line(text.encode("utf-8"))
    if redacted.endswith(b"\n"):
        return redacted
    return redacted + b"\n"


def _cleanup() -> None:
    """No server is owned. cleanup deadline is HOST_DEADLINES_SECONDS['cleanup']."""
    _ = HOST_DEADLINES_SECONDS["cleanup"]


def _marker_present(text: str, mode: str, control: str) -> bool:
    ready = False
    harness = False
    for line in text.splitlines():
        stripped = line.strip()
        if (
            stripped.startswith("LAB05_PEAK_READY ")
            and "status=" not in stripped
            and f"control={control}" in stripped
            and "server=none" in stripped
        ):
            ready = True
        if (
            stripped.startswith("LAB05_PEAK_HARNESS_DONE ")
            and "status=complete" in stripped
            and f"control={control}" in stripped
        ):
            harness = True
    if mode == "scripted":
        return ready and harness
    return ready


def _capture(host, target, run_id: str, seconds: int, mode: str, control: str, parser) -> Path:
    if not 1 <= seconds <= MAX_CAPTURE_SECONDS:
        raise ValueError(f"LAB-05 peak capture seconds must be 1..{MAX_CAPTURE_SECONDS}")
    try:
        import serial
    except ImportError as exc:  # pragma: no cover - depends on host installation
        raise RuntimeError("pyserial is required for LAB-05 peak capture") from exc

    path = host.ROOT / "logs" / "device" / "labs" / "lab-05-peak" / run_id / "serial.txt"
    path.parent.mkdir(parents=True, exist_ok=False)
    connection = None
    total = 0
    ready_at = None
    harness_count = 0
    started_at = time.monotonic()
    pending = b""
    try:
        connection = serial.Serial(target.device, 115200, timeout=0.2)
        connection.dtr = False
        connection.rts = True
        time.sleep(RESET_HOLD_SECONDS)
        connection.reset_input_buffer()
        connection.rts = False
        with path.open("wb") as log:
            while True:
                chunk = connection.read(2048)
                if chunk:
                    if total + len(chunk) > MAX_SERIAL_BYTES:
                        raise RuntimeError("LAB-05 peak serial log exceeded byte bound")
                    pending += chunk
                    pieces = pending.split(b"\n")
                    pending = pieces.pop()
                    if len(pending) > MAX_LINE_BYTES:
                        raise RuntimeError("LAB-05 peak serial line exceeded byte bound")
                    for piece in pieces:
                        safe = _safe_line(piece, parser)
                        if total + len(safe) > MAX_SERIAL_BYTES:
                            raise RuntimeError("LAB-05 peak serial log exceeded byte bound")
                        log.write(safe)
                        total += len(safe)
                        text = safe.decode("utf-8").strip()
                        if text.startswith("LAB05_PEAK_READY ") and "status=" not in text and ready_at is None:
                            ready_at = time.monotonic()
                        if text.startswith("LAB05_PEAK_HARNESS_DONE ") and "status=complete" in text:
                            harness_count += 1
                now = time.monotonic()
                boot_deadline = started_at + BOOT_TIMEOUT_SECONDS
                if mode == "scripted" and harness_count >= 1 and ready_at is not None:
                    break
                if ready_at is None and now >= boot_deadline:
                    break
                if mode == "manual" and ready_at is not None and now >= ready_at + seconds:
                    break
                if (
                    mode == "scripted"
                    and ready_at is not None
                    and now >= ready_at + HARNESS_TIMEOUT_SECONDS
                ):
                    break
            if pending:
                safe = _safe_line(pending, parser)
                if total + len(safe) > MAX_SERIAL_BYTES:
                    raise RuntimeError("LAB-05 peak serial log exceeded byte bound")
                log.write(safe)
            log.flush()
    finally:
        if connection is not None:
            connection.close()
    captured = path.read_text(encoding="utf-8")
    if not _marker_present(captured, mode, control):
        raise RuntimeError("LAB-05 peak success marker missing after armed reset")
    return path


class Adapter:
    def run(self, host, config, spec, args, *, mode: str) -> int:
        backend = getattr(args, "backend", "managed-http")
        if backend not in {None, "", "managed-http"}:
            raise ProjectConfigError("LAB-05 peak supports no BACKEND value; no port is bound")
        requested_mode = getattr(args, "mode", None) or mode
        if requested_mode not in {"manual", "scripted"}:
            raise ProjectConfigError("LAB-05 peak mode must be manual or scripted")
        selected = _selected_variant(spec, args, requested_mode)
        control = _control_for_variant(selected)
        seconds = args.seconds if args.seconds is not None else spec.capture_seconds_default
        if not 1 <= seconds <= MAX_CAPTURE_SECONDS:
            raise ProjectConfigError(f"LAB-05 peak capture seconds must be 1..{MAX_CAPTURE_SECONDS}")
        flash = host._flash_from_env_and_args(args, spec, for_run=True)
        parser = _parser_module(spec)

        def execute(context, evidence):
            stages = {
                "preflight": "not_started",
                "service": "not_configured",
                "build": "not_started",
                "activity": "not_started",
                "parser": "not_started",
                "scenario": "not_configured",
                "cleanup": "not_started",
            }
            cleanup_ok = False
            completed = False
            workflow = "FAIL"
            experiment = "UNMEASURED"
            reason = "run_failed"

            def event(outcome: str, stage: str, **fields):
                evidence.event_sink({
                    "source": "host",
                    "kind": "stage",
                    "stage": stage,
                    "outcome": outcome,
                    "adapter_identity": "lab05peak",
                    "adapter_version": ADAPTER_VERSION,
                    **fields,
                })

            try:
                evidence.update_context({
                    "firmware": {
                        "project_path": spec.project_dir.as_posix(),
                        "target": spec.target,
                    },
                    "server": {"path": "none"},
                    "capture": {
                        "mode": requested_mode,
                        "variant": selected,
                        "seconds": seconds,
                        "control": control,
                        "host_deadlines_seconds": dict(HOST_DEADLINES_SECONDS),
                    },
                    "scenarios": ["P-alloc-fail", "P-stale-callback", "P-cancel"],
                    "source": {"revision": "lab05peak-host/v1"},
                })
                event("not_configured", "service")
                event("not_configured", "scenario")
                event("running", "preflight")
                target = host.resolve_run_target(config, args.kit, args.port)
                stages["preflight"] = "passed"
                evidence.update_context({"hardware": {"kit_nickname": target.kit_id}})
                event("passed", "preflight")

                event("running", "build")
                if getattr(args, "no_build", False):
                    build_result = host.ROOT / host.resolve_variant(spec, selected).build_dir
                else:
                    build_result = host.build_lab(
                        config,
                        spec.make_name,
                        selected,
                        kit=target.kit_id,
                        port=target.device,
                        target=target,
                    )
                stages["build"] = "passed"
                evidence.update_context({"build": {"status": "passed"}})
                event("passed", "build", result=str(build_result))
                if flash:
                    port = host.revalidate_run_target(config, target)
                    host.flash_lab_build(config, build_result, port)

                event("running", "activity", mode=requested_mode)
                serial_path = _capture(
                    host, target, context.run_id, seconds, requested_mode, control, parser
                )
                relative_serial = serial_path.resolve().relative_to(host.ROOT.resolve()).as_posix()
                stages["activity"] = "passed"
                evidence.update_context({
                    "capture": {"status": "captured", "post_reset": True, "variant": selected},
                    "artifacts": {"serial_log": relative_serial},
                })
                event("passed", "activity", mode=requested_mode)

                event("running", "parser")
                metrics_path = evidence.run_dir / "metrics.json"
                parsed = parser.parse_file(
                    serial_path,
                    output_path=metrics_path,
                    expected_run_id=context.run_id,
                    expected_mode=requested_mode,
                    expected_variant=selected,
                    post_reset=True,
                    capture_context={
                        "kind": "device_serial_capture",
                        "target_id": target.kit_id,
                        "post_reset": True,
                    },
                )
                stages["parser"] = "passed"
                relative_metrics = metrics_path.resolve().relative_to(host.ROOT.resolve()).as_posix()
                evidence.update_context({
                    "artifacts": {"metrics": relative_metrics},
                    "capture": {"parser_status": "parsed", "parser_identity": "lab05-peak-trace/v1"},
                    "event_loss": {"firmware_trace": parsed["evidence"]["firmware_trace_loss"]},
                })
                captured = serial_path.read_text(encoding="utf-8")
                if not _marker_present(captured, requested_mode, control):
                    workflow = "FAIL"
                    experiment = "UNMEASURED"
                    completed = False
                    reason = "missing_post_reset_success_marker"
                else:
                    experiment = parsed["lab_metrics"]["experiment_verdict"]
                    completed = True
                    workflow = "PASS"
                    reason = "post_reset_capture_parsed"
            except KeyboardInterrupt:
                workflow = "INCOMPLETE"
                experiment = "UNMEASURED"
                completed = False
                reason = "KeyboardInterrupt"
            except Exception as exc:
                workflow = "FAIL"
                experiment = "UNMEASURED"
                completed = False
                reason = type(exc).__name__
                try:
                    event("failed", "run", reason=reason)
                except Exception:
                    pass
            finally:
                try:
                    _cleanup()
                    cleanup_ok = True
                    stages["cleanup"] = "passed"
                    event("passed", "cleanup")
                except Exception:
                    cleanup_ok = False
                    stages["cleanup"] = "failed"
                    workflow = "FAIL"
                    completed = False
                    if experiment == "PASS":
                        experiment = "UNMEASURED"
                    reason = "cleanup_failed"
                evidence.update_context({"capture": {"stages": stages}})
            if workflow != "PASS" and experiment == "PASS":
                experiment = "UNMEASURED"
            return {
                "workflow_status": workflow,
                "experiment_verdict": experiment,
                "caller_completed": completed,
                "cleanup_succeeded": cleanup_ok,
                "terminal_reason": reason,
                "stages": stages,
            }

        return run_with_adapter(
            repo_root=host.ROOT,
            lab_id=spec.lab_id,
            evidence_slug=spec.evidence_slug,
            variant=selected,
            mode=requested_mode,
            requested_run_id=args.run_id,
            hooks=RunHooks(lambda: None, lambda _target: None, lambda _target, _run_id: None),
            project_path=str(spec.project_dir),
            firmware_target=spec.target,
            capture_seconds=seconds,
            execute=execute,
        )
