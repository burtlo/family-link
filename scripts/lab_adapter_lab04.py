"""LAB-04 host lifecycle adapter: reset-gated capture and strict trace evidence."""
from __future__ import annotations

import importlib.util
import hashlib
import json
import re
import sys
import time
from pathlib import Path

from lab_runner import RunHooks, run_with_adapter
from project_config import ProjectConfigError

ADAPTER_VERSION = "v1"
MAX_CAPTURE_SECONDS = 900
MAX_SERIAL_BYTES = 4 * 1024 * 1024
MAX_LINE_BYTES = 4096
BOOT_TIMEOUT_SECONDS = 20
HARNESS_TIMEOUT_SECONDS = 90
RESET_HOLD_SECONDS = 0.1
DEADLINES_SECONDS = {
    "preflight": 30,
    "build": 600,
    "boot": BOOT_TIMEOUT_SECONDS,
    "manual_capture": MAX_CAPTURE_SECONDS,
    "harness": HARNESS_TIMEOUT_SECONDS,
    "parse": 20,
    "cleanup": 5,
}


def _parser_module(spec):
    parser_path = spec.trace_parser
    if parser_path is None or not parser_path.is_file():
        raise ProjectConfigError("LAB-04 trace parser is not configured")
    name = "lab04_parse_lab_trace"
    module = sys.modules.get(name)
    if module is None:
        spec_module = importlib.util.spec_from_file_location(name, parser_path)
        if spec_module is None or spec_module.loader is None:
            raise ProjectConfigError("could not load LAB-04 trace parser")
        module = importlib.util.module_from_spec(spec_module)
        sys.modules[name] = module
        spec_module.loader.exec_module(module)
    return module


def _safe_line(raw: bytes, parser) -> bytes:
    try:
        line = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise RuntimeError("serial emitted non-UTF8 data") from exc
    if len(raw) > MAX_LINE_BYTES:
        raise RuntimeError("serial line exceeded byte bound")
    parser.validate_evidence_line(line)
    return (parser.redact_line(line).rstrip("\r") + "\n").encode("utf-8")


def _cleanup() -> None:
    """LAB-04 owns no server; this lifecycle hook remains an explicit gate."""


