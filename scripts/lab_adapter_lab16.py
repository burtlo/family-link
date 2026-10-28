"""LAB-16 host adapter: h22 diary server + SD-buffered upload harness."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import re
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
UPLOAD_FIXTURE = Path("server/labs/lab-16-attached-storage/fixtures/upload/fixture.json")
UPLOAD_PARSER = Path("client/labs/lab-16-attached-storage/tools/parse_upload_evidence.py")
UPLOAD_VARIANT = "upload"
SERVER_OFFLINE_VARIANT = "server-offline-during-record"
MEMORY_CAPTURE_VARIANTS = frozenset({
    UPLOAD_VARIANT,
    SERVER_OFFLINE_VARIANT,
    OVERLAP_VARIANT,
    LIVE_FAILOVER_VARIANT,
})
UPLOAD_CHUNK_COUNT = 210


class OwnedServer:
    def __init__(self):
        self.proc = None
        self.log = None
        self.port = None
        self._drain = None

    def start(self, repo_root: Path, run_id: str, log_path: Path, device_token: str, port: int = PORT,
              bind: str = "0.0.0.0", expected_chunks: int | None = None,
              fixture_file: Path | None = None) -> None:
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
        if fixture_file is not None:
            cmd.extend(["--fixture-file", str(fixture_file)])
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
                        server.start(
                            repo_root,
                            run_id,
                            log_path,
                            device_token,
                            PORT,
                            bind="0.0.0.0",
                            expected_chunks=UPLOAD_CHUNK_COUNT,
                            fixture_file=repo_root / UPLOAD_FIXTURE,
                        )
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


def _experiment_verdict_from_upload_metrics(metrics: dict) -> str:
    if metrics.get("memory_status") != "COMPLETE":
        return "UNMEASURED"
    functional = metrics.get("functional_status")
    if functional == "PASS":
        return "PASS"
    if functional == "FAIL":
        return "FAIL"
    return "UNMEASURED"


def classify_upload_capture(post_reset: str, cleanup_ok: bool, serial_path: Path, upload_metrics: dict,
                            server_errors: list[str] | None = None,
                            capture_variant: str = UPLOAD_VARIANT) -> dict:
    """Memory-capture variants: parser metrics are authoritative; serial verify is advisory."""
    from device_guardrails.lab16_serial import verify_lab16_upload_capture

    def _serial_verify_errors(text: str) -> list[str]:
        if capture_variant == OVERLAP_VARIANT:
            repo_root = Path(__file__).resolve().parents[1]
            proc = subprocess.run(
                [sys.executable, str(repo_root / VERIFY_OVERLAP), str(serial_path)],
                capture_output=True,
                text=True,
                check=False,
            )
            if proc.returncode == 0:
                return []
            return [line for line in (proc.stderr or proc.stdout or "verify_failed").splitlines() if line.strip()]
        return verify_lab16_upload_capture(text)

    experiment = _experiment_verdict_from_upload_metrics(upload_metrics)
    ready = READY_PREFIX in (post_reset or "")
    harness = HARNESS_COMPLETE in (post_reset or "")
    server_errors = server_errors or []
    functional = upload_metrics.get("functional_status")
    memory = upload_metrics.get("memory_status")
    device_finished = ready and harness and functional == "PASS"

    def _failure(workflow: str, reason: str, *, completed: bool = False) -> dict:
        return {
            "workflow_status": workflow,
            "experiment_verdict": experiment,
            "caller_completed": completed,
            "cleanup_succeeded": cleanup_ok,
            "terminal_reason": reason,
        }

    if server_errors:
        return _failure("FAIL", "; ".join(server_errors))
    if not cleanup_ok:
        return _failure("FAIL", "cleanup_failed")
    if not device_finished:
        serial_text = serial_path.read_text(encoding="utf-8", errors="replace")
        verify_errors = _serial_verify_errors(serial_text)
        if functional == "FAIL":
            reason = "; ".join(upload_metrics.get("functional_verification_errors")
                               or upload_metrics.get("functional_incomplete_reasons")
                               or verify_errors) or "upload_functional_failed"
            return _failure("FAIL", reason)
        if memory != "COMPLETE":
            reason = "; ".join(upload_metrics.get("incomplete_reasons") or []) or "memory_evidence_incomplete"
            return _failure("INCOMPLETE", reason)
        reason = "; ".join(verify_errors) or "upload_capture_incomplete"
        return _failure("FAIL", reason)
    if memory != "COMPLETE":
        reason = "; ".join(upload_metrics.get("incomplete_reasons") or []) or "memory_evidence_incomplete"
        return _failure("INCOMPLETE", reason, completed=True)
    return {
        "workflow_status": "PASS",
        "experiment_verdict": experiment,
        "caller_completed": True,
        "cleanup_succeeded": True,
        "terminal_reason": "harness_and_upload_verified",
    }


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


def server_identity(repo_root: Path, variant: str = "upload") -> dict[str, str]:
    server_dir = repo_root / SERVER_DIR
    source = (server_dir / "server.py").read_bytes()
    manifest = json.loads((server_dir / "manifest.lab.json").read_text(encoding="utf-8"))
    fixture_path = repo_root / (
        UPLOAD_FIXTURE if variant in (UPLOAD_VARIANT, SERVER_OFFLINE_VARIANT) else SERVER_DIR / "fixtures" / "device.json"
    )
    fixture = fixture_path.read_bytes()
    fixture_data = json.loads(fixture.decode("utf-8"))
    return {
        "path": SERVER_DIR.as_posix(),
        "revision": manifest["lab_revision"],
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "fixture_sha256": hashlib.sha256(fixture).hexdigest(),
        "fixture_revision": fixture_data.get("fixture_revision", manifest["fixture_revision"]),
        "fixture_path": fixture_path.relative_to(repo_root).as_posix(),
        "fixture_schema": fixture_data.get("fixture_schema", "lab16-diary-fixture-v1"),
        "fixture_id": fixture_data.get("fixture_id", fixture_data.get("device_id", "unknown")),
        "generator_version": fixture_data.get("generator_version", "existing-static-fixture"),
        "seed": fixture_data.get("seed", "not-recorded"),
    }


def upload_build_source_identity(repo_root: Path, build_dir: Path) -> dict[str, str]:
    """Hash the selected project inputs and the source/header inputs of this build."""
    repo_root = repo_root.resolve()
    build_dir = build_dir.resolve()
    project = repo_root / "client/labs/lab-16-attached-storage"
    cmake_cache = build_dir / "CMakeCache.txt"
    if not cmake_cache.is_file():
        raise RuntimeError("LAB-16 upload build is missing CMakeCache.txt")

    cache_values: dict[str, str] = {}
    for line in cmake_cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache_values[key.split(":", 1)[0]] = value
    toolchain_file = Path(cache_values.get("CMAKE_TOOLCHAIN_FILE", ""))
    idf_root = toolchain_file.parents[2] if len(toolchain_file.parents) > 2 else None
    compiler = Path(cache_values.get("CMAKE_C_COMPILER", ""))
    toolchain_root = compiler.parents[3] if len(compiler.parents) > 3 else None

    input_paths: set[Path] = {
        project / "CMakeLists.txt",
        project / "dependencies.lock",
        project / "manifest.lab.json",
        project / "partitions.csv",
        project / "sdkconfig.defaults",
        project / "sdkconfig.defaults.upload",
        project / "sdkconfig.defaults.server-offline",
        project / "sdkconfig.defaults.overlap",
        project / "sdkconfig.defaults.livestream",
    }
    build_tool_keys = (
        "CMAKE_COMMAND", "CMAKE_MAKE_PROGRAM", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
        "CMAKE_ASM_COMPILER", "CMAKE_LINKER", "CMAKE_AR", "CMAKE_RANLIB", "CMAKE_OBJCOPY", "PYTHON",
    )
    for key in build_tool_keys:
        tool_path = Path(cache_values.get(key, ""))
        if tool_path.is_file():
            input_paths.add(tool_path.resolve(strict=True))
    code_suffixes = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".S", ".s", ".inc", ".inl"}
    for source_dir in (project / "main", project / "components"):
        if not source_dir.is_dir():
            raise RuntimeError(f"LAB-16 upload build source directory is missing: {source_dir}")
        for path in source_dir.rglob("*"):
            if path.is_file() and (path.suffix in code_suffixes or path.name == "CMakeLists.txt"
                                   or path.name == "idf_component.yml" or path.name.startswith("Kconfig")):
                input_paths.add(path)
    managed_components = project / "managed_components"
    if managed_components.is_dir():
        for path in managed_components.rglob("*"):
            if path.is_file() and (path.name == "CMakeLists.txt" or path.name == "idf_component.yml"
                                   or path.suffix == ".cmake" or path.name.startswith("Kconfig")):
                input_paths.add(path)

    compile_commands = build_dir / "compile_commands.json"
    build_ninja = build_dir / "build.ninja"
    for path in (cmake_cache, build_ninja, compile_commands, build_dir / "sdkconfig",
                 build_dir / "config/sdkconfig.h", build_dir / "bootloader/bootloader.bin",
                 build_dir / "partition_table/partition-table.bin"):
        if path.is_file():
            input_paths.add(path)
    if not compile_commands.is_file() or not build_ninja.is_file():
        raise RuntimeError("LAB-16 upload build is missing compile_commands.json or build.ninja")

    commands = json.loads(compile_commands.read_text(encoding="utf-8"))
    if not isinstance(commands, list) or not commands:
        raise RuntimeError("LAB-16 upload compile command database is empty")
    compiled_sources: set[str] = set()
    compiled_source_paths: list[Path] = []
    for command in commands:
        source = Path(command["file"]).resolve(strict=True)
        input_paths.add(source)
        compiled_source_paths.append(source)
        compiled_sources.add(str(source).casefold())
        for token in re.findall(r"[A-Za-z]:[/\\][^\s\"']+\.exe", command.get("command", "")):
            executable = Path(token)
            if executable.is_file():
                input_paths.add(executable.resolve(strict=True))

    if toolchain_file.is_file():
        input_paths.add(toolchain_file.resolve(strict=True))
    if idf_root is not None:
        idf_cmake = idf_root / "tools/cmake"
        if idf_cmake.is_dir():
            for path in idf_cmake.rglob("*"):
                if path.is_file() and (path.name == "CMakeLists.txt" or path.suffix == ".cmake"):
                    input_paths.add(path)
        used_idf_components: set[Path] = set()
        for source in compiled_source_paths:
            try:
                relative = source.relative_to(idf_root / "components")
            except ValueError:
                continue
            component_root = idf_root / "components" / relative.parts[0]
            used_idf_components.add(component_root)
        for component_root in used_idf_components:
            for path in component_root.rglob("*"):
                if path.is_file() and (path.name == "CMakeLists.txt" or path.name == "idf_component.yml"
                                       or path.suffix == ".cmake" or path.name.startswith("Kconfig")):
                    input_paths.add(path)

    ninja = Path(cache_values.get("CMAKE_MAKE_PROGRAM", ""))
    if not ninja.is_file():
        raise RuntimeError("LAB-16 upload build does not identify its Ninja executable")
    dependency_result = subprocess.run(
        [str(ninja), "-C", str(build_dir), "-t", "deps"],
        cwd=repo_root, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90,
        check=False,
    )
    if dependency_result.returncode != 0:
        raise RuntimeError("could not enumerate LAB-16 upload compiler dependencies")
    dependency_sources: set[str] = set()
    dependency_paths: dict[str, str] = {}
    for line in dependency_result.stdout.splitlines():
        if not line.startswith("    "):
            continue
        raw_path = line.strip()
        dependency_paths.setdefault(raw_path.replace("\\", "/").casefold(), raw_path)
    if toolchain_root is None:
        for raw_path in dependency_paths.values():
            normalized = raw_path.replace("\\", "/")
            marker = "xtensa-esp-elf/"
            index = normalized.casefold().find(marker)
            if index >= 0:
                candidate = Path(normalized[:index + len(marker) - 1])
                if candidate.is_dir():
                    toolchain_root = candidate
                    break
    for raw_path in dependency_paths.values():
        dependency = Path(raw_path).resolve(strict=True)
        input_paths.add(dependency)
        dependency_sources.add(str(dependency).casefold())
    missing_dependencies = compiled_sources - dependency_sources
    if missing_dependencies:
        raise RuntimeError(f"Ninja dependency database omitted {len(missing_dependencies)} compiled source files")

    def input_key(path: Path) -> str:
        resolved = path.resolve(strict=True)
        for base, label in ((repo_root, ""), (idf_root, "external/esp-idf/"),
                            (toolchain_root, "external/xtensa-toolchain/")):
            if base is None:
                continue
            try:
                relative = resolved.relative_to(base.resolve())
            except ValueError:
                continue
            return label + relative.as_posix()
        parent_key = hashlib.sha256(str(resolved.parent).casefold().encode("utf-8")).hexdigest()[:16]
        return f"external/unresolved-{parent_key}/{resolved.name}"

    files: dict[str, str] = {}
    for path in sorted(input_paths, key=lambda item: str(item).casefold()):
        files[input_key(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    digest = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
    return {"sha256": digest, "files": files}


def upload_server_rows(port: int, device_token: str) -> list[dict]:
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=5) as response:
        health = json.loads(response.read().decode("utf-8"))
    if not health.get("run_id"):
        raise RuntimeError("server health response omitted run ID")
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/v1/diary",
        headers={"Authorization": f"Bearer {device_token}"},
    )
    with urllib.request.urlopen(req, timeout=15) as response:
        payload = json.loads(response.read().decode("utf-8"))
    rows = payload.get("chunks")
    if not isinstance(rows, list):
        raise RuntimeError("server diary response did not contain a chunk list")
    return rows


def capture_upload_evidence(repo_root: Path, evidence, serial_path: Path, server: OwnedServer,
                            device_token: str, identity: dict[str, str], run_id: str, *,
                            capture_variant: str = UPLOAD_VARIANT,
                            partial_capture: bool = False, verify_server: bool = True) -> tuple[dict, list[str]]:
    parser_spec = importlib.util.spec_from_file_location("lab16_upload_evidence_parser", repo_root / UPLOAD_PARSER)
    if parser_spec is None or parser_spec.loader is None:
        raise RuntimeError("could not load LAB-16 upload evidence parser")
    parser_module = importlib.util.module_from_spec(parser_spec)
    parser_spec.loader.exec_module(parser_module)
    errors: list[str] = []
    try:
        serial_text = serial_path.read_text(encoding="utf-8", errors="replace")
        parse = parser_module.parse_serial_partial if partial_capture else parser_module.parse_serial
        metrics = parse(serial_text, variant=capture_variant)
    except Exception as exc:
        errors.append(f"upload serial evidence parse failed: {type(exc).__name__}: {exc}")
        metrics = {
            "document": "family-link.lab-metrics/v2",
            "parser": "lab16-upload-evidence-parser/v3",
            "variant": capture_variant,
            "memory_status": "INCOMPLETE",
            "functional_status": "UNMEASURED",
            "incomplete_reasons": errors.copy(),
            "required_beats": ["baseline_before_sd", "sd_mount_probe", "mic_i2s_open_start",
                               "mic_i2s_open_complete", "sd_mic_recording", "wifi_upload_active",
                               "upload_complete_workers_quiescent"],
            "observed_beats": [],
            "memory_samples": [],
            "beat_windows": {},
            "device_events": [],
        }
    rows: list[dict] = []
    if partial_capture:
        metrics["memory_status"] = "INCOMPLETE"
        metrics.setdefault("incomplete_reasons", []).append("capture_interrupted_before_normal_completion")
        metrics["functional_status"] = "UNMEASURED"
        errors.append("server verification skipped because capture did not return normally")
    elif not verify_server:
        errors.append("server verification was not requested")
    elif server.proc is None or server.proc.poll() is not None:
        errors.append("run-owned diary server is not alive for final verification")
    else:
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/health", timeout=5) as response:
                health = json.loads(response.read().decode("utf-8"))
            if health.get("run_id") != run_id:
                raise RuntimeError("health run ID does not match this evidence run")
            if capture_variant not in (UPLOAD_VARIANT, SERVER_OFFLINE_VARIANT):
                rows = upload_server_rows(PORT, device_token)
            else:
                if health.get("fixture_sha256") != identity["fixture_sha256"]:
                    raise RuntimeError("server fixture hash does not match versioned upload fixture")
                rows = upload_server_rows(PORT, device_token)
                errors.extend(parser_module.verify_server_rows(metrics, rows))
        except Exception as exc:
            errors.append(f"run-owned server evidence inspection failed: {type(exc).__name__}: {exc}")
    if errors:
        metrics["functional_verification_errors"] = list(errors)
        if metrics.get("functional_status") != "FAIL":
            metrics["functional_status"] = "UNMEASURED"
            metrics.setdefault("functional_incomplete_reasons", []).append("server_verification_incomplete")
    metrics["run_id"] = run_id
    metrics["fixture"] = {
        "id": identity["fixture_id"],
        "revision": identity["fixture_revision"],
        "schema": identity["fixture_schema"],
        "path": identity["fixture_path"],
        "sha256": identity["fixture_sha256"],
        "generator_version": identity["generator_version"],
        "seed": identity["seed"],
    }
    metrics["server_verification_errors"] = errors
    metrics["server_chunks"] = sorted(rows, key=lambda row: (str(row.get("session")), int(row.get("seq", -1))))
    metrics["server_chunk_count"] = len(rows)
    metrics_path = evidence.run_dir / "metrics.json"
    metrics_sha256 = None
    write_metrics = not partial_capture or bool(metrics.get("memory_samples"))
    if write_metrics:
        metrics_path.write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8",
                                newline="\n")
        metrics_sha256 = hashlib.sha256(metrics_path.read_bytes()).hexdigest()

    for row in metrics.get("memory_samples", []):
        evidence.event_sink({"source": "device", "kind": "memory_window", "monotonic_ns": row["t_end_us"] * 1000,
                             **{key: value for key, value in row.items() if key not in {"schema", "event"}}})
    for row in metrics.get("device_events", []):
        evidence.event_sink({"source": "device", "kind": str(row.get("event", "event")),
                             "monotonic_ns": int(row.get("t_us", 0)) * 1000,
                             **{key: value for key, value in row.items() if key not in {"schema", "event", "t_us"}}})
    for row in rows:
        evidence.event_sink({"source": "server", "kind": "diary_chunk_accepted",
                             "monotonic_ns": time.monotonic_ns(), **row})
    evidence.update_context({
        "fixture": {"file": identity["fixture_path"], "revision": identity["fixture_revision"],
                    "schema": identity["fixture_schema"], "sha256": identity["fixture_sha256"],
                    "id": identity["fixture_id"], "generator_version": identity["generator_version"],
                    "seed": identity["seed"]},
        "server": {"path": identity["path"], "revision": identity["revision"],
                   "source_sha256": identity["source_sha256"], "fixture_sha256": identity["fixture_sha256"],
                   "fixture_revision": identity["fixture_revision"]},
        "artifacts": {
            "serial_log": serial_path.resolve().relative_to(evidence.repo_root).as_posix(),
            "server_log": (evidence.run_dir / "server.log").resolve().relative_to(evidence.repo_root).as_posix(),
            **({"metrics": metrics_path.resolve().relative_to(evidence.repo_root).as_posix()}
               if write_metrics else {}),
        },
        "capture": {"status": "incomplete" if partial_capture else "complete",
                    **({"metrics_sha256": metrics_sha256} if metrics_sha256 else {})},
        "event_loss": {"firmware_trace": {"status": "COMPLETE" if metrics.get("memory_status") == "COMPLETE"
                                             else "INCOMPLETE",
                                            "dropped_events": 0}},
    })
    notes_path = evidence.run_dir / "notes.md"
    notes_path.write_text(
        f"# LAB-16 memory run {run_id}\n\n"
        f"Variant: `{capture_variant}`\n\nFixture: `{identity['fixture_path']}` ({identity['fixture_revision']}, "
        f"SHA-256 `{identity['fixture_sha256']}`; generator `{identity['generator_version']}`, "
        f"seed `{identity['seed']}`)\n\n"
        f"Memory status: {metrics.get('memory_status', 'INCOMPLETE')}\n\n"
        f"Functional status: {metrics.get('functional_status', 'UNMEASURED')}\n\n"
        + (f"Metrics SHA-256: `{metrics_sha256}`\n\n" if metrics_sha256 else
           "Metrics artifact: not written because no memory samples were parsed.\n\n")
        + ("Server verification errors: " + "; ".join(errors) + "\n" if errors else "Server hash verification passed.\n"),
        encoding="utf-8",
        newline="\n",
    )
    return metrics, errors


def capture_serial_with_partial_upload_evidence(*, capture_args: tuple, capture_kwargs: dict,
                                                selected_variant: str, repo_root: Path, evidence,
                                                serial_path: Path, server: OwnedServer,
                                                device_token: str, identity: dict[str, str],
                                                run_id: str):
    """Keep a valid metrics prefix if a serial read or capture is interrupted."""
    try:
        return capture_serial(*capture_args, **capture_kwargs)
    except (Exception, KeyboardInterrupt):
        if selected_variant in MEMORY_CAPTURE_VARIANTS and serial_path.is_file():
            try:
                capture_upload_evidence(repo_root, evidence, serial_path, server, device_token, identity, run_id,
                                        capture_variant=selected_variant,
                                        partial_capture=True, verify_server=False)
            except Exception as exc:
                evidence.event_sink({"source": "host", "kind": "partial_metrics_preservation",
                                     "monotonic_ns": time.monotonic_ns(), "outcome": "failed",
                                     "error_type": type(exc).__name__})
        raise


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
            seconds = max(seconds, 1200)
        if live_failover:
            seconds = max(seconds, 1200)

        def execute(context, evidence):
            identity = server_identity(host.ROOT, selected)
            server = OwnedServer()
            cleanup_ok = False
            captured = False
            capture_started = False
            capture_interrupted = False
            failure = ""
            post_reset = ""
            upload_metrics = None
            upload_evidence_errors: list[str] = []
            server_errors: list[str] = []
            try:
                target = host.resolve_run_target(config, getattr(args, "kit", "") or "",
                                                  getattr(args, "port", "") or "")
                device_token = lab16_build._token_for_kit(target.deployment, target.kit)
                resolved_host_ip = lab16_build.client_address()
                host_ip_method = "override" if __import__("os").environ.get("LAB16_CLIENT_ADDRESS") else "route_discovery"
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
                                 expected_chunks=expected_chunks,
                                 fixture_file=(host.ROOT / identity["fixture_path"]
                                               if selected in (UPLOAD_VARIANT, SERVER_OFFLINE_VARIANT) else None))
                    self.bound_ports.append(PORT)
                evidence.update_context({
                    "hardware": {"kit_nickname": target.kit_id},
                    "fixture": {"file": identity["fixture_path"], "revision": identity["fixture_revision"],
                                "schema": identity["fixture_schema"], "sha256": identity["fixture_sha256"],
                                "id": identity["fixture_id"], "generator_version": identity["generator_version"],
                                "seed": identity["seed"]},
                    "server": {"path": identity["path"], "revision": identity["revision"],
                               "source_sha256": identity["source_sha256"],
                               "fixture_sha256": identity["fixture_sha256"],
                               "fixture_revision": identity["fixture_revision"]},
                    "capture": {"resolved_host_ip": resolved_host_ip, "host_ip_resolution": host_ip_method,
                                "server_port": PORT, "server_process_run_id": context.run_id},
                })
                build_dir = host.build_lab(config, spec.make_name, selected, kit=target.kit_id,
                                            port=target.device, target=target)
                sdkconfig_path = Path(build_dir) / "sdkconfig"
                built_host_ip = ""
                if sdkconfig_path.is_file():
                    for line in sdkconfig_path.read_text(encoding="utf-8", errors="replace").splitlines():
                        if line.startswith("CONFIG_LAB16_SERVER_HOST="):
                            built_host_ip = line.split("=", 1)[1].strip().strip('"')
                            break
                if built_host_ip != resolved_host_ip:
                    raise RuntimeError("LAB-16 build endpoint differs from recorded host-IP resolution")
                firmware_files = [
                    Path("client/labs/lab-16-attached-storage/main/app_main.c"),
                    Path("client/labs/lab-16-attached-storage/main/lab16_upload.c"),
                    Path("client/labs/lab-16-attached-storage/main/lab16_upload.h"),
                    Path("client/labs/lab-16-attached-storage/main/Kconfig.projbuild"),
                    Path("client/labs/lab-16-attached-storage/main/CMakeLists.txt"),
                    Path("client/labs/lab-16-attached-storage/sdkconfig.defaults"),
                    Path("client/labs/lab-16-attached-storage/sdkconfig.defaults.upload"),
                    Path("client/labs/lab-16-attached-storage/sdkconfig.defaults.server-offline"),
                    Path("client/labs/lab-16-attached-storage/sdkconfig.defaults.overlap"),
                    Path("client/labs/lab-16-attached-storage/sdkconfig.defaults.livestream"),
                    Path("client/labs/lab-16-attached-storage/manifest.lab.json"),
                    Path("config/labs.registry.yaml"),
                    Path("scripts/lab_adapter_lab16.py"),
                    Path("scripts/lab16_build.py"),
                    UPLOAD_PARSER,
                    UPLOAD_FIXTURE,
                    Path("server/labs/lab-16-attached-storage/server.py"),
                    Path("server/labs/lab-16-attached-storage/manifest.lab.json"),
                ]
                firmware_hashes = {path.as_posix(): hashlib.sha256((host.ROOT / path).read_bytes()).hexdigest()
                                   for path in firmware_files}
                sdkconfig_sha = hashlib.sha256(sdkconfig_path.read_bytes()).hexdigest() if sdkconfig_path.is_file() else None
                binary_path = Path(build_dir) / "lab16_attached_storage.bin"
                binary_sha = hashlib.sha256(binary_path.read_bytes()).hexdigest() if binary_path.is_file() else None
                build_id = None
                if sdkconfig_path.is_file():
                    for line in sdkconfig_path.read_text(encoding="utf-8", errors="replace").splitlines():
                        if line.startswith("CONFIG_LAB16_BUILD_ID="):
                            build_id = line.split("=", 1)[1].strip().strip('"')
                            break
                if selected in MEMORY_CAPTURE_VARIANTS:
                    build_inputs = upload_build_source_identity(host.ROOT, Path(build_dir))
                    firmware_hashes.update(build_inputs["files"])
                    source_sha = hashlib.sha256(
                        json.dumps(firmware_hashes, sort_keys=True, separators=(",", ":")).encode("utf-8")
                    ).hexdigest()
                else:
                    build_inputs = None
                    source_sha = hashlib.sha256("".join(firmware_hashes.values()).encode("ascii")).hexdigest()
                evidence.update_context({
                    "firmware": {"build_id": build_id if selected in MEMORY_CAPTURE_VARIANTS else None,
                                 "project_path": str(spec.project_dir), "source_sha256": source_sha,
                                 "sdkconfig_sha256": sdkconfig_sha, "binary_sha256": binary_sha},
                    "source": {"identity_schema": "lab16-build-inputs/v2" if build_inputs else "lab16-source/v1",
                               "build_input_count": len(build_inputs["files"]) if build_inputs else None,
                               "files": firmware_hashes},
                    "build": {"status": "passed", "directory": Path(build_dir).relative_to(host.ROOT).as_posix(),
                              "resolved_host_ip": built_host_ip, "host_ip_resolution": host_ip_method},
                })
                flash = host._flash_from_env_and_args(args, spec, for_run=True)
                if flash:
                    port = host.revalidate_run_target(config, target)
                    host.flash_lab_build(config, build_dir, port)
                    time.sleep(1.0)
                print(f"LAB-16 capture up to {seconds}s variant={selected}", flush=True)
                if overlap or live_failover:
                    cap_limit = MAX_SERIAL_SECONDS_OVERLAP
                else:
                    cap_limit = MAX_SERIAL_SECONDS
                evidence.update_context({
                    "capture": {"status": "capturing"},
                    "artifacts": {"serial_log": serial_path.relative_to(host.ROOT).as_posix(),
                                  "server_log": log_path.relative_to(host.ROOT).as_posix()},
                })
                capture_started = True
                post_reset, delayed = capture_serial_with_partial_upload_evidence(
                    capture_args=(target.device, serial_path, seconds, server if not delay_server else None,
                                  host.ROOT, context.run_id, log_path, device_token, delay_server, cap_limit),
                    capture_kwargs={"simulate_server_outage": live_failover},
                    selected_variant=selected, repo_root=host.ROOT, evidence=evidence,
                    serial_path=serial_path, server=server, device_token=device_token,
                    identity=identity, run_id=context.run_id,
                )
                capture_started = False
                if delayed is not None:
                    server = delayed
                captured = True
                if selected in MEMORY_CAPTURE_VARIANTS:
                    upload_metrics, upload_evidence_errors = capture_upload_evidence(
                        host.ROOT, evidence, serial_path, server, device_token, identity, context.run_id,
                        capture_variant=selected)
                    server_errors = list(upload_evidence_errors)
                if server.proc is not None and server.proc.poll() is None:
                    if overlap:
                        server_errors = verify_overlap_server(PORT, device_token)
                    elif live_failover:
                        server_errors = verify_server_session(PORT, device_token, "1", LIVE_STREAM_CHUNKS)
                cleanup_ok = server.stop() if server.proc is not None else True
            except KeyboardInterrupt:
                failure = "KeyboardInterrupt"
                capture_interrupted = capture_started
                if server.proc is not None:
                    cleanup_ok = server.stop()
            except Exception as exc:
                failure = type(exc).__name__
                capture_interrupted = capture_started
                if server.proc is not None:
                    cleanup_ok = server.stop()
            if not captured:
                result = {
                    "workflow_status": "INCOMPLETE" if (capture_interrupted or failure == "KeyboardInterrupt")
                    else "FAIL",
                    "experiment_verdict": "UNMEASURED",
                    "caller_completed": False,
                    "cleanup_succeeded": cleanup_ok,
                    "terminal_reason": ("serial_capture_interrupted" if capture_interrupted else
                                         failure or "capture_not_started"),
                }
            else:
                if selected in MEMORY_CAPTURE_VARIANTS and upload_metrics is not None:
                    result = classify_upload_capture(
                        post_reset,
                        cleanup_ok,
                        serial_path,
                        upload_metrics,
                        server_errors=server_errors,
                        capture_variant=selected,
                    )
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
            max_events=10000 if selected in MEMORY_CAPTURE_VARIANTS else 2000,
            execute=execute,
        )
