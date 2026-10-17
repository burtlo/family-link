"""Single-owner managed HTTP/capture coordinator for canonical LAB-02 runs."""
from __future__ import annotations

import hashlib
import json
import queue
import os
import re
import subprocess
import sys
import threading
import time
import uuid
import yaml
from collections import deque
from datetime import datetime, timezone
from pathlib import Path

from lab02_evidence import EvidenceRun, sha256_file
from lab02_scenarios import Lab02ScenarioCoordinator, classify_scenario, parse_trace
from lab02_server import ManagedLab02Server, select_fixture
from lab02_workflow_config import load_lab02_workflow_config

ROOT = Path(__file__).resolve().parents[1]
READY = ("LAB02_READY", "LAB02_READY ")
MAX_SERIAL_LINE_BYTES = 4096
READY_REQUIRED_FIELDS = ("build", "fixture", "mode", "harness", "scenario")
READY_OPTIONAL_FIELDS = frozenset({
    "backend", "trace_schema", "draw_buf_height_lines", "draw_buf_count",
    "draw_buf_bytes", "color_depth_bits",
})


def _parse_ready_marker(line: str) -> dict[str, str] | None:
    """Parse a LAB02_READY line without making field order part of its contract."""
    tokens = line.split()
    if not tokens or tokens[0] != "LAB02_READY":
        return None
    fields: dict[str, str] = {}
    supported_fields = set(READY_REQUIRED_FIELDS) | READY_OPTIONAL_FIELDS
    for token in tokens[1:]:
        if "=" not in token:
            return None
        key, value = token.split("=", 1)
        if key not in supported_fields or not value or key in fields:
            return None
        fields[key] = value
    if any(key not in fields for key in READY_REQUIRED_FIELDS):
        return None
    if fields["harness"] not in {"0", "1"}:
        return None
    return fields


def _attribute_unowned_trace(line: str, run_id: str) -> str:
    """Attribute only the firmware's unset run-id sentinel to this capture."""
    return re.sub(r"(?<!\S)run_id=-(?=\s|$)", f"run_id={run_id}", line)


def _scenario_snap_has_motion(snap: dict) -> bool:
    """Avoid dispatching time-sensitive scenario actions during harness no-ops."""
    start_x, target_x = snap.get("start_x"), snap.get("target_x")
    return start_x is not None and target_x is not None and start_x != target_x


def _attach_metrics_run_id(metrics_path: Path, run_id: str) -> None:
    """Add canonical envelope identity without changing parser evidence conclusions."""
    metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
    if not isinstance(metrics, dict):
        raise ValueError("metrics_document_invalid")
    parsed_run_id = metrics.get("run_id")
    if parsed_run_id not in (None, "", run_id):
        raise ValueError("metrics_run_id_mismatch")
    metrics["run_id"] = run_id
    metrics_path.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")


def _build_identity(*, variant: str, fixture_size: int, scenario: str, mode: str,
                    backend: str, client_url: str, kit_id: str, run_id: str,
                    firmware_source_sha256: str, compiled_config_sha256: str = "") -> str:
    # This identity is embedded in firmware and guards fresh-boot readiness.
    # It must describe compiled inputs only: run IDs and kit nicknames are
    # execution context and must not invalidate an otherwise reusable build.
    payload = json.dumps({"variant": variant, "fixture": fixture_size, "scenario": scenario,
        "mode": mode, "backend": backend, "client_url": client_url,
        "firmware_source_sha256": firmware_source_sha256,
        "compiled_config_sha256": compiled_config_sha256},
        sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode()).hexdigest()


def _local_defaults(config) -> tuple[int, str]:
    try:
        document = yaml.safe_load(config.path_value("labs.registry").read_text(encoding="utf-8"))
        row = document["labs"]["lab02"]["workflow"]
        fixture, scenario = row["fixture_size_default"], row["scenario_default"]
        if fixture not in {8, 16, 64, 256} or not isinstance(scenario, str):
            raise ValueError
        return fixture, scenario
    except (OSError, KeyError, TypeError, ValueError, yaml.YAMLError):
        raise ValueError("invalid LAB-02 local experiment defaults") from None


