"""LAB-16 host adapter: h22 diary server + SD-buffered upload harness."""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

import lab16_build
from lab_runner import RunHooks, run_with_adapter
from project_config import ProjectConfigError

ADAPTER_VERSION = "v1"
PORT = 8776
DEADLINES_SECONDS = {"startup": 15, "health": 10, "cleanup": 5}
HARNESS_COMPLETE = "LAB16_HARNESS_DONE status=complete"
READY_PREFIX = "LAB16_READY "
STREAM_COMPLETE = "LAB16_STREAM status=complete"
SERVER_DIR = Path("server/labs/lab-16-attached-storage")
BOOT_TIMEOUT_SECONDS = 60
MAX_SERIAL_SECONDS = 720
MAX_SERIAL_SECONDS_OVERLAP = 1200
MAX_SERIAL_BYTES = 8 * 1024 * 1024
VERIFY = Path("client/labs/lab-16-attached-storage/tools/verify_capture.py")
VERIFY_OVERLAP = Path("client/labs/lab-16-attached-storage/tools/verify_overlap_capture.py")
OVERLAP_VARIANT = "overlap-record-during-upload"
LIVE_FAILOVER_VARIANT = "live-stream-failover-during-record"
OVERLAP_CHUNKS_PER_SESSION = 180
LIVE_STREAM_CHUNKS = 180
LIVE_STREAM_MARKER = "LAB16_LIVE status=streaming"
VERIFY_LIVE = Path("client/labs/lab-16-attached-storage/tools/verify_live_stream_capture.py")


class OwnedServer:
    def __init__(self):
        self.proc = None
        self.log = None
        self.port = None
        self._drain = None

    def start(self, repo_root: Path, run_id: str, log_path: Path, device_token: str, port: int = PORT,
              bind: str = "0.0.0.0", expected_chunks: int | None = None) -> None:
        from server_log import BoundedRedactingLog

        source = repo_root / SERVER_DIR / "server.py"
        self.log = BoundedRedactingLog(log_path)
        cmd = [
            sys.executable,
            str(source),
            "--bind",
            bind,
            "--port",
            str(port),
            "--run-id",
            run_id,
            "--device-token",
            device_token,
        ]
        if expected_chunks is not None:
            cmd.extend(["--expected-chunks", str(expected_chunks)])
        self.proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
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
        raise RuntimeError("LAB-16 diary server did not become healthy")

    def stop(self) -> bool:
        proc = self.proc
        if proc is None:
            return True
        if proc.poll() is None:
            proc.terminate()
        try:
            proc.wait(timeout=DEADLINES_SECONDS["cleanup"])
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=DEADLINES_SECONDS["cleanup"])
        self.proc = None
        if self._drain is not None:
            self._drain.join(timeout=1)
            self._drain = None
        if self.log is not None:
            self.log.close()
            self.log = None
        return True


def server_set_accepting(port: int, accepting: bool) -> None:
    body = json.dumps({"accepting": accepting}).encode("utf-8")
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/v1/control",
        data=body,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=5) as response:
        if response.status != 200:
            raise RuntimeError(f"server control returned {response.status}")


def capture_serial(device: str, path: Path, seconds: int, server: OwnedServer | None, repo_root: Path,
                   run_id: str, log_path: Path, device_token: str,
                   delay_server_until_stream: bool, max_serial_seconds: int = MAX_SERIAL_SECONDS,
                   simulate_server_outage: bool = False) -> tuple[str, OwnedServer | None]:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required for LAB-16 capture") from exc
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    connection = None
    text_parts: list[str] = []
    total = 0
    server_started = server is not None and not delay_server_until_stream
    try:
        connection = serial.Serial(device, 115200, timeout=0.2)
        connection.dtr = False
        connection.rts = True
        time.sleep(0.12)
        connection.reset_input_buffer()
        connection.rts = False
        started = time.monotonic()
        ready_at = None
        harness = False
        cap_seconds = min(seconds, max_serial_seconds)
        outage_off_at = None
        outage_on_at = None
        outage_armed = False
        outage_server_down = False
        outage_server_up = False
        with path.open("w", encoding="utf-8", newline="\n") as log:
            while True:
                chunk = connection.read(2048)
                now = time.monotonic()
                if chunk:
                    if total + len(chunk) > MAX_SERIAL_BYTES:
                        raise RuntimeError("LAB-16 serial log exceeded byte bound")
                    piece = chunk.decode("utf-8", errors="replace")
                    log.write(piece)
                    log.flush()
                    total += len(chunk)
                    text_parts.append(piece)
                    text = "".join(text_parts)
                    if ready_at is None and READY_PREFIX in text:
                        ready_at = now
                    if delay_server_until_stream and not server_started and STREAM_COMPLETE in text:
                        server = OwnedServer()
                        server.start(repo_root, run_id, log_path, device_token, PORT, bind="0.0.0.0",
                                     expected_chunks=None)
                        server_started = True
                    if simulate_server_outage and server_started and LIVE_STREAM_MARKER in text and not outage_armed:
                        outage_off_at = now + 30.0
                        outage_on_at = now + 75.0
                        outage_armed = True
                    if (
                        outage_armed
                        and not outage_server_down
                        and outage_off_at is not None
                        and now >= outage_off_at
                    ):
                        server_set_accepting(PORT, False)
                        print("LAB-16 host server accepting=false (simulated outage)", flush=True)
                        outage_server_down = True
                    if (
                        outage_armed
                        and outage_server_down
                        and not outage_server_up
                        and outage_on_at is not None
                        and now >= outage_on_at
                    ):
                        server_set_accepting(PORT, True)
                        print("LAB-16 host server accepting=true (outage ended)", flush=True)
                        outage_server_up = True
                    if HARNESS_COMPLETE in text:
                        harness = True
                if ready_at is None and now >= started + BOOT_TIMEOUT_SECONDS:
                    break
                if ready_at is not None and harness:
                    break
                if ready_at is not None and now >= ready_at + cap_seconds:
                    break
        return "".join(text_parts), server if server_started else None
    finally:
        if connection is not None:
            connection.close()