def _tree_sha256(root: Path, *, fixtures: bool) -> str:
    """Hash authored project inputs with stable relative-path framing."""
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root)
        parts = rel.parts
        if any(part in {"build", "managed_components", ".git", "__pycache__"} for part in parts):
            continue
        in_fixtures = bool(parts and parts[0] == "fixtures")
        if in_fixtures != fixtures:
            continue
        if not fixtures and (rel.as_posix() == "sdkconfig" or path.suffix in {".pyc", ".o", ".a"}):
            continue
        digest.update(rel.as_posix().encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def _built_identity(project_dir: Path, build_dir: Path, variant: str) -> dict[str, str]:
    sdkconfig = build_dir / "sdkconfig"
    binary = build_dir / "lab04_sketch_format.bin"
    if not sdkconfig.is_file() or not binary.is_file():
        raise RuntimeError("LAB-04 build identity artifacts are missing")
    config = sdkconfig.read_text(encoding="utf-8", errors="strict")
    found = re.findall(r'^CONFIG_LAB04_BUILD_ID="([^"\r\n]+)"$', config, flags=re.MULTILINE)
    if len(found) != 1:
        raise RuntimeError("LAB-04 sdkconfig has missing or duplicate build ID")
    build_id = found[0]
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", build_id):
        raise RuntimeError("LAB-04 sdkconfig build ID is outside the marker bound")
    scripted = "CONFIG_LAB04_SCRIPTED=y" in config.splitlines()
    if scripted != (variant == "scripted"):
        raise RuntimeError("LAB-04 sdkconfig scripted configuration mismatches selected variant")
    source_hash = _tree_sha256(project_dir, fixtures=False)
    fixture_hash = _tree_sha256(project_dir, fixtures=True)
    sdkconfig_hash = hashlib.sha256(sdkconfig.read_bytes()).hexdigest()
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    fingerprint_input = {"source_sha256": source_hash, "fixture_sha256": fixture_hash,
                         "sdkconfig_sha256": sdkconfig_hash, "binary_sha256": binary_hash,
                         "variant": variant, "build_id": build_id}
    fingerprint = hashlib.sha256(json.dumps(fingerprint_input, sort_keys=True,
                                            separators=(",", ":")).encode("utf-8")).hexdigest()
    return {"build_id": build_id, "source_sha256": source_hash, "fixture_sha256": fixture_hash,
            "sdkconfig_sha256": sdkconfig_hash, "binary_sha256": binary_hash,
            "build_fingerprint": "sha256:" + fingerprint}


def _capture(host, target, run_id: str, seconds: int, mode: str, variant: str, parser) -> Path:
    if not 1 <= seconds <= MAX_CAPTURE_SECONDS:
        raise ValueError(f"LAB-04 capture seconds must be 1..{MAX_CAPTURE_SECONDS}")
    try:
        import serial
    except ImportError as exc:  # pragma: no cover - depends on host installation
        raise RuntimeError("pyserial is required for LAB-04 capture") from exc

    path = host.ROOT / "logs/device/labs/lab-04" / run_id / "serial.txt"
    path.parent.mkdir(parents=True, exist_ok=False)
    connection = None
    total = 0
    ready_count = harness_count = 0
    ready_at = None
    started_at = time.monotonic()
    boot_deadline = started_at + BOOT_TIMEOUT_SECONDS
    capture_end = None
    pending = b""
    failed_marker = False
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
                        raise RuntimeError("LAB-04 serial log exceeded byte bound")
                    pending += chunk
                    pieces = pending.split(b"\n")
                    pending = pieces.pop()
                    if len(pending) > MAX_LINE_BYTES:
                        raise RuntimeError("LAB-04 serial line exceeded byte bound")
                    for piece in pieces:
                        safe = _safe_line(piece, parser)
                        if total + len(safe) > MAX_SERIAL_BYTES:
                            raise RuntimeError("LAB-04 serial log exceeded byte bound")
                        log.write(safe)
                        total += len(safe)
                        text = safe.decode("utf-8").strip()
                        if text.startswith("LAB04_INIT_FAILED") or "LAB04_HARNESS_DONE status=failed" in text:
                            failed_marker = True
                        if text.startswith("LAB04_READY "):
                            ready_count += 1
                            if ready_count == 1:
                                ready_at = time.monotonic()
                                if f"mode={mode}" not in text or "server=none" not in text or "trace_schema=lab04-trace/v1" not in text:
                                    failed_marker = True
                                capture_end = ready_at + seconds if mode == "manual" else ready_at + HARNESS_TIMEOUT_SECONDS
                        if text.startswith("LAB04_HARNESS_DONE "):
                            harness_count += 1
                now = time.monotonic()
                if ready_count > 1 or harness_count > 1:
                    failed_marker = True
                    break
                if mode == "scripted" and harness_count == 1:
                    break
                if ready_at is None and now >= boot_deadline:
                    break
                if mode == "manual" and capture_end is not None and now >= capture_end:
                    break
                if mode == "scripted" and capture_end is not None and now >= capture_end:
                    break
            if pending:
                safe = _safe_line(pending, parser)
                if total + len(safe) > MAX_SERIAL_BYTES:
                    raise RuntimeError("LAB-04 serial log exceeded byte bound")
                log.write(safe)
                total += len(safe)
            log.flush()
            if ready_count != 1:
                raise RuntimeError("LAB-04_READY missing or duplicated after armed reset")
            if failed_marker:
                raise RuntimeError("LAB-04 emitted failed or mismatched lifecycle marker")
            if mode == "scripted" and harness_count != 1:
                raise RuntimeError("LAB04_HARNESS_DONE missing after scripted deadline")
    finally:
        if connection is not None:
            connection.close()
    return path


class Adapter:
    def run(self, host, config, spec, args, *, mode: str) -> int:
        backend = getattr(args, "backend", "managed-http")
        if backend not in {None, "", "managed-http"}:
            raise ProjectConfigError("LAB-04 supports no BACKEND value; reject before I/O")
        requested_mode = getattr(args, "mode", None) or mode
        if requested_mode not in {"manual", "scripted"}:
            raise ProjectConfigError("LAB-04 mode must be manual or scripted")
        selected = args.variant or (requested_mode if requested_mode in spec.variants else spec.default_variant)
        if selected not in spec.variants:
            raise ProjectConfigError(f"unknown LAB-04 variant: {selected}")
        if selected != requested_mode:
            raise ProjectConfigError("LAB-04 variant/phase mismatch")
        seconds = args.seconds if args.seconds is not None else spec.capture_seconds_default
        if not 1 <= seconds <= MAX_CAPTURE_SECONDS:
            raise ProjectConfigError(f"LAB-04 capture seconds must be 1..{MAX_CAPTURE_SECONDS}")
        flash = host._flash_from_env_and_args(args, spec, for_run=True)
        parser = _parser_module(spec)

        def execute(context, evidence):
            stages = {"preflight": "not_started", "service": "not_configured", "build": "not_started",
                      "activity": "not_started", "parser": "not_started", "scenario": "not_configured",
                      "cleanup": "not_started"}
            cleanup_ok = False
            completed = False
            workflow = "FAIL"
            reason = "run_failed"
            target = None
            serial_path = None
            metrics_path = evidence.run_dir / "metrics.json"

            def event(outcome: str, stage: str, **fields):
                evidence.event_sink({"source": "host", "kind": "stage", "stage": stage,
                                     "outcome": outcome, "adapter_identity": "lab04",
                                     "adapter_version": ADAPTER_VERSION, **fields})

            try:
                evidence.update_context({
                    "firmware": {"project_path": str(spec.project_dir), "target": spec.target},
                    "server": {"path": "none"},
                    "capture": {"mode": requested_mode, "variant": selected, "seconds": seconds},
                    "scenarios": ["F-malformed", "F-overflow", "F-cancel"],
                    "source": {"revision": "lab04-host/v1"},
                })
                event("running", "preflight")
                target = host.resolve_run_target(config, args.kit, args.port)
                stages["preflight"] = "passed"
                evidence.update_context({"hardware": {"kit_nickname": target.kit_id}})
                event("passed", "preflight")

                event("running", "build")
                if getattr(args, "no_build", False):
                    raise RuntimeError("LAB-04 run identity requires a fresh registered build")
                source_before = _tree_sha256(spec.project_dir, fixtures=False)
                fixtures_before = _tree_sha256(spec.project_dir, fixtures=True)
                build_dir = host.ROOT / host.resolve_variant(spec, selected).build_dir
                build_result = host.build_lab(config, spec.make_name, selected,
                                              kit=target.kit_id, port=target.device, target=target)
                build_identity = _built_identity(spec.project_dir, build_dir, selected)
                if (source_before != build_identity["source_sha256"]
                        or fixtures_before != build_identity["fixture_sha256"]):
                    raise RuntimeError("LAB-04 source or fixtures changed during build")
                stages["build"] = "passed"
                evidence.update_context({
                    "build": {"status": "passed"},
                    "firmware": {"build_id": build_identity["build_id"],
                                 "build_fingerprint": build_identity["build_fingerprint"],
                                 "source_sha256": build_identity["source_sha256"],
                                 "sdkconfig_sha256": build_identity["sdkconfig_sha256"],
                                 "binary_sha256": build_identity["binary_sha256"],
                                 "fixture_sha256": build_identity["fixture_sha256"]},
                    "fixture": {"revision": "lab04-sketch-format/v1",
                                "sha256": build_identity["fixture_sha256"]},
                })
                event("passed", "build", result=str(build_result))
                if flash:
                    port = host.revalidate_run_target(config, target)
                    host.flash_lab_build(config, build_result, port)
                    time.sleep(1.0)

                serial_path = host.ROOT / "logs/device/labs/lab-04" / context.run_id / "serial.txt"
                evidence.update_context({"artifacts": {"serial_log": serial_path.relative_to(host.ROOT).as_posix()}})
                event("running", "activity", mode=requested_mode)
                serial_path = _capture(host, target, context.run_id, seconds, requested_mode, selected, parser)
                stages["activity"] = "passed"
                evidence.update_context({"capture": {"status": "captured", "post_reset": True},
                                         "artifacts": {"serial_log": serial_path.relative_to(host.ROOT).as_posix()}})
                event("passed", "activity", mode=requested_mode)

                event("running", "parser")
                parsed = parser.parse_file(serial_path, output_path=metrics_path,
                                           expected_run_id=context.run_id, expected_mode=requested_mode,
                                           expected_variant=selected, post_reset=True,
                                           capture_context={"kind": "device_serial_capture",
                                                            "target_id": target.kit_id,
                                                            "post_reset": True},
                                           expected_build_id=build_identity["build_id"])
                stages["parser"] = "passed"
                evidence.update_context({"artifacts": {"metrics": metrics_path.relative_to(host.ROOT).as_posix()},
                                         "capture": {"parser_status": "parsed", "parser_identity": "lab04-trace/v1"},
                                         "event_loss": {
                                             "firmware_trace": parsed["evidence"]["firmware_trace_loss"],
                                             "firmware_publication_queue": parsed["evidence"]["firmware_publication_queue"]}})
                experiment = parsed["lab_metrics"]["experiment_verdict"]
                completed = True
                workflow = "PASS"
                reason = "post_reset_capture_parsed"
            except KeyboardInterrupt:
                workflow, reason = "INCOMPLETE", "KeyboardInterrupt"
            except Exception as exc:
                workflow, reason = "FAIL", type(exc).__name__
                try:
                    event("failed", "run", reason=reason)
                except Exception:
                    pass
            finally:
                # LAB-04 owns no server, but still executes and records cleanup.
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
                    reason = "cleanup_failed"
                if not cleanup_ok:
                    workflow = "FAIL"
                    completed = False
                evidence.update_context({"capture": {"stages": stages}})
            return {"workflow_status": workflow, "experiment_verdict": locals().get("experiment", "UNMEASURED"),
                    "caller_completed": completed, "cleanup_succeeded": cleanup_ok,
                    "terminal_reason": reason, "stages": stages}

        return run_with_adapter(repo_root=host.ROOT, lab_id=spec.lab_id,
                                evidence_slug=spec.evidence_slug, variant=selected,
                                mode=requested_mode, requested_run_id=args.run_id,
                                hooks=RunHooks(lambda: None, lambda _t: None, lambda _t, _r: None),
                                project_path=str(spec.project_dir), firmware_target=spec.target,
                                capture_seconds=seconds, execute=execute)