def _hash_tree(path: Path) -> str:
    digest = hashlib.sha256()
    for file in sorted(p for p in path.rglob("*") if p.is_file() and "build" not in p.parts):
        digest.update(file.relative_to(path).as_posix().encode())
        try:
            digest.update(file.read_bytes())
        except OSError:
            continue
    return digest.hexdigest()


def _source_info(project: Path) -> dict:
    try:
        rev = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
                             capture_output=True, timeout=3, check=True).stdout.strip()
        dirty = bool(subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, text=True,
                                    capture_output=True, timeout=3, check=True).stdout.strip())
    except (OSError, subprocess.SubprocessError):
        rev, dirty = None, None
    return {"revision": rev, "dirty": dirty,
            "files": {"firmware_tree_sha256": _hash_tree(project),
                      "server_sha256": sha256_file(ROOT / "server/labs/lab-02-paging/server.py")}}


class _SerialRedactor:
    """Redact known per-kit Wi-Fi values even when UART reads split a secret."""
    def __init__(self, values: list[str]):
        self.secrets = sorted({value.encode("utf-8") for value in values if value}, key=len, reverse=True)
        self.pending = bytearray()
        self.hold = max((len(value) for value in self.secrets), default=1) - 1

    def feed(self, data: bytes, *, final: bool = False) -> bytes:
        self.pending.extend(data)
        raw = bytes(self.pending)
        cut = len(raw) if final else max(0, len(raw) - self.hold)
        if not final:
            for secret in self.secrets:
                start = 0
                while True:
                    start = raw.find(secret, start)
                    if start < 0:
                        break
                    if start < cut < start + len(secret):
                        cut = start
                    start += 1
                for size in range(1, min(len(secret), len(raw))):
                    if raw.endswith(secret[:size]):
                        cut = min(cut, len(raw) - size)
        prefix = bytes(self.pending[:cut])
        del self.pending[:cut]
        for secret in self.secrets:
            prefix = prefix.replace(secret, b"[REDACTED]")
        return prefix


def _redact_event(value, secrets: list[str]):
    if isinstance(value, dict):
        return {key: _redact_event(item, secrets) for key, item in value.items()}
    if isinstance(value, list):
        return [_redact_event(item, secrets) for item in value]
    if isinstance(value, str):
        for secret in secrets:
            if secret:
                value = value.replace(secret, "[REDACTED]")
    return value