def verify_server_session(port: int, device_token: str, session: str, need: int) -> list[str]:
    errors: list[str] = []
    try:
        req = urllib.request.Request(
            f"http://127.0.0.1:{port}/v1/diary",
            headers={"Authorization": f"Bearer {device_token}"},
        )
        with urllib.request.urlopen(req, timeout=10) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except Exception as exc:
        return [f"server diary GET failed: {exc}"]
    seqs: set[int] = set()
    for row in payload.get("chunks") or []:
        if str(row.get("session") or "") != session:
            continue
        try:
            seqs.add(int(row.get("seq")))
        except (TypeError, ValueError):
            continue
    if len(seqs) != need:
        errors.append(f"server session {session}: expected {need} chunks, got {len(seqs)}")
    missing = [i for i in range(need) if i not in seqs]
    if missing and len(missing) <= 5:
        errors.append(f"server session {session}: missing seq {missing}")
    elif missing:
        errors.append(f"server session {session}: missing {len(missing)} seq values")
    return errors


def verify_overlap_server(port: int, device_token: str) -> list[str]:
    errors: list[str] = []
    errors.extend(verify_server_session(port, device_token, "1", OVERLAP_CHUNKS_PER_SESSION))
    errors.extend(verify_server_session(port, device_token, "2", OVERLAP_CHUNKS_PER_SESSION))
    return errors


def classify_capture(post_reset: str, cleanup_ok: bool, repo_root: Path, serial_path: Path, *,
                     variant: str = "upload", server_errors: list[str] | None = None) -> dict:
    if variant == OVERLAP_VARIANT:
        verify_path = VERIFY_OVERLAP
    elif variant == LIVE_FAILOVER_VARIANT:
        verify_path = VERIFY_LIVE
    else:
        verify_path = VERIFY
    proc = subprocess.run(
        [sys.executable, str(repo_root / verify_path), str(serial_path)],
        capture_output=True,
        text=True,
        check=False,
    )
    experiment = "PASS" if proc.returncode == 0 else "UNMEASURED"
    ready = READY_PREFIX in (post_reset or "")
    harness = HARNESS_COMPLETE in (post_reset or "")
    server_errors = server_errors or []
    if not (ready and harness and proc.returncode == 0 and not server_errors):
        reason = proc.stderr.strip() or "verify_failed"
        if server_errors:
            reason = "; ".join(server_errors)
        return {
            "workflow_status": "FAIL",
            "experiment_verdict": experiment,
            "caller_completed": False,
            "cleanup_succeeded": cleanup_ok,
            "terminal_reason": reason,
        }
    if not cleanup_ok:
        return {
            "workflow_status": "FAIL",
            "experiment_verdict": experiment,
            "caller_completed": False,
            "cleanup_succeeded": False,
            "terminal_reason": "cleanup_failed",
        }
    return {
        "workflow_status": "PASS",
        "experiment_verdict": experiment,
        "caller_completed": True,
        "cleanup_succeeded": True,
        "terminal_reason": "harness_and_upload_verified",
    }


