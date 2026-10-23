"""LAB-11 host lifecycle adapter. The adapter owns the fixture server child."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

from lab_runner import RunHooks, run_with_adapter
from project_config import ProjectConfigError

ADAPTER_VERSION = "v1"
PORT = 8770
DEADLINES_SECONDS = {
    "startup": 10,
    "health": 10,
    "seed": 10,
    "readiness": 15,
    "action": 5,
    "cleanup": 5,
}
HARNESS_COMPLETE = "LAB11_HARNESS_DONE status=complete"
READY_PREFIX = "LAB11_READY "
SERVER_DIR = Path("server/labs/lab-11-send-receipt")
BOOT_TIMEOUT_SECONDS = 45
RESET_HOLD_SECONDS = 0.12
MAX_SERIAL_BYTES = 4 * 1024 * 1024


def select_variant(spec, args, mode: str) -> str:
    """Unset variant plus scripted mode selects the scripted build when it exists."""
    requested = getattr(args, "variant", None)
    if requested:
        return requested
    variants = getattr(spec, "variants", {})
    if mode == "scripted" and "scripted" in variants:
        return "scripted"
    if mode in variants:
        return mode
    return spec.default_variant


def classify_capture(*, pre_reset: str, post_reset: str, parser_verdict: str,
                     cleanup_ok: bool, mode: str = "scripted") -> dict:
    """A ready or harness marker counts only in bytes captured after the armed reset."""
    del pre_reset
    text = post_reset or ""
    ready = READY_PREFIX in text and f"mode={mode} " in text
    harness = HARNESS_COMPLETE in text
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
            "experiment_verdict": parser_verdict if parser_verdict else "UNMEASURED",
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


def server_identity(repo_root: Path | None = None) -> dict[str, str]:
    root = repo_root or Path(__file__).resolve().parents[1]
    server_dir = root / SERVER_DIR
    source = (server_dir / "server.py").read_bytes()
    manifest = json.loads((server_dir / "manifest.lab.json").read_text(encoding="utf-8"))
    fixture = (server_dir / "fixtures" / "manifest.json").read_bytes()
    return {
        "path": SERVER_DIR.as_posix(),
        "revision": manifest["lab_revision"],
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "fixture_sha256": hashlib.sha256(fixture).hexdigest(),
        "fixture_revision": manifest["fixture_revision"],
        "fixture_schema": manifest["fixture_schema"],
    }


class OwnedServer:
    """Keep the fixture child owned until wait succeeds, with one cleanup retry."""

    def __init__(self):
        self.proc = None
        self.log = None
        self.port = None
        self._drain = None

    def start(self, repo_root: Path, run_id: str, log_path: Path, port: int = PORT,
              bind: str = "127.0.0.1") -> None:
        from server_log import BoundedRedactingLog

        source = repo_root / SERVER_DIR / "server.py"
        self.log = BoundedRedactingLog(log_path)
        self.proc = subprocess.Popen(
            [sys.executable, str(source), "--bind", bind, "--port", str(port), "--run-id", run_id],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        self.port = port

        def drain() -> None:
            stream = self.proc.stdout if self.proc is not None else None
            if stream is None:
                return
            while True:
                try:
                    chunk = stream.read(256)
                except (ValueError, OSError):
                    break
                if not chunk:
                    break
                if self.log is not None:
                    self.log.feed("stdout", chunk)

        self._drain = threading.Thread(target=drain, daemon=True)
        self._drain.start()
        deadline = time.monotonic() + DEADLINES_SECONDS["startup"]
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                break
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=0.5) as response:
                    payload = json.loads(response.read().decode("utf-8"))
                if payload.get("run_id") == run_id:
                    return
            except Exception:
                time.sleep(0.05)
        raise RuntimeError("LAB-11 fixture server did not become healthy")

    def stop(self, wait=None, kill=None) -> bool:
        proc = self.proc
        if proc is None:
            return True
        if getattr(proc, "poll", lambda: None)() is None and hasattr(proc, "terminate"):
            proc.terminate()
        waiter = wait or proc.wait
        killer = kill or proc.kill
        try:
            waiter(timeout=DEADLINES_SECONDS["cleanup"])
        except subprocess.TimeoutExpired:
            killer()
            try:
                waiter(timeout=DEADLINES_SECONDS["cleanup"])
            except subprocess.TimeoutExpired:
                return False
        self.proc = None
        if self._drain is not None:
            self._drain.join(timeout=1)
            self._drain = None
        stream = getattr(proc, "stdout", None)
        if stream is not None:
            stream.close()
        if self.log is not None:
            self.log.close()
            self.log = None
        return True


def _parser_module(spec, repo_root: Path):
    parser_path = repo_root / spec.trace_parser
    if not parser_path.is_file():
        raise ProjectConfigError("LAB-11 trace parser is not configured")
    name = "lab11_parse_lab_trace"
    module = sys.modules.get(name)
    if module is None:
        loaded = importlib.util.spec_from_file_location(name, parser_path)
        if loaded is None or loaded.loader is None:
            raise ProjectConfigError("LAB-11 trace parser could not be loaded")
        module = importlib.util.module_from_spec(loaded)
        sys.modules[name] = module
        loaded.loader.exec_module(module)
    return module


def capture_serial(device: str, path: Path, seconds: int, mode: str) -> str:
    """Reset the kit, then keep only bytes that arrive after that reset."""
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required for LAB-11 capture") from exc
    path.parent.mkdir(parents=True, exist_ok=False)
    connection = None
    text_parts: list[str] = []
    total = 0
    try:
        connection = serial.Serial(device, 115200, timeout=0.2)
        connection.dtr = False
        connection.rts = True
        time.sleep(RESET_HOLD_SECONDS)
        connection.reset_input_buffer()
        connection.rts = False
        started = time.monotonic()
        ready_at = None
        harness = False
        with path.open("w", encoding="utf-8", newline="\n") as log:
            while True:
                chunk = connection.read(2048)
                now = time.monotonic()
                if chunk:
                    if total + len(chunk) > MAX_SERIAL_BYTES:
                        raise RuntimeError("LAB-11 serial log exceeded byte bound")
                    piece = chunk.decode("utf-8", errors="replace")
                    log.write(piece)
                    log.flush()
                    total += len(chunk)
                    text_parts.append(piece)
                    text = "".join(text_parts)
                    if ready_at is None and READY_PREFIX in text and f"mode={mode} " in text:
                        ready_at = now
                    if HARNESS_COMPLETE in text:
                        harness = True
                if ready_at is None and now >= started + BOOT_TIMEOUT_SECONDS:
                    break
                if ready_at is not None and mode == "scripted" and harness:
                    break
                if ready_at is not None and now >= ready_at + seconds:
                    break
        return "".join(text_parts)
    finally:
        if connection is not None:
            connection.close()


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
            raise ProjectConfigError("LAB-11 mode must be manual or scripted")
        selected = select_variant(spec, args, requested_mode)
        if selected not in spec.variants:
            raise ProjectConfigError(f"unknown LAB-11 variant: {selected}")
        seconds = args.seconds if getattr(args, "seconds", None) is not None else spec.capture_seconds_default

        def execute(context, evidence):
            identity = server_identity(host.ROOT)
            server = OwnedServer()
            cleanup_ok = False
            captured = False
            failure = ""
            parser_verdict = "UNMEASURED"
            post_reset = ""
            try:
                evidence.update_context({
                    "lab_revision": "v1",
                    "lab_version_string": (
                        "LAB-11/v1 = firmware lab11-fw-scripted-v1 + server lab11-server/v1 + fixture lab11-send/v1"
                    ),
                    "server": {
                        "path": identity["path"],
                        "revision": identity["revision"],
                        "source_sha256": identity["source_sha256"],
                    },
                    "fixture": {
                        "schema": identity["fixture_schema"],
                        "revision": identity["fixture_revision"],
                        "sha256": identity["fixture_sha256"],
                    },
                    "capture": {"mode": requested_mode, "variant": selected, "seconds": seconds,
                                "status": "not_started"},
                })
                target = host.resolve_run_target(config, getattr(args, "kit", "") or "",
                                                  getattr(args, "port", "") or "")
                evidence.update_context({"hardware": {"kit_nickname": target.kit_id}})
                log_path = evidence.run_dir / "server.log"
                server.start(host.ROOT, context.run_id, log_path, PORT, bind="0.0.0.0")
                self.bound_ports.append(PORT)
                with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/health",
                                             timeout=DEADLINES_SECONDS["health"]) as response:
                    health = json.loads(response.read().decode("utf-8"))
                if health.get("run_id") != context.run_id or health.get("fixture_sha256") != identity["fixture_sha256"]:
                    raise RuntimeError("LAB-11 health identity does not match the run manifest")
                evidence.update_context({"artifacts": {"server_log": log_path.relative_to(host.ROOT).as_posix()}})
                evidence.update_context({"build": {"status": "running"}})
                build_dir = host.build_lab(config, spec.make_name, selected, kit=target.kit_id,
                                            port=target.device, target=target)
                evidence.update_context({"build": {"status": "passed"},
                                         "firmware": {"project_path": str(spec.project_dir)}})
                flash = host._flash_from_env_and_args(args, spec, for_run=True)
                if flash:
                    port = host.revalidate_run_target(config, target)
                    host.flash_lab_build(config, build_dir, port)
                    time.sleep(1.0)
                serial_path = host.ROOT / "logs/device/labs/lab-11" / context.run_id / "serial.txt"
                evidence.update_context({"artifacts": {"serial_log": serial_path.relative_to(host.ROOT).as_posix()},
                                         "capture": {"status": "capturing"}})
                print(f"LAB-11 capture active for {seconds}s mode={requested_mode}", flush=True)
                post_reset = capture_serial(target.device, serial_path, seconds, requested_mode)
                captured = True
                evidence.update_context({"capture": {"status": "captured", "post_reset": True}})
                parser = _parser_module(spec, host.ROOT)
                metrics_path = evidence.run_dir / "metrics.json"
                parsed = parser.parse_file(
                    serial_path, output_path=metrics_path, expected_run_id=context.run_id,
                    expected_mode=requested_mode, post_reset=True,
                    capture_context={"kind": "device_serial_capture", "target_id": target.kit_id,
                                     "post_reset": True})
                parser_verdict = parsed["lab_metrics"]["experiment_verdict"]
                evidence.update_context({
                    "artifacts": {"metrics": metrics_path.relative_to(host.ROOT).as_posix()},
                    "event_loss": {"firmware_trace": parsed["evidence"]["firmware_trace_loss"]},
                })
            except KeyboardInterrupt:
                failure = "KeyboardInterrupt"
            except Exception as exc:
                failure = type(exc).__name__
            finally:
                cleanup_ok = server.stop()
            if not captured:
                result = _failed(failure or "capture_not_started", cleanup_ok)
            else:
                result = classify_capture(pre_reset="", post_reset=post_reset,
                                          parser_verdict=parser_verdict, cleanup_ok=cleanup_ok,
                                          mode=requested_mode)
                if failure == "KeyboardInterrupt":
                    result = _failed("KeyboardInterrupt", cleanup_ok, parser_verdict)
                    result["workflow_status"] = "INCOMPLETE"
            self.last_workflow = result["workflow_status"]
            return result

        return run_with_adapter(
            repo_root=host.ROOT, lab_id=spec.lab_id, evidence_slug=spec.evidence_slug,
            variant=selected, mode=requested_mode, requested_run_id=getattr(args, "run_id", "") or "",
            hooks=RunHooks(lambda: None, lambda _target: None, lambda _target, _run: None),
            project_path=str(spec.project_dir), firmware_target=spec.target,
            capture_seconds=seconds, execute=execute,
        )
