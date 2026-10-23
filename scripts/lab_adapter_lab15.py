"""LAB-15 host lifecycle adapter: capture memory envelope scripted harness."""
from __future__ import annotations

import importlib.util
import sys
import time
from pathlib import Path
from types import SimpleNamespace

from lab_runner import RunHooks, run_with_adapter
from project_config import ProjectConfigError

ADAPTER_IDENTITY = "lab15"
ADAPTER_VERSION = "v1"
MAX_CAPTURE_SECONDS = 240
MAX_SERIAL_BYTES = 2 * 1024 * 1024
MAX_LINE_BYTES = 4096
BOOT_TIMEOUT_SECONDS = 45
HARNESS_TIMEOUT_SECONDS = 120
RESET_HOLD_SECONDS = 0.12
READY_PREFIX = "LAB15_READY "
HARNESS_DONE = "LAB15_HARNESS_DONE status=complete"


def _parser_module(spec):
    path = spec.trace_parser
    if path is None or not path.is_file():
        raise ProjectConfigError("LAB-15 trace parser is not configured")
    name = "lab15_parse_lab_trace"
    module = sys.modules.get(name)
    if module is None:
        load = importlib.util.spec_from_file_location(name, path)
        if load is None or load.loader is None:
            raise ProjectConfigError("cannot load LAB-15 parser")
        module = importlib.util.module_from_spec(load)
        sys.modules[name] = module
        load.loader.exec_module(module)
    return module


def _safe_line(raw: bytes, parser) -> bytes:
    if len(raw) > MAX_LINE_BYTES:
        raise RuntimeError("LAB-15 serial line exceeds byte bound")
    line = raw.decode("utf-8", errors="strict")
    parser.validate_evidence_line(line)
    return (parser.redact_line(line).rstrip("\r") + "\n").encode("utf-8")


def _capture_serial(device: str, serial_path: Path, seconds: int, mode: str, parser) -> str:
    if not 1 <= seconds <= MAX_CAPTURE_SECONDS:
        raise ValueError("LAB-15 capture seconds outside bound")
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required for LAB-15 capture") from exc
    serial_path.parent.mkdir(parents=True, exist_ok=True)
    connection = None
    pending = b""
    total = 0
    ready_count = 0
    harness_seen = False
    armed = False
    text_parts: list[str] = []
    started = time.monotonic()
    ready_deadline = started + BOOT_TIMEOUT_SECONDS
    finish_deadline = None
    try:
        connection = serial.Serial(device, 115200, timeout=0.2)
        connection.dtr = False
        connection.rts = True
        time.sleep(RESET_HOLD_SECONDS)
        connection.reset_input_buffer()
        connection.rts = False
        armed = True
        while True:
            chunk = connection.read(2048)
            if chunk:
                pending += chunk
                pieces = pending.split(b"\n")
                pending = pieces.pop()
                for piece in pieces:
                    if not piece:
                        continue
                    line = _safe_line(piece + b"\n", parser)
                    total += len(line)
                    if total > MAX_SERIAL_BYTES:
                        raise RuntimeError("LAB-15 serial capture exceeded byte bound")
                    text = line.decode("utf-8")
                    text_parts.append(text)
                    if text.startswith("LAB15_INIT_FAILED"):
                        raise RuntimeError("LAB-15 firmware init failed")
                    if text.startswith(READY_PREFIX):
                        ready_count += 1
                        ready_deadline = time.monotonic() + HARNESS_TIMEOUT_SECONDS
                        finish_deadline = ready_deadline
                    if HARNESS_DONE in text:
                        harness_seen = True
                        break
            now = time.monotonic()
            if mode == "scripted":
                if ready_count == 0 and now > ready_deadline:
                    raise RuntimeError("LAB15_READY missing after armed reset")
                if ready_count > 0 and finish_deadline and now > finish_deadline:
                    raise RuntimeError("LAB15_HARNESS_DONE missing after scripted deadline")
            elif now - started > seconds:
                break
            if harness_seen:
                break
            if not chunk:
                time.sleep(0.05)
    finally:
        if connection is not None:
            connection.close()
    if not armed:
        raise RuntimeError("LAB-15 capture never armed reset")
    if mode == "scripted" and not harness_seen:
        raise RuntimeError("LAB15_HARNESS_DONE missing")
    if ready_count != 1:
        raise RuntimeError("LAB15_READY missing or duplicated after armed reset")
    post_reset = "".join(text_parts)
    serial_path.write_text(post_reset, encoding="utf-8")
    return post_reset


def _classify_capture(*, post_reset: str, parser_verdict: str, cleanup_ok: bool, mode: str) -> dict:
    ready = READY_PREFIX in post_reset
    harness = HARNESS_DONE in post_reset
    if mode == "scripted" and not (ready and harness):
        return {
            "workflow_status": "FAIL",
            "experiment_verdict": "UNMEASURED",
            "caller_completed": False,
            "cleanup_succeeded": bool(cleanup_ok),
            "terminal_reason": "missing_harness_marker" if ready else "missing_ready_marker",
        }
    if mode != "scripted" and not ready:
        return {
            "workflow_status": "FAIL",
            "experiment_verdict": "UNMEASURED",
            "caller_completed": False,
            "cleanup_succeeded": bool(cleanup_ok),
            "terminal_reason": "missing_ready_marker",
        }
    if not cleanup_ok:
        return {
            "workflow_status": "FAIL",
            "experiment_verdict": parser_verdict or "UNMEASURED",
            "caller_completed": False,
            "cleanup_succeeded": False,
            "terminal_reason": "cleanup_failed",
        }
    return {
        "workflow_status": "PASS",
        "experiment_verdict": parser_verdict,
        "caller_completed": True,
        "cleanup_succeeded": True,
        "terminal_reason": "post_reset_capture_parsed",
    }