def run_canonical(config, *, mode: str, kit: str = "", port: str = "", seconds: int | None = None,
                  flash: bool = True, run_id: str = "", no_build: bool = False,
                  backend: str = "managed-http",
                  build_variant: str = "phaseb",
                  target_resolver, target_revalidator, build, flash_build, parse,
                  evidence=None, outcome_sink=None):
    """Run LAB-02 end-to-end. The injected callbacks are the established lab_host adapters."""
    from lab02_workflow_config import Lab02WorkflowConfigError
    from lab02_server import ServerLifecycleError
    from project_config import ProjectConfigError

    rid = run_id or datetime.now(timezone.utc).strftime("%Y%m%d-") + uuid.uuid4().hex[:10]
    owned_evidence = evidence is None
    if evidence is None:
        evidence = EvidenceRun(ROOT, rid, {"lab_revision": "v1", "operator": "human" if mode == "manual" else "harness",
        "scenarios": ["nominal"], "hardware": {"kit_nickname": None},
        "build": {"status": "not_started"}, "capture": {"status": "not_started"}})
    evidence_path = evidence.run_dir
    log_dir: Path | None = None
    log_path: Path | None = None
    server = None
    coordinator = None
    conn = None
    reader_thread = None
    log_handle = None
    redactor_for_reader = None
    reset_complete = threading.Event()
    reader_armed = threading.Event()
    reader_stop = threading.Event()
    serial_chunks: queue.Queue[tuple[bytes, bool] | None] = queue.Queue(maxsize=64)
    workflow = "FAIL"
    experiment = "INCOMPLETE"
    reason = "preflight_failed"
    cleanup_ok = True
    completed = False
    build_started = False
    lines = deque(maxlen=10000)
    workflow_events: list[dict] = []
    action_futures = []
    known_secrets: list[str] = []
    def record_event(event: dict) -> None:
        if event.get("source") not in {"host", "server", "device", "unknown"}:
            event = {"source": "unknown", **event}
        event = _redact_event(event, known_secrets)
        if len(workflow_events) < 2000:
            workflow_events.append(event)
        evidence.event_sink(event)
    stages = {"preflight": "not_started", "service": "not_started", "build": "not_started",
              "activity": "not_started", "scenario": "not_started", "cleanup": "not_started"}
    def stage(name: str, outcome: str) -> None:
        stages[name] = {"running": "running", "passed": "passed", "failed": "failed"}.get(outcome, outcome)
        record_event({"source": "host", "kind": "stage", "stage": name, "outcome": outcome,
                      "monotonic_ns": time.monotonic_ns()})
    try:
        if no_build:
            raise ProjectConfigError("canonical run cannot prove a matching prior build")
        configured_logs = config.path_value("firmware.logs").resolve()
        expected_logs = (ROOT / "logs/device").resolve()
        if configured_logs != expected_logs:
            raise ProjectConfigError("unsafe_artifact_root")
        log_dir = configured_logs / "labs" / "lab-02" / rid
        log_path = log_dir / "serial.txt"
        spec = load_lab02_workflow_config(config) if backend == "managed-http" else None
        local_fixture_size, local_scenario = _local_defaults(config) if spec is None else (16, "nominal")
        stage("preflight", "running")
        selected = target_resolver(config, kit, port)
        stage("preflight", "passed")
        profile = selected.deployment.secrets_profile_for_kit(selected.kit)
        known_secrets = [profile.wifi_ssid, profile.wifi_password] if profile else []
        fixture_size = spec.fixture_size_default if spec else local_fixture_size
        fixture = select_fixture(fixture_size)
        server_log_path = evidence_path / "server.log" if spec else None
        server = (ManagedLab02Server(spec, run_id=rid,
                                     event_sink=lambda event: record_event({**event, "source": "server"}),
                                     log_path=server_log_path) if spec else None)
        scenario_name = spec.scenario_default if spec else local_scenario
        client_url = spec.client_url if spec else ""
        frozen_source_sha = _hash_tree(ROOT / "client/labs/lab-02-bounded-inbox")
        compiled_config_sha = hashlib.sha256(json.dumps(
            {"ssid": profile.wifi_ssid if profile else None,
             "password": profile.wifi_password if profile else None},
            sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        build_fingerprint = _build_identity(variant=build_variant, fixture_size=fixture_size,
            scenario=scenario_name, mode=mode, backend=backend, client_url=client_url,
            kit_id=selected.kit_id, run_id=rid, firmware_source_sha256=frozen_source_sha,
            compiled_config_sha256=compiled_config_sha)
        expected_build_id = build_fingerprint[:24]
        evidence.update_context({"source": _source_info(ROOT / "client/labs/lab-02-bounded-inbox"),
            "hardware": {"board": "esp32s3-box-3", "kit_nickname": selected.kit_id},
            "scenarios": [scenario_name], "fixture": {"schema": "inbox-summary/v1",
                "revision": "v1", "file": f"fixtures/inbox_{fixture.size}.json", "sha256": fixture.sha256},
            "server": {"path": "server/labs/lab-02-paging" if spec else "none", "revision": None,
                       "source_sha256": sha256_file(ROOT / "server/labs/lab-02-paging/server.py") if spec else None}})
        if server:
            stage("service", "running")
            server.start(fixture.size)
            stage("service", "passed")
        else:
            stage("service", "not_configured")
        build_dirs = {"phaseb": "build/labs/lab-02-bounded-inbox-phaseb",
                      "scripted-phaseb": "build/labs/lab-02-bounded-inbox-scripted-phaseb",
                      "recycle": "build/labs/lab-02-bounded-inbox",
                      "scripted": "build/labs/lab-02-bounded-inbox-scripted",
                      "eager": "build/labs/lab-02-bounded-inbox-eager"}
        build_dir = ROOT / build_dirs[build_variant]
        if not no_build:
            stage("build", "running")
            build_started = True
            build(config, "lab02", build_variant, kit=selected.kit_id, port=selected.device,
                target=selected, frozen_workflow=spec, frozen_fixture_size=fixture_size,
                frozen_scenario=scenario_name, frozen_build_id=expected_build_id)
            stage("build", "passed")
        build_dir = ROOT / build_dirs[build_variant]
        if not (build_dir / "flasher_args.json").is_file():
            raise ProjectConfigError("build_artifact_missing")
        bin_files = sorted(build_dir.glob("*.bin"))
        app_binary = next((path for path in bin_files if path.stem in {"lab02_bounded_inbox", "lab-02-bounded-inbox"}), None)
        evidence.update_context({"build": {"status": "built" if not no_build else "reused_with_artifact_proof"},
            "firmware": {"project_path": "client/labs/lab-02-bounded-inbox", "source_sha256": frozen_source_sha,
                "binary_sha256": sha256_file(app_binary) if app_binary else None,
                "sdkconfig_sha256": sha256_file(build_dir / "sdkconfig"),
                "build_stamp": None, "build_id": expected_build_id,
                "build_fingerprint": f"sha256:{build_fingerprint}", "idf_version": None,
                "cmake_options": {f"CONFIG_LAB02_FIXTURE_{fixture.size}": "y", "CONFIG_LAB_AUTO_HARNESS": "y" if mode == "scripted" else "n"}}})
        if flash:
            device = target_revalidator(config, selected)
            flash_build(config, build_dir, device)
        import serial
        conn = serial.Serial(selected.device, 115200, timeout=0.15)
        conn.dtr = False
        conn.rts = False
        conn.reset_input_buffer()
        capture_started = time.monotonic()
        log_dir.mkdir(parents=True, exist_ok=False)
        log_handle = log_path.open("wb")
        reader_errors: list[BaseException] = []
        def serial_reader() -> None:
            try:
                while not reader_stop.is_set():
                    # A chunk is eligible only when its read began after reset.
                    # Reads can block across the reset edge, so tagging on return
                    # would let bytes already in flight establish fresh readiness.
                    reset_was_complete = reset_complete.is_set()
                    chunk = conn.read(4096)
                    if chunk:
                        log_handle.write(redactor_for_reader.feed(chunk))
                        log_handle.flush()
                        try:
                            serial_chunks.put_nowait((chunk, reset_was_complete))
                        except queue.Full:
                            reader_errors.append(BufferError("serial consumer queue limit exceeded"))
                            break
                    reader_armed.set()
            except BaseException as exc:
                reader_errors.append(exc)
            finally:
                try:
                    serial_chunks.put_nowait(None)
                except queue.Full:
                    pass
        redactor_for_reader = _SerialRedactor(known_secrets)
        reader_thread = threading.Thread(target=serial_reader, name=f"lab02-uart-{rid}", daemon=True)
        reader_thread.start()
        coordinator = Lab02ScenarioCoordinator(server, capture_active=lambda: True, event_sink=record_event) if server else None
        evidence.update_context({"capture": {"status": "armed", "reset": "RTS pulse after capture arm"}})
        if build_variant == "eager":
            command = "make lab.lab02 BACKEND=local VARIANT=eager"
        elif mode == "scripted":
            command = "make lab.lab02.scripted" + (" BACKEND=local" if backend == "local" else "")
        else:
            command = "make lab.lab02" + (" BACKEND=local" if backend == "local" else "")
        stage("activity", "running")
        scenario_required = bool(spec and spec.scenario_default in {"S-push", "S-rm-focus", "S-page-snap"})
        stage("scenario", "running" if scenario_required else "not_configured")
        print(f"CAPTURE ACTIVE — command={command}; backend={backend}; mode={mode}; fixture={fixture.size}; scenario={scenario_name}; kit={selected.kit_id}; remaining={seconds or 120}s")
        if mode == "manual":
            print("Manual mode: traverse freely, including loading cards.")
        else:
            print("Scripted mode: no operator touch; waiting for HARNESS_DONE.")
        if not reader_armed.wait(2):
            raise TimeoutError("serial_reader_arm_timeout")
        conn.rts = True
        time.sleep(0.12)
        conn.rts = False
        reset_complete.set()
        record_event({"source": "host", "kind": "reset", "method": "serial_rts_pulse",
                      "utc": datetime.now(timezone.utc).isoformat()})
        readiness_seconds = spec.deadlines_seconds["readiness"] if spec else 15
        ready_deadline = time.monotonic() + readiness_seconds
        fresh_ready = False
        marker_mismatch = False
        harness_done = False
        harness_failed = False
        scenario_dispatched = False
        duration = seconds or 120
        end_after_ready = None
        draining_after_completion = False
        try:
            pending_bytes = bytearray()
            while (draining_after_completion or
                   time.monotonic() - capture_started < duration + readiness_seconds):
                try:
                    chunk = serial_chunks.get_nowait() if draining_after_completion else serial_chunks.get(timeout=0.05)
                except queue.Empty:
                    if draining_after_completion:
                        break
                    if not fresh_ready and time.monotonic() > ready_deadline:
                        reason = "fresh_boot_marker_timeout"
                        workflow = "INCOMPLETE"
                        break
                    if fresh_ready and end_after_ready and time.monotonic() >= end_after_ready:
                        completed = True
                        break
                    continue
                if chunk is None:
                    if reader_errors:
                        workflow, experiment, reason = "INCOMPLETE", "INCOMPLETE", "serial_reader_incomplete"
                    break
                chunk, eligible_for_readiness = chunk
                if not eligible_for_readiness:
                    # Pre-reset bytes remain in serial.txt for diagnosis, but cannot
                    # contribute to the post-reset readiness parser.
                    continue
                pending_bytes.extend(chunk)
                first_newline = pending_bytes.find(b"\n")
                if ((first_newline < 0 and len(pending_bytes) > MAX_SERIAL_LINE_BYTES) or
                    (first_newline > MAX_SERIAL_LINE_BYTES)):
                    record_event({"kind": "serial_line", "outcome": "incomplete", "reason": "line_limit_exceeded"})
                    workflow, experiment, reason = "INCOMPLETE", "INCOMPLETE", "serial_line_limit_exceeded"
                    break
                complete_lines = []
                while b"\n" in pending_bytes:
                    raw_line, _, remainder = pending_bytes.partition(b"\n")
                    pending_bytes = bytearray(remainder)
                    complete_lines.append(raw_line.decode("utf-8", "replace").rstrip("\r"))
                stop_after_chunk = False
                for line in complete_lines:
                    lines.append(line)
                    if coordinator:
                        coordinator.ingest_line(_attribute_unowned_trace(line, rid))
                    if not fresh_ready and (line.startswith("LAB02_READY ") or line.strip() == "LAB02_READY"):
                        match = _parse_ready_marker(line)
                        expected_mode = "http" if backend == "managed-http" else "eager" if build_variant == "eager" else "recycle"
                        expected_harness = "1" if mode == "scripted" else "0"
                        if (not match or match["build"] != expected_build_id or
                            match["fixture"] != f"inbox_{fixture.size}" or match["mode"] != expected_mode or
                            match["harness"] != expected_harness or match["scenario"] != scenario_name):
                            marker_mismatch = True
                            reason = "fresh_boot_identity_mismatch"
                            workflow = "INCOMPLETE"
                            record_event({"source": "host", "kind": "boot_ready", "outcome": "mismatch"})
                            break
                        fresh_ready = True
                        end_after_ready = time.monotonic() + duration
                        evidence.update_context({"firmware": {"build_stamp": match["build"]}})
                        record_event({"source": "host", "kind": "boot_ready", "outcome": "success", "firmware_build_id": match["build"],
                                      "fixture": match["fixture"], "mode": match["mode"]})
                    if "HARNESS_DONE" in line:
                        expected_steps = min(fixture.size, 20) * 2
                        done = re.search(r"\bHARNESS_DONE\s+build=(\S+)\s+steps=(\d+)\s+scenario=(\S+)", line)
                        harness_done = bool(done and done.group(1) == expected_build_id and
                            int(done.group(2)) == expected_steps and done.group(3) == scenario_name and fresh_ready)
                        harness_failed = not harness_done
                        if harness_done:
                            # Finish parsing every complete line in this already-read
                            # chunk, then stop the reader and drain queued chunks.
                            stop_after_chunk = True
                    if coordinator and spec and not scenario_dispatched and spec.scenario_default in {"S-push", "S-rm-focus", "S-page-snap"} and "event=snap_start" in line:
                        parsed = parse_trace((line,))
                        if parsed and _scenario_snap_has_motion(parsed[0]):
                            snap = parsed[0]
                            action_name = {"S-push": "push_item", "S-rm-focus": "remove_message", "S-page-snap": "delay_ms"}[spec.scenario_default]
                            data = {"item": {"sender_name": "LAB-02", "preview": "Managed push"}} if action_name == "push_item" else {"ms": 5000} if action_name == "delay_ms" else {}
                            try:
                                action_futures.append(coordinator.dispatch_action(spec.scenario_default, action_name, data,
                                    viewed_message_id=snap.get("message_id")))
                                scenario_dispatched = True
                            except ServerLifecycleError as exc:
                                record_event({"kind": "scenario_action", "proof_status": "INCOMPLETE", "reason": str(exc)})
                                scenario_dispatched = True
                if marker_mismatch:
                    break
                if mode == "scripted" and stop_after_chunk:
                    completed = True
                    # Stop producing chunks before draining. The bounded reader
                    # timeout lets join finish promptly; queued chunks are then
                    # consumed exactly once through this same parser path.
                    reader_stop.set()
                    if reader_thread:
                        reader_thread.join(timeout=2)
                    draining_after_completion = True
                    continue
                if mode == "manual" and fresh_ready and end_after_ready and time.monotonic() >= end_after_ready:
                    completed = True
                    break
        finally:
            reader_stop.set()
            if reader_thread:
                reader_thread.join(timeout=2)
            if log_handle and redactor_for_reader:
                log_handle.write(redactor_for_reader.feed(b"", final=True))
                log_handle.close()
        if fresh_ready and completed and flash and not no_build:
            workflow = "PASS"
            evidence.update_context({"capture": {"status": "complete", "fresh_boot_marker": True,
                "harness_done": harness_done, "duration_seconds": duration}})
            experiment = "UNMEASURED" if scenario_name == "nominal" else "INCOMPLETE"
            if action_futures:
                receipts = []
                action_failed = False
                for future in action_futures:
                    try:
                        _, receipt = future.result(timeout=(spec.deadlines_seconds["action"] if spec else 5) + 1)
                        receipts.append(receipt)
                    except Exception:
                        action_failed = True
                if action_failed:
                    workflow, reason = "FAIL", "scenario_action_failed"
                device_events = [row for row in workflow_events if row.get("kind") == "device_trace"]
                effects = [row for row in workflow_events if row.get("kind") == "scenario_effect"]
                verdicts = [classify_scenario(spec.scenario_default, device_events + effects,
                    action_receipt=receipt, expected_message_id=receipt.get("viewed_message_id"))
                    for receipt in receipts]
                experiment = "PASS" if verdicts and all(v == "PASS" for v in verdicts) else (
                    "FAIL" if "FAIL" in verdicts else "INCOMPLETE")
            stage("activity", "passed")
            if scenario_required:
                stage("scenario", "passed" if experiment in {"PASS", "FAIL"} else "incomplete")
        else:
            workflow = "INCOMPLETE"
            if not flash:
                reason = "flash_skipped_image_identity_unproven"
            evidence.update_context({"capture": {"status": "incomplete", "fresh_boot_marker": fresh_ready,
                "harness_done": harness_done}})
        if reason == "serial_line_limit_exceeded":
            workflow, experiment = "INCOMPLETE", "INCOMPLETE"
        elif mode == "scripted" and harness_failed:
            workflow, reason = "FAIL", "harness_failed"
        elif mode == "scripted" and not harness_done:
            workflow, reason = "INCOMPLETE", "harness_done_missing"
        if completed:
            reason = "completed" if workflow == "PASS" else reason
        if reader_errors:
            reader_error = reader_errors[0]
            reader_detail = str(reader_error).strip() or type(reader_error).__name__
            record_event({"source": "host", "kind": "serial_reader", "outcome": "incomplete",
                          "error_type": type(reader_error).__name__, "reason": reader_detail})
            workflow, experiment = "INCOMPLETE", "INCOMPLETE"
            reason = f"serial_reader_incomplete:{reader_detail}"
            evidence.update_context({"capture": {"status": "incomplete", "reader_error": reader_detail}})
    except KeyboardInterrupt:
        workflow, experiment, reason = "INCOMPLETE", "INCOMPLETE", "interrupted"
    except (ProjectConfigError, OSError, subprocess.SubprocessError, ServerLifecycleError, Lab02WorkflowConfigError) as exc:
        workflow = "INCOMPLETE" if reason == "fresh_boot_marker_timeout" else "FAIL"
        reason = str(exc) if str(exc).isidentifier() else type(exc).__name__
        if build_started:
            evidence.update_context({"build": {"status": "failed"}})
    except Exception as exc:
        workflow, experiment, reason = "FAIL", "INCOMPLETE", type(exc).__name__
        if build_started:
            evidence.update_context({"build": {"status": "failed"}})
    finally:
        reader_stop.set()
        reset_complete.set()
        if reader_thread:
            reader_thread.join(timeout=2)
        if log_handle and not log_handle.closed:
            if redactor_for_reader:
                log_handle.write(redactor_for_reader.feed(b"", final=True))
            log_handle.close()
        if coordinator:
            try: coordinator.close(timeout=5)
            except Exception: cleanup_ok = False
        if conn:
            try: conn.close()
            except Exception: cleanup_ok = False
        if server:
            try: server.close()
            except Exception: cleanup_ok = False
            if server_log_path is not None and server_log_path.is_file():
                try:
                    artifact_path = server_log_path.resolve().relative_to(ROOT.resolve()).as_posix()
                except (OSError, ValueError):
                    artifact_path = None
                if artifact_path:
                    evidence.update_context({"artifacts": {"server_log": artifact_path}})
        if log_path is not None and log_path.is_file():
            evidence.update_context({"artifacts": {"device_log": f"logs/device/labs/lab-02/{rid}/serial.txt"}})
            try:
                metrics = evidence_path / "metrics.json"
                parse(log_path, metrics)
                _attach_metrics_run_id(metrics, rid)
                parsed_metrics = json.loads(metrics.read_text(encoding="utf-8"))
                firmware_trace_loss = parsed_metrics.get("trace_loss")
                if not isinstance(firmware_trace_loss, dict):
                    firmware_trace_loss = {"status": "UNKNOWN", "dropped_events": None}
                evidence.update_context({"event_loss": {"firmware_trace": firmware_trace_loss}})
                evidence.update_context({"artifacts": {"metrics": f"docs/labs/evidence/lab-02/runs/{rid}/metrics.json"}})
            except Exception:
                workflow = "FAIL"
                reason = "trace_parse_failed"
        if not cleanup_ok:
            workflow, experiment, reason = "FAIL", "INCOMPLETE", "cleanup_failed"
        stage("cleanup", "passed" if cleanup_ok else "failed")
        if owned_evidence:
            try:
                evidence.finalize(workflow_status=workflow, experiment_verdict=experiment,
                    caller_completed=completed, cleanup_succeeded=cleanup_ok, terminal_reason=reason)
            except OSError:
                evidence.finalize(workflow_status=workflow, experiment_verdict=experiment,
                    caller_completed=completed, cleanup_succeeded=cleanup_ok, terminal_reason=reason)
        if outcome_sink:
            outcome_sink({"workflow_status": workflow, "experiment_verdict": experiment,
                          "caller_completed": completed, "cleanup_succeeded": cleanup_ok,
                          "terminal_reason": reason, "stages": stages})
    print(f"LAB-02 workflow_status={workflow} experiment_verdict={experiment} run_id={rid}")
    return 0 if workflow == "PASS" else 1