def server_identity(repo_root: Path) -> dict[str, str]:
    server_dir = repo_root / SERVER_DIR
    source = (server_dir / "server.py").read_bytes()
    manifest = json.loads((server_dir / "manifest.lab.json").read_text(encoding="utf-8"))
    fixture = (server_dir / "fixtures" / "device.json").read_bytes()
    return {
        "path": SERVER_DIR.as_posix(),
        "revision": manifest["lab_revision"],
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "fixture_sha256": hashlib.sha256(fixture).hexdigest(),
        "fixture_revision": manifest["fixture_revision"],
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
        selected = getattr(args, "variant", None) or spec.default_variant
        if selected not in spec.variants:
            raise ProjectConfigError(f"unknown LAB-16 variant: {selected}")
        seconds = args.seconds if getattr(args, "seconds", None) is not None else spec.capture_seconds_default
        delay_server = selected == "server-offline-during-record"
        overlap = selected == OVERLAP_VARIANT
        live_failover = selected == LIVE_FAILOVER_VARIANT
        if overlap:
            seconds = max(seconds, 900)
        if live_failover:
            seconds = max(seconds, 600)

        def execute(context, evidence):
            identity = server_identity(host.ROOT)
            server = OwnedServer()
            cleanup_ok = False
            captured = False
            failure = ""
            post_reset = ""
            try:
                target = host.resolve_run_target(config, getattr(args, "kit", "") or "",
                                                  getattr(args, "port", "") or "")
                device_token = lab16_build._token_for_kit(target.deployment, target.kit)
                log_path = evidence.run_dir / "server.log"
                serial_path = host.ROOT / "logs/device/labs/lab-16" / context.run_id / "serial.txt"
                if overlap:
                    expected_chunks = OVERLAP_CHUNKS_PER_SESSION * 2
                elif live_failover:
                    expected_chunks = LIVE_STREAM_CHUNKS
                else:
                    expected_chunks = None
                if not delay_server:
                    server.start(host.ROOT, context.run_id, log_path, device_token, PORT, bind="0.0.0.0",
                                 expected_chunks=expected_chunks)
                    self.bound_ports.append(PORT)
                evidence.update_context({"hardware": {"kit_nickname": target.kit_id}})
                build_dir = host.build_lab(config, spec.make_name, selected, kit=target.kit_id,
                                            port=target.device, target=target)
                flash = host._flash_from_env_and_args(args, spec, for_run=True)
                if flash:
                    port = host.revalidate_run_target(config, target)
                    host.flash_lab_build(config, build_dir, port)
                    time.sleep(1.0)
                print(f"LAB-16 capture up to {seconds}s variant={selected}", flush=True)
                if overlap:
                    cap_limit = MAX_SERIAL_SECONDS_OVERLAP
                elif live_failover:
                    cap_limit = 900
                else:
                    cap_limit = MAX_SERIAL_SECONDS
                post_reset, delayed = capture_serial(
                    target.device,
                    serial_path,
                    seconds,
                    server if not delay_server else None,
                    host.ROOT,
                    context.run_id,
                    log_path,
                    device_token,
                    delay_server,
                    cap_limit,
                    simulate_server_outage=live_failover,
                )
                if delayed is not None:
                    server = delayed
                captured = True
                server_errors: list[str] = []
                if server.proc is not None and server.proc.poll() is None:
                    if overlap:
                        server_errors = verify_overlap_server(PORT, device_token)
                    elif live_failover:
                        server_errors = verify_server_session(PORT, device_token, "1", LIVE_STREAM_CHUNKS)
                cleanup_ok = server.stop() if server.proc is not None else True
            except KeyboardInterrupt:
                failure = "KeyboardInterrupt"
            except Exception as exc:
                failure = type(exc).__name__
                if server.proc is not None:
                    cleanup_ok = server.stop()
            if not captured:
                result = {
                    "workflow_status": "FAIL",
                    "experiment_verdict": "UNMEASURED",
                    "caller_completed": False,
                    "cleanup_succeeded": cleanup_ok,
                    "terminal_reason": failure or "capture_not_started",
                }
            else:
                result = classify_capture(
                    post_reset,
                    cleanup_ok,
                    host.ROOT,
                    serial_path,
                    variant=selected,
                    server_errors=server_errors if (overlap or live_failover) else None,
                )
                if failure == "KeyboardInterrupt":
                    result["workflow_status"] = "INCOMPLETE"
            self.last_workflow = result["workflow_status"]
            return result

        return run_with_adapter(
            repo_root=host.ROOT,
            lab_id=spec.lab_id,
            evidence_slug=spec.evidence_slug,
            variant=selected,
            mode="scripted",
            requested_run_id=getattr(args, "run_id", "") or "",
            hooks=RunHooks(lambda: None, lambda _target: None, lambda _target, _run: None),
            project_path=str(spec.project_dir),
            firmware_target=spec.target,
            capture_seconds=seconds,
            execute=execute,
        )
