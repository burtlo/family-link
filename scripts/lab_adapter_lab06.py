"""LAB-06 bounded stage-shell host integration."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import sys
import time
from pathlib import Path

from lab_runner import RunHooks, run_with_adapter
from project_config import ProjectConfigError

ADAPTER_IDENTITY = "lab06"
ADAPTER_VERSION = "v1"
MAX_CAPTURE_SECONDS = 900
MAX_SERIAL_BYTES = 4 * 1024 * 1024
MAX_LINE_BYTES = 4096
BOOT_TIMEOUT_SECONDS = 30
HARNESS_TIMEOUT_SECONDS = 240
RESET_HOLD_SECONDS = 0.1
DEADLINES_SECONDS = {"preflight":30,"build":900,"boot":BOOT_TIMEOUT_SECONDS,
                     "manual_capture":MAX_CAPTURE_SECONDS,"harness":HARNESS_TIMEOUT_SECONDS,
                     "parse":30,"cleanup":5}
_JSON_BLOCKED_FIELD = re.compile(r'(?i)"(?:pin|media_payload|payload_b64|image_data|audio_data)"\s*:')
_JSON_PASSWORD_VALUE = re.compile(
    r'(?i)("(?:password|passwd)"\s*:\s*)("(?:\\.|[^"\\])*"|[^,}\s]+)'
)


def _parser_module(spec):
    path = spec.trace_parser
    if path is None or not path.is_file():
        raise ProjectConfigError("LAB-06 trace parser is not configured")
    module_name = "lab06_parse_lab_trace"
    module = sys.modules.get(module_name)
    if module is None:
        load = importlib.util.spec_from_file_location(module_name, path)
        if load is None or load.loader is None:
            raise ProjectConfigError("cannot load LAB-06 parser")
        module = importlib.util.module_from_spec(load)
        sys.modules[module_name] = module
        load.loader.exec_module(module)
    return module


def _safe_line(raw: bytes, parser) -> bytes:
    if len(raw) > MAX_LINE_BYTES:
        raise RuntimeError("LAB-06 serial line exceeds byte bound")
    try:
        line = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise RuntimeError("LAB-06 serial contains non-UTF8 data") from exc
    if _JSON_BLOCKED_FIELD.search(line):
        raise RuntimeError("LAB-06 serial line contains a raw PIN or payload")
    line = _JSON_PASSWORD_VALUE.sub(r'\1"[REDACTED]"', line)
    parser.validate_evidence_line(line)
    line = re.sub(r"(?i)https?://[^\s\"']+", "[REDACTED_URL]", line)
    line = re.sub(r"(?i)(password|passwd)\s*[=:]\s*[^\s,;]+", r"\1=[REDACTED]", line)
    line = re.sub(r"(?i)Bearer\s+\S+", "Bearer [REDACTED]", line)
    return (line.rstrip("\r") + "\n").encode("utf-8")


def _capture(host, target, run_id: str, seconds: int, mode: str, variant: str, parser) -> Path:
    if not 1 <= seconds <= MAX_CAPTURE_SECONDS:
        raise ValueError("LAB-06 capture seconds outside bound")
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required for LAB-06 capture") from exc
    path = host.ROOT / "logs/device/labs/lab-06" / run_id / "serial.txt"
    path.parent.mkdir(parents=True, exist_ok=False)
    connection = None
    pending = b""
    total = 0
    ready_count = harness_count = 0
    failed = False
    armed = False
    started = time.monotonic()
    ready_deadline = started + BOOT_TIMEOUT_SECONDS
    finish_deadline = None
    try:
        connection = serial.Serial(target.device, 115200, timeout=0.2)
        connection.dtr = False
        connection.rts = True
        time.sleep(RESET_HOLD_SECONDS)
        connection.reset_input_buffer()
        connection.rts = False
        armed = True
        with path.open("xb") as log:
            while True:
                chunk = connection.read(2048)
                if chunk:
                    pending += chunk
                    pieces = pending.split(b"\n")
                    pending = pieces.pop()
                    if len(pending) > MAX_LINE_BYTES:
                        raise RuntimeError("LAB-06 unterminated serial line exceeds bound")
                    for piece in pieces:
                        safe = _safe_line(piece, parser)
                        if total + len(safe) > MAX_SERIAL_BYTES:
                            raise RuntimeError("LAB-06 serial log exceeds byte bound")
                        log.write(safe); total += len(safe)
                        line = safe.decode("utf-8").strip()
                        if line.startswith("LAB06_INIT_FAILED") or line.startswith("LAB06_RUNTIME_FAILED"):
                            failed = True
                        if line.startswith("LAB06_READY "):
                            ready_count += 1
                            fields = _marker_fields(line, "LAB06_READY ")
                            expected = {"mode":mode,"server":"none","trace_schema":"lab06-trace/v1"}
                            if (not armed or ready_count != 1 or set(fields) != {"build","mode","server","trace_schema"}
                                    or any(fields.get(k) != v for k,v in expected.items())
                                    or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", fields.get("build", ""))):
                                failed = True
                            finish_deadline = time.monotonic() + (seconds if mode == "manual" else HARNESS_TIMEOUT_SECONDS)
                        if line.startswith("LAB06_HARNESS_DONE "):
                            harness_count += 1
                            fields = _marker_fields(line, "LAB06_HARNESS_DONE ")
                            if mode != "scripted" or fields != {"status":"complete"}:
                                failed = True
                now = time.monotonic()
                if failed:
                    break
                if ready_count > 1 or harness_count > 1:
                    failed = True; break
                if mode == "scripted" and harness_count == 1:
                    break
                if ready_count == 0 and now >= ready_deadline:
                    break
                if finish_deadline is not None and now >= finish_deadline:
                    break
            if pending:
                safe = _safe_line(pending, parser)
                if total + len(safe) > MAX_SERIAL_BYTES:
                    raise RuntimeError("LAB-06 serial log exceeds byte bound")
                log.write(safe)
            log.flush()
            if not armed or ready_count != 1:
                raise RuntimeError("LAB06_READY missing or duplicated after reset-gated capture")
            if failed:
                raise RuntimeError("LAB-06 failed or mismatched lifecycle marker")
            if mode == "scripted" and harness_count != 1:
                raise RuntimeError("LAB06_HARNESS_DONE missing before harness deadline")
    finally:
        if connection is not None:
            connection.close()
    return path


def _marker_fields(line: str, prefix: str) -> dict[str,str]:
    body = line[len(prefix):]
    out = {}
    for token in body.split():
        if "=" not in token:
            raise RuntimeError("malformed LAB-06 lifecycle marker")
        key, value = token.split("=",1)
        if not key or not value or key in out:
            raise RuntimeError("duplicate or empty LAB-06 marker field")
        out[key] = value
    return out


def _tree_hash(root: Path, fixtures: bool) -> str:
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if not path.is_file(): continue
        rel = path.relative_to(root)
        if any(p.startswith("build") or p in {"managed_components",".git","__pycache__"} for p in rel.parts): continue
        in_fixtures = rel.parts[0] == "fixtures"
        if in_fixtures != fixtures: continue
        if not fixtures:
            if rel.as_posix() == "sdkconfig" or path.suffix in {".pyc",".o",".a"}: continue
            compiled = (path.suffix.lower() in {".c", ".h", ".cpp", ".hpp", ".s", ".S"}
                        or path.name in {"CMakeLists.txt", "idf_component.yml", "Kconfig.projbuild", "partitions.csv"}
                        or path.name.startswith("sdkconfig.defaults"))
            if not compiled: continue
        digest.update(rel.as_posix().encode()+b"\0"+path.read_bytes()+b"\0")
    return digest.hexdigest()


def _build_identity(project: Path, build: Path, variant: str) -> dict:
    config_path = build / "sdkconfig"
    binary_path = build / "lab06_stage_shell.bin"
    if not config_path.is_file() or not binary_path.is_file():
        raise RuntimeError("LAB-06 build identity artifacts missing")
    config = config_path.read_text(encoding="utf-8")
    ids = re.findall(r'^CONFIG_LAB06_BUILD_ID="([^"\r\n]+)"$',config,re.M)
    pool = re.findall(r"^CONFIG_LV_MEM_SIZE=(\d+)$",config,re.M)
    scripted = "CONFIG_LAB06_SCRIPTED=y" in config.splitlines()
    if (len(ids)!=1 or ids[0] != f"lab06-fw-{variant}-v3" or len(pool)!=1
            or int(pool[0])!=65536 or scripted != (variant=="scripted")):
        raise RuntimeError("LAB-06 resolved sdkconfig does not match selected variant/pool")
    compiled = {"build_id":ids[0],"variant":variant,"source_sha256":_tree_hash(project,False),
                "fixture_sha256":_tree_hash(project,True),"sdkconfig_sha256":hashlib.sha256(config_path.read_bytes()).hexdigest(),
                "binary_sha256":hashlib.sha256(binary_path.read_bytes()).hexdigest()}
    fingerprint_input = {key:compiled[key] for key in ("build_id","variant","source_sha256","sdkconfig_sha256","binary_sha256")}
    compiled["build_fingerprint"] = "sha256:"+hashlib.sha256(json.dumps(fingerprint_input,sort_keys=True,separators=(",",":")).encode()).hexdigest()
    return compiled


def _cleanup():
    """LAB-06 owns no server or child process; keep cleanup an explicit gate."""


class Adapter:
    def run(self, host, config, spec, args, *, mode: str) -> int:
        backend = getattr(args,"backend","managed-http")
        if backend not in (None,"","managed-http"):
            raise ProjectConfigError("LAB-06 has no backend; reject unsupported BACKEND before I/O")
        requested_mode = getattr(args,"mode",None) or mode
        if requested_mode not in {"manual","scripted"}:
            raise ProjectConfigError("LAB-06 mode must be manual or scripted")
        selected = args.variant or (requested_mode if requested_mode in spec.variants else spec.default_variant)
        if selected not in spec.variants:
            raise ProjectConfigError(f"unknown LAB-06 variant {selected}")
        if selected != requested_mode:
            raise ProjectConfigError("LAB-06 variant/phase mismatch")
        seconds = args.seconds if args.seconds is not None else spec.capture_seconds_default
        if not 1 <= seconds <= MAX_CAPTURE_SECONDS:
            raise ProjectConfigError("LAB-06 capture seconds outside bound")
        flash = host._flash_from_env_and_args(args,spec,for_run=True)
        parser = _parser_module(spec)

        def execute(context,evidence):
            stages={"preflight":"not_started","service":"not_configured","build":"not_started","activity":"not_started","parser":"not_started","scenario":"not_configured","cleanup":"not_started"}
            cleanup_ok=completed=False; workflow="FAIL"; reason="run_failed"; experiment="UNMEASURED"; target=None
            def event(outcome,stage,**fields):
                evidence.event_sink({"source":"host","kind":"stage","stage":stage,"outcome":outcome,"adapter_identity":ADAPTER_IDENTITY,"adapter_version":ADAPTER_VERSION,**fields})
            try:
                evidence.update_context({"firmware":{"project_path":str(spec.project_dir),"target":spec.target},"server":{"path":"none"},"capture":{"mode":requested_mode,"variant":selected,"seconds":seconds},"scenarios":parser.DECIDED,"source":{"revision":"lab06-host/v1"},"fixture":{"revision":"lab06-stage-shell/v1","schema":"lab06-stage-shell-fixtures/v1"}})
                event("running","preflight")
                target=host.resolve_run_target(config,args.kit,args.port)
                stages["preflight"]="passed"; evidence.update_context({"hardware":{"kit_nickname":target.kit_id}}); event("passed","preflight")
                event("running","build")
                if getattr(args,"no_build",False): raise RuntimeError("LAB-06 requires a fresh registered build")
                source_before=_tree_hash(spec.project_dir,False); fixture_before=_tree_hash(spec.project_dir,True)
                resolved=host.resolve_variant(spec,selected); build_dir=host.ROOT/resolved.build_dir
                build_result=host.build_lab(config,spec.make_name,selected,kit=target.kit_id,port=target.device,target=target)
                identity=_build_identity(spec.project_dir,build_dir,selected)
                if identity["source_sha256"]!=source_before or identity["fixture_sha256"]!=fixture_before: raise RuntimeError("LAB-06 source changed during build")
                stages["build"]="passed"
                evidence.update_context({"build":{"status":"passed"},"firmware":identity,"fixture":{"revision":"lab06-stage-shell/v1","schema":"lab06-stage-shell-fixtures/v1","sha256":identity["fixture_sha256"]}})
                event("passed","build",result=str(build_result))
                if flash:
                    port=host.revalidate_run_target(config,target); host.flash_lab_build(config,build_result,port); time.sleep(1)
                serial_path=host.ROOT/"logs/device/labs/lab-06"/context.run_id/"serial.txt"
                evidence.update_context({"artifacts":{"serial_log":serial_path.relative_to(host.ROOT).as_posix()}})
                event("running","activity",mode=requested_mode)
                serial_path=_capture(host,target,context.run_id,seconds,requested_mode,selected,parser)
                stages["activity"]="passed"; evidence.update_context({"capture":{"status":"captured","post_reset":True},"artifacts":{"serial_log":serial_path.relative_to(host.ROOT).as_posix()}})
                event("passed","activity",mode=requested_mode)
                event("running","parser")
                parsed=parser.parse_file(serial_path,output_path=evidence.run_dir/"metrics.json",expected_run_id=context.run_id,expected_mode=requested_mode,expected_variant=selected,post_reset=True,capture_context={"kind":"device_serial_capture","target_id":target.kit_id,"post_reset":True,"reset_performed":True,"ready_marker_verified":True,"build_fingerprint":identity["build_fingerprint"],"fixture_sha256":identity["fixture_sha256"]},expected_build_id=identity["build_id"])
                stages["parser"]="passed"; experiment=parsed["lab_metrics"]["experiment_verdict"]
                evidence.update_context({"artifacts":{"metrics":(evidence.run_dir/"metrics.json").relative_to(host.ROOT).as_posix()},"capture":{"parser_status":"parsed","parser_identity":"lab06-trace/v1"},"event_loss":{"firmware_trace":{"status":"UNKNOWN","count":None}}})
                completed=True; workflow="PASS"; reason="post_reset_harness_parsed" if requested_mode=="scripted" else "post_reset_capture_parsed"
            except KeyboardInterrupt:
                workflow,reason="INCOMPLETE","KeyboardInterrupt"
            except Exception as exc:
                workflow,reason="FAIL",type(exc).__name__
                try: event("failed","run",reason=reason)
                except Exception: pass
            finally:
                try:
                    _cleanup(); cleanup_ok=True; stages["cleanup"]="passed"; event("passed","cleanup")
                except Exception:
                    cleanup_ok=False; stages["cleanup"]="failed"; workflow="FAIL"; completed=False; experiment="INCOMPLETE"; reason="cleanup_failed"
                if not cleanup_ok: workflow="FAIL"; completed=False
                evidence.update_context({"capture":{"stages":stages}})
            return {"workflow_status":workflow,"experiment_verdict":experiment,"caller_completed":completed,"cleanup_succeeded":cleanup_ok,"terminal_reason":reason,"stages":stages}

        return run_with_adapter(repo_root=host.ROOT,lab_id=spec.lab_id,evidence_slug=spec.evidence_slug,variant=selected,mode=requested_mode,requested_run_id=args.run_id,hooks=RunHooks(lambda:None,lambda _t:None,lambda _t,_r:None),project_path=str(spec.project_dir),firmware_target=spec.target,capture_seconds=seconds,execute=execute)