def _failed(reason: str, cleanup_ok: bool, experiment: str = "UNMEASURED") -> dict:
    return {
        "workflow_status": "FAIL",
        "experiment_verdict": experiment,
        "caller_completed": False,
        "cleanup_succeeded": cleanup_ok,
        "terminal_reason": reason,
    }


class Adapter:
    def __init__(self):
        self.last_workflow = None
        self.bound_ports: list[int] = []

    def run(self, host, config, spec, args, *, mode: str) -> int:
        backend = getattr(args, "backend", None) or "managed-http"
        if backend != "managed-http":
            self.last_workflow = "FAIL"
            return 2
        requested_mode = getattr(args, "mode", None) or mode
        if requested_mode not in {"manual", "scripted"}:
            raise ProjectConfigError("LAB-15 mode must be manual or scripted")
        selected = args.variant or (
            requested_mode if requested_mode in spec.variants else spec.default_variant
        )
        if selected not in spec.variants:
            raise ProjectConfigError(f"unknown LAB-15 variant {selected}")
        seconds = args.seconds if args.seconds is not None else spec.capture_seconds_default
        parser = _parser_module(spec)

        def execute(context, evidence):
            cleanup_ok = True
            captured = False
            failure = ""
            parser_verdict = "UNMEASURED"
            post_reset = ""
            try:
                evidence.update_context({
                    "lab_revision": "v1",
                    "lab_version_string": (
                        "LAB-15/v1 = firmware <build-id> + fixture lab15-envelope/v1"
                    ),
                    "capture": {"mode": requested_mode, "variant": selected, "seconds": seconds,
                                "status": "not_started"},
                    "service": "not_configured",
                    "scenario": "not_configured",
                })
                target = host.resolve_run_target(config, getattr(args, "kit", "") or "",
                                                  getattr(args, "port", "") or "")
                evidence.update_context({"hardware": {"kit_nickname": target.kit_id}})
                evidence.update_context({"build": {"status": "running"}})
                if getattr(args, "no_build", False):
                    build_dir = host.ROOT / host.resolve_variant(spec, selected).build_dir
                else:
                    build_dir = host.ensure_lab_built(
                        config,
                        spec.make_name,
                        selected,
                        kit=target.kit_id,
                        port=target.device,
                        target=target,
                    )
                evidence.update_context({
                    "build": {"status": "passed"},
                    "firmware": {"project_path": str(spec.project_dir)},
                })
                flash = host._flash_from_env_and_args(args, spec, for_run=True)
                if flash:
                    port = host.revalidate_run_target(config, target)
                    host.flash_lab_build(config, build_dir, port)
                    time.sleep(1.0)
                serial_path = host.ROOT / "logs/device/labs/lab-15" / context.run_id / "serial.txt"
                evidence.update_context({
                    "artifacts": {"serial_log": serial_path.relative_to(host.ROOT).as_posix()},
                    "capture": {"status": "capturing"},
                })
                print(f"LAB-15 capture active for {seconds}s mode={requested_mode}", flush=True)
                post_reset = _capture_serial(target.device, serial_path, seconds, requested_mode, parser)
                captured = True
                evidence.update_context({"capture": {"status": "captured", "post_reset": True}})
                metrics_path = evidence.run_dir / "metrics.json"
                parsed = parser.parse_file(
                    serial_path,
                    output_path=metrics_path,
                    run_id=context.run_id,
                    mode=requested_mode,
                    capture_context={
                        "kind": "device_serial_capture",
                        "target_id": target.kit_id,
                        "post_reset": True,
                    },
                )
                parser_verdict = parsed.get("experiment_verdict", "UNMEASURED")
                evidence.update_context({
                    "artifacts": {"metrics": metrics_path.relative_to(host.ROOT).as_posix()},
                })
            except KeyboardInterrupt:
                failure = "KeyboardInterrupt"
            except Exception as exc:
                failure = type(exc).__name__
                cleanup_ok = False
            if not captured:
                result = _failed(failure or "capture_not_started", cleanup_ok)
            else:
                result = _classify_capture(
                    post_reset=post_reset,
                    parser_verdict=parser_verdict,
                    cleanup_ok=cleanup_ok,
                    mode=requested_mode,
                )
                if failure == "KeyboardInterrupt":
                    result = _failed("KeyboardInterrupt", cleanup_ok, parser_verdict)
                    result["workflow_status"] = "INCOMPLETE"
            self.last_workflow = result["workflow_status"]
            return result

        return run_with_adapter(
            repo_root=host.ROOT,
            lab_id=spec.lab_id,
            evidence_slug=spec.evidence_slug,
            variant=selected,
            mode=requested_mode,
            requested_run_id=getattr(args, "run_id", "") or "",
            hooks=RunHooks(lambda: None, lambda _target: None, lambda _target, _run: None),
            project_path=str(spec.project_dir),
            firmware_target=spec.target,
            capture_seconds=seconds,
            execute=execute,
        )
