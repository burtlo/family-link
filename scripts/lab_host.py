#!/usr/bin/env python3
"""Build, flash, and capture device lab firmware (see config/labs.registry.yaml)."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from deployment_config import Deployment, Kit, load_active_deployment
from device_targets import Port, kit_for, ports, resolve_port as resolve_kit_port, select, serial_key, validate_bindings
from lab_deployment_sdkconfig import deployment_defaults_sidecars, write_deployment_kit_sdkconfig
from firmware import idf_environment, run
from lab_registry import (
    LabSpec,
    get_lab,
    load_registry,
    print_operator_help,
    resolve_variant,
    run_mode_hint_lines,
)
from project_config import ProjectConfig, ProjectConfigError


def load_lab02_workflow_config(*args, **kwargs):
    """Lazy compatibility import used only while preparing LAB-02 builds."""
    from lab02_workflow_config import load_lab02_workflow_config as load
    return load(*args, **kwargs)


def write_lab02_local_overlay(*args, **kwargs):
    """Lazy compatibility import used only while preparing LAB-02 builds."""
    from lab02_experiment_sdkconfig import write_lab02_local_overlay as write
    return write(*args, **kwargs)


def run_canonical(*args, **kwargs):
    """Lazy legacy hook retained for compatibility tests and adapter dispatch."""
    from lab02_canonical_run import run_canonical as run
    return run(*args, **kwargs)

_BANNER = "-" * 62


@dataclass(frozen=True)
class RunTarget:
    """Registered kit and exact USB identity frozen for one host run."""

    kit_id: str
    device: str
    serial: str
    kit: Kit
    deployment: Deployment


def resolve_run_target(config: ProjectConfig, kit: str = "", port: str = "") -> RunTarget:
    deployment = load_active_deployment(config)
    validate_bindings(deployment)
    available = ports()
    if kit:
        targets = select(deployment, available, kit, False)
    elif port:
        matches = [item for item in available if item.device == port]
        if len(matches) != 1 or not matches[0].serial:
            raise ProjectConfigError("PORT must identify one connected device with a readable USB serial")
        key = serial_key(matches[0].serial)
        kits = [item for item in deployment.kits if item.usb_serial and serial_key(item.usb_serial) == key]
        if len(kits) != 1:
            raise ProjectConfigError("PORT does not identify exactly one registered kit")
        targets = select(deployment, available, kits[0].id, False)
    else:
        targets = select(deployment, available, "", False)
    if len(targets) != 1:
        raise ProjectConfigError("A run needs exactly one connected registered kit")
    selected_kit, selected_port = targets[0]
    if port and port != selected_port.device:
        raise ProjectConfigError(
            f"PORT does not match registered kit {selected_kit.id}'s connected USB binding"
        )
    return RunTarget(
        selected_kit.id, selected_port.device, selected_port.serial, selected_kit, deployment
    )


def revalidate_run_target(config: ProjectConfig, target: RunTarget) -> str:
    """Confirm the frozen device path still reports the registered serial."""
    deployment = load_active_deployment(config)
    validate_bindings(deployment)
    matches = [item for item in deployment.kits if item.id == target.kit_id]
    if len(matches) != 1:
        raise ProjectConfigError("Frozen run kit is no longer uniquely registered")
    kit = matches[0]
    current = resolve_kit_port(kit, ports())
    if current.device != target.device or serial_key(current.serial) != serial_key(target.serial):
        raise ProjectConfigError("Frozen run target disconnected or changed before flash")
    return current.device


def _rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def _print_run_banner(title: str, lines: list[str]) -> None:
    print(_BANNER)
    print(f"  {title}")
    for line in lines:
        print(f"  {line}")
    print(_BANNER)
    print()


def print_run_start(
    spec: LabSpec,
    *,
    run_id: str,
    variant: str,
    action: str,
    seconds: int,
    flash: bool,
) -> None:
    _print_run_banner(
        f"LAB RUN START: lab.{spec.make_name} ({spec.lab_id})",
        [
            f"run_id={run_id}",
            f"variant={variant}  action={action}  capture={seconds}s  flash={'yes' if flash else 'no'}",
        ],
    )


def _parse_make_command(make_name: str, run_id: str) -> str:
    return f"make lab.{make_name}.parse RUN_ID={run_id}"


def print_run_complete(
    spec: LabSpec,
    *,
    run_id: str,
    serial_path: Path,
    metrics_path: Path | None,
    parsed: bool,
) -> None:
    parse_cmd = _parse_make_command(spec.make_name, run_id)
    body = [
        f"run_id={run_id}",
        "",
        "Files:",
        f"  serial.txt -> {_rel(serial_path)}",
    ]
    if parsed and metrics_path is not None:
        body.append(f"  metrics.json -> {_rel(metrics_path)}  (parsed during this run)")
    elif spec.trace_parser:
        body.append("  metrics.json -> (not written; parse failed or skipped)")
    body.extend(
        [
            "",
            "Make:",
            f"  {parse_cmd}",
        ]
    )
    if spec.run_instructions_doc:
        body.append(f"  Evidence doc: {spec.run_instructions_doc}")
    _print_run_banner(f"LAB RUN COMPLETE: lab.{spec.make_name} ({spec.lab_id})", body)


def _lab_logs_base(config: ProjectConfig, spec: LabSpec) -> Path:
    return config.path_value("firmware.logs") / "labs" / spec.evidence_slug


def _latest_run_dir(log_base: Path) -> Path:
    if not log_base.is_dir():
        raise ProjectConfigError(f"No lab captures under {log_base}")
    candidates = [
        p
        for p in log_base.iterdir()
        if p.is_dir() and (p / "serial.txt").is_file()
    ]
    if not candidates:
        raise ProjectConfigError(f"No serial.txt captures under {log_base}")
    return max(candidates, key=lambda p: p.stat().st_mtime)


def _resolve_serial_path(
    config: ProjectConfig,
    spec: LabSpec,
    *,
    run_id: str,
    serial: Path | None,
) -> tuple[Path, Path]:
    if serial is not None:
        serial_path = serial.resolve()
        if not serial_path.is_file():
            raise ProjectConfigError(f"serial file not found: {serial_path}")
        return serial_path.parent, serial_path
    log_base = _lab_logs_base(config, spec)
    if run_id:
        log_dir = log_base / run_id
    else:
        log_dir = _latest_run_dir(log_base)
        run_id = log_dir.name
    serial_path = log_dir / "serial.txt"
    if not serial_path.is_file():
        raise ProjectConfigError(f"Missing {serial_path}; run make lab.{spec.make_name} first")
    return log_dir, serial_path


def parse_serial_file(spec: LabSpec, serial_path: Path, metrics_path: Path) -> None:
    if not spec.trace_parser:
        raise ProjectConfigError(f"lab.{spec.make_name} has no trace_parser in the registry")
    parser = ROOT / spec.trace_parser
    if not parser.is_file():
        raise ProjectConfigError(f"trace parser not found: {parser}")
    metrics_path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [sys.executable, str(parser), str(serial_path), "-o", str(metrics_path)],
        check=True,
        cwd=str(ROOT),
    )


def _sdkconfig_defaults_files(
    project: Path, variant_defaults: tuple[Path, ...]
) -> list[Path]:
    if variant_defaults:
        return [ROOT / path for path in variant_defaults]
    default = ROOT / project / "sdkconfig.defaults"
    return [default] if default.is_file() else []


def _sdkconfig_defaults_env(project: Path, variant_defaults: tuple[Path, ...]) -> dict[str, str]:
    files = _sdkconfig_defaults_files(project, variant_defaults)
    return _sdkconfig_defaults_env_from_files(files)


def _sdkconfig_defaults_env_from_files(files: list[Path]) -> dict[str, str]:
    if not files:
        return {}
    # ESP-IDF / CMake list variables use ';' on all platforms (':' is not split).
    return {"SDKCONFIG_DEFAULTS": ";".join(str(path) for path in files)}


def _variant_uses_deployment_kit_secrets(variant_name: str) -> bool:
    return variant_name in {"phaseb", "scripted-phaseb"}


def _prepare_lab_build_directory(build_dir: Path) -> None:
    """ESP-IDF set-target refuses fullclean on a non-CMake directory; start empty or configured."""
    if (build_dir / "CMakeCache.txt").is_file():
        return
    if build_dir.is_dir():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)


def clean_lab(config: ProjectConfig, make_name: str, variant: str | None) -> None:
    spec = get_lab(make_name)
    var = resolve_variant(spec, variant)
    build_dir = ROOT / var.build_dir
    if not build_dir.is_dir():
        print(f"Already clean: {_rel(build_dir)}")
        return
    if (build_dir / "CMakeCache.txt").is_file():
        env, idf = idf_environment(config)
        project = ROOT / spec.project_dir
        base = idf + [
            "-C",
            str(project),
            "-B",
            str(build_dir),
            f"-DIDF_TARGET={spec.target}",
            f"-DSDKCONFIG={build_dir / 'sdkconfig'}",
        ]
        timeout = config.integer("command.lab.build.timeout_seconds", default=1200, minimum=1)
        try:
            run(base + ["fullclean"], env=env, timeout=timeout)
            print(f"Cleaned lab.{spec.make_name} variant={var.name} (idf fullclean) -> {_rel(build_dir)}")
        except (ProjectConfigError, OSError, subprocess.SubprocessError):
            pass
    shutil.rmtree(build_dir)
    for sidecar in deployment_defaults_sidecars(build_dir):
        sidecar.unlink(missing_ok=True)
    for sidecar in (build_dir.parent / f"{build_dir.name}.experiment.defaults",
                    build_dir.parent / f"{build_dir.name}.experiment.defaults.sha256"):
        sidecar.unlink(missing_ok=True)
    print(f"Cleaned lab.{spec.make_name} variant={var.name} -> removed {_rel(build_dir)}")


def _defaults_files_for_lab_build(
    config: ProjectConfig,
    spec: LabSpec,
    var,
    build_dir: Path,
    kit: str,
    port: str,
    target: RunTarget | None = None,
    frozen_workflow=None,
    frozen_fixture_size: int | None = None,
    frozen_scenario: str | None = None,
    frozen_build_id: str = "",
) -> list[Path]:
    defaults_files = _sdkconfig_defaults_files(spec.project_dir, var.sdkconfig_defaults)
    if not _variant_uses_deployment_kit_secrets(var.name):
        if spec.lab_id == "LAB-02" and var.name in {"recycle", "scripted", "eager"}:
            try:
                import yaml
                if frozen_fixture_size is None or frozen_scenario is None:
                    registry = yaml.safe_load(config.path_value("labs.registry").read_text(encoding="utf-8"))
                    workflow = registry["labs"]["lab02"]["workflow"]
                    fixture_size = int(workflow["fixture_size_default"])
                    scenario = str(workflow["scenario_default"])
                else:
                    fixture_size, scenario = frozen_fixture_size, frozen_scenario
                overlay = write_lab02_local_overlay(
                    build_dir,
                    fixture_size=fixture_size,
                    scenario=scenario,
                    scripted=var.name == "scripted",
                    build_id=frozen_build_id,
                )
                return defaults_files + [overlay]
            except (OSError, KeyError, TypeError, ValueError):
                raise ProjectConfigError("invalid LAB-02 local experiment defaults") from None
        return defaults_files
    if not kit.strip():
        raise ProjectConfigError(
            "Managed HTTP build requires KIT=<nickname> (deployment secrets_profiles are per kit); "
            "e.g. make lab.lab02.build KIT=lynn"
        )
    deployment = target.deployment if target is not None else load_active_deployment(config)
    resolved = target.kit if target is not None else kit_for(deployment, kit)
    profile = deployment.secrets_profile_for_kit(resolved)
    if not profile or not profile.wifi_ssid or not profile.wifi_password:
        raise ProjectConfigError(
            "Managed HTTP build requires per-kit Wi-Fi credentials in the private deployment roster"
        )
    workflow = frozen_workflow or load_lab02_workflow_config(config)
    overlay = write_deployment_kit_sdkconfig(
        build_dir, resolved, deployment, workflow.client_url,
        fixture_size=workflow.fixture_size_default, scenario=workflow.scenario_default,
        scripted=var.name == "scripted-phaseb",
        build_id=frozen_build_id,
    )
    return defaults_files + [overlay]


def _sdkconfig_assignment_lines(path: Path) -> list[str]:
    """Non-comment CONFIG_*= lines from a defaults or sdkconfig file."""
    if not path.is_file():
        return []
    lines: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("CONFIG_") and "=" in line:
            lines.append(line)
    return lines


def _sdkconfig_missing_assignments(sdkconfig_path: Path, defaults_files: list[Path]) -> list[str]:
    """Defaults entries absent from an existing sdkconfig (IDF does not re-merge defaults)."""
    if not sdkconfig_path.is_file() or not defaults_files:
        return []
    existing = sdkconfig_path.read_text(encoding="utf-8")
    required: list[str] = []
    seen: set[str] = set()
    for path in defaults_files:
        for line in _sdkconfig_assignment_lines(path):
            key = line.split("=", 1)[0]
            if key in seen:
                continue
            seen.add(key)
            required.append(line)
    missing: list[str] = []
    for line in required:
        key = line.split("=", 1)[0]
        if f"{key}=" not in existing:
            missing.append(line)
    return missing


def _refresh_stale_sdkconfig(build_dir: Path, defaults_files: list[Path]) -> bool:
    sdkconfig = build_dir / "sdkconfig"
    missing = _sdkconfig_missing_assignments(sdkconfig, defaults_files)
    if not missing:
        return False
    if sdkconfig.is_file():
        print(
            f"Removing stale { _rel(sdkconfig) } (missing {len(missing)} variant default(s), "
            f"e.g. {missing[0].split('=', 1)[0]})"
        )
        sdkconfig.unlink()
    old = build_dir / "sdkconfig.old"
    if old.is_file():
        old.unlink()
    return True


def ensure_lab_built(
    config: ProjectConfig,
    make_name: str,
    variant: str | None,
    *,
    kit: str = "",
    port: str = "",
    target: RunTarget | None = None,
) -> Path:
    """Build when artifacts are missing or sdkconfig is stale vs variant defaults."""
    spec = get_lab(make_name)
    var = resolve_variant(spec, variant)
    build_dir = ROOT / var.build_dir
    defaults_files = _defaults_files_for_lab_build(config, spec, var, build_dir, kit, port, target)
    stale = _refresh_stale_sdkconfig(build_dir, defaults_files)
    if stale or not (build_dir / "flasher_args.json").is_file():
        return build_lab(
            config,
            make_name,
            variant,
            kit=kit,
            port=port,
            target=target,
            frozen_defaults_files=defaults_files,
        )
    return build_dir


def build_lab(
    config: ProjectConfig,
    make_name: str,
    variant: str | None,
    *,
    kit: str = "",
    port: str = "",
    target: RunTarget | None = None,
    frozen_defaults_files: list[Path] | None = None,
    frozen_workflow=None,
    frozen_fixture_size: int | None = None,
    frozen_scenario: str | None = None,
    frozen_build_id: str = "",
) -> Path:
    spec = get_lab(make_name)
    var = resolve_variant(spec, variant)
    if config.text("idf.target", required=True) != spec.target:
        raise ProjectConfigError(f"Lab {spec.make_name} target {spec.target} differs from configured idf.target")
    env, idf = idf_environment(config)
    project = ROOT / spec.project_dir
    build_dir = ROOT / var.build_dir
    _prepare_lab_build_directory(build_dir)
    defaults_files = (
        frozen_defaults_files
        if frozen_defaults_files is not None
        else _defaults_files_for_lab_build(config, spec, var, build_dir, kit, port, target,
            frozen_workflow=frozen_workflow, frozen_fixture_size=frozen_fixture_size,
            frozen_scenario=frozen_scenario, frozen_build_id=frozen_build_id)
    )
    _refresh_stale_sdkconfig(build_dir, defaults_files)
    env.update(_sdkconfig_defaults_env_from_files(defaults_files))
    base = idf + [
        "-C",
        str(project),
        "-B",
        str(build_dir),
        f"-DIDF_TARGET={spec.target}",
        f"-DSDKCONFIG={build_dir / 'sdkconfig'}",
    ]
    timeout = config.integer("command.lab.build.timeout_seconds", default=1200, minimum=1)
    # set-target runs fullclean when -B exists; idf.py build configures a fresh tree via -DIDF_TARGET.
    run(base + ["build"], env=env, timeout=timeout)
    print(f"Built lab.{spec.make_name} variant={var.name} -> {build_dir}")
    return build_dir


def flash_lab_build(config: ProjectConfig, build_dir: Path, port: str) -> None:
    args_path = build_dir / "flasher_args.json"
    if not args_path.is_file():
        raise ProjectConfigError(f"Missing {args_path}; run build first")
    meta = json.loads(args_path.read_text(encoding="utf-8"))
    settings = meta["flash_settings"]
    chip = config.text("idf.target", required=True)
    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        chip,
        "--port",
        port,
        "write-flash",
        "--flash-mode",
        settings["flash_mode"],
        "--flash-freq",
        settings["flash_freq"],
        "--flash-size",
        settings["flash_size"],
    ]
    for offset, filename in meta["flash_files"].items():
        cmd.extend([offset, str(build_dir / filename)])
    timeout = config.integer("command.flash.timeout_seconds", default=180, minimum=1)
    subprocess.run(cmd, check=True, timeout=timeout)


def capture_serial(port: str, log_path: Path, seconds: int) -> None:
    import serial

    log_path.parent.mkdir(parents=True, exist_ok=True)
    print(
        f"Recording serial on {port} for {seconds}s "
        f"(no further output until capture finishes)..."
    )
    deadline = time.monotonic() + seconds
    with serial.Serial(port, 115200, timeout=0.25) as connection, log_path.open("wb") as log:
        connection.dtr = False
        connection.rts = False
        while time.monotonic() < deadline:
            log.write(connection.read(2048))


def _print_capture_markers(serial_path: Path) -> None:
    try:
        raw = serial_path.read_bytes()
    except OSError:
        return
    text = raw.decode("utf-8", errors="replace")
    hits: list[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if "HARNESS_DONE" in stripped or "_READY " in stripped or stripped.endswith("_READY"):
            hits.append(stripped)
    if not hits:
        print("Capture summary: no HARNESS_DONE or *_READY line found in serial.txt.")
        return
    print("Capture summary (from serial.txt):")
    for line in hits[:8]:
        print(f"  {line}")
    if len(hits) > 8:
        print(f"  ... and {len(hits) - 8} more")


def resolve_port(config: ProjectConfig, kit: str, port: str) -> str:
    return resolve_run_target(config, kit, port).device


def capture_lab(
    config: ProjectConfig,
    make_name: str,
    *,
    kit: str,
    port: str,
    variant: str | None,
    seconds: int,
    flash: bool,
    run_id: str,
    action: str = "capture",
    emit_operator_help: bool = True,
    target: RunTarget | None = None,
) -> Path:
    spec = get_lab(make_name)
    var = resolve_variant(spec, variant)
    build_dir = ROOT / var.build_dir
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d")
    rid = run_id or f"{stamp}-{var.name}-capture-{uuid.uuid4().hex[:6]}"
    print_run_start(
        spec,
        run_id=rid,
        variant=var.name,
        action=action,
        seconds=seconds,
        flash=flash,
    )
    if emit_operator_help:
        print_operator_help(
            spec,
            variant=var.name,
            action=action,
            run_id=rid,
            seconds=seconds,
            flash=flash,
        )
        print()
    selected_target = target or resolve_run_target(config, kit, port)
    if kit and kit_for(selected_target.deployment, kit).id != selected_target.kit_id:
        raise ProjectConfigError("Frozen run target does not match requested KIT")
    if port and port != selected_target.device:
        raise ProjectConfigError("Frozen run target does not match requested PORT")
    if flash:
        device_port = revalidate_run_target(config, selected_target)
        flash_lab_build(config, build_dir, device_port)
        time.sleep(1.0)
    else:
        device_port = selected_target.device
    log_dir = config.path_value("firmware.logs") / "labs" / spec.evidence_slug / rid
    serial_path = log_dir / "serial.txt"
    capture_serial(device_port, serial_path, seconds)
    meta = {
        "document": "family-link.lab-serial-capture/v1",
        "lab_make_name": spec.make_name,
        "lab_id": spec.lab_id,
        "run_id": rid,
        "captured_at_utc": datetime.now(timezone.utc).isoformat(),
        "port": device_port,
        "variant": var.name,
        "build_dir": str(var.build_dir),
        "flashed": flash,
        "seconds": seconds,
        "instructions": spec.run_instructions,
        "instructions_doc": spec.run_instructions_doc,
    }
    (log_dir / "serial.meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    metrics_path = log_dir / "metrics.json"
    parsed = False
    if spec.trace_parser and (ROOT / spec.trace_parser).is_file():
        try:
            parse_serial_file(spec, serial_path, metrics_path)
            parsed = True
        except (subprocess.CalledProcessError, OSError) as exc:
            print(f"WARNING: parse failed: {exc}", file=sys.stderr)
    _print_capture_markers(serial_path)
    print_run_complete(
        spec,
        run_id=rid,
        serial_path=serial_path,
        metrics_path=metrics_path if parsed else None,
        parsed=parsed,
    )
    return serial_path


def cmd_parse(config: ProjectConfig, args: argparse.Namespace) -> int:
    spec = get_lab(args.lab)
    serial_arg = Path(args.serial).expanduser() if args.serial else None
    log_dir, serial_path = _resolve_serial_path(
        config,
        spec,
        run_id=args.run_id or os.environ.get("RUN_ID", ""),
        serial=serial_arg,
    )
    run_id = log_dir.name
    metrics_path = log_dir / "metrics.json"
    _print_run_banner(
        f"LAB PARSE: lab.{spec.make_name} ({spec.lab_id})",
        [f"run_id={run_id}", f"serial: {_rel(serial_path)}"],
    )
    parse_serial_file(spec, serial_path, metrics_path)
    parse_cmd = _parse_make_command(spec.make_name, run_id)
    _print_run_banner(
        f"LAB PARSE COMPLETE: lab.{spec.make_name} ({spec.lab_id})",
        [
            f"run_id={run_id}",
            "",
            "Files:",
            f"  serial.txt -> {_rel(serial_path)}",
            f"  metrics.json -> {_rel(metrics_path)}",
            "",
            "Make:",
            f"  {parse_cmd}",
        ],
    )
    print()
    with metrics_path.open(encoding="utf-8") as handle:
        print(handle.read())
    return 0


def cmd_list(args: argparse.Namespace) -> int:
    for name, spec in sorted(load_registry().items()):
        if not getattr(args, "variants", False):
            print(f"      {'lab.' + name:<22} {spec.lab_id:<8} Manual — {spec.description}")
            if "scripted" in spec.variants:
                print(f"      {'lab.' + name + '.scripted':<22} {spec.lab_id:<8} Scripted workflow")
            continue
        variants = ", ".join(sorted(spec.variants))
        print(f"  lab.{name:<8} {spec.lab_id:<8} variants: {variants}")
        if spec.description:
            print(f"             {spec.description}")
    return 0


def cmd_help(args: argparse.Namespace) -> int:
    spec = get_lab(args.lab)
    variant = resolve_variant(spec, args.variant or None).name if args.variant else None
    print_operator_help(spec, variant=variant)
    print()
    for line in run_mode_hint_lines(spec):
        print(line)
    return 0


def _flash_from_env_and_args(args, spec: LabSpec, for_run: bool) -> bool:
    if getattr(args, "no_flash", False):
        return False
    if getattr(args, "flash", False):
        return True
    env = os.environ.get("FLASH", "").strip().lower()
    if env in ("0", "false", "no"):
        return False
    if env in ("1", "true", "yes"):
        return True
    if for_run:
        return spec.flash_by_default
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)

    list_p = sub.add_parser("list", help="List registered labs (make lab.help)")
    list_p.add_argument("--variants", action="store_true", help="Include internal registry variants")

    help_p = sub.add_parser("help", help="Operator instructions (make lab.<name>.help)")
    help_p.add_argument("lab")
    help_p.add_argument("--variant", default=os.environ.get("VARIANT", ""))

    build_p = sub.add_parser("build")
    build_p.add_argument("lab")
    build_p.add_argument("--variant", default=os.environ.get("VARIANT", ""))
    build_p.add_argument("--kit", default=os.environ.get("KIT", ""))
    build_p.add_argument("--port", default=os.environ.get("PORT", ""))

    clean_p = sub.add_parser("clean", help="Remove variant build directory (make lab.<name>.clean)")
    clean_p.add_argument("lab")
    clean_p.add_argument("--variant", default=os.environ.get("VARIANT", ""))

    flash_p = sub.add_parser("flash")
    flash_p.add_argument("lab")
    flash_p.add_argument("--variant", default=os.environ.get("VARIANT", ""))
    flash_p.add_argument("--kit", default=os.environ.get("KIT", ""))
    flash_p.add_argument("--port", default=os.environ.get("PORT", ""))

    cap_p = sub.add_parser("capture")
    cap_p.add_argument("lab")
    cap_p.add_argument("--variant", default=os.environ.get("VARIANT", ""))
    cap_p.add_argument("--kit", default=os.environ.get("KIT", ""))
    cap_p.add_argument("--port", default=os.environ.get("PORT", ""))
    cap_p.add_argument("--seconds", type=int, default=None)
    cap_p.add_argument("--flash", action="store_true", default=False)
    cap_p.add_argument("--no-flash", action="store_true")
    cap_p.add_argument("--run-id", default=os.environ.get("RUN_ID", ""))
    cap_p.add_argument("--backend", choices=("managed-http", "local"), default="managed-http")
    cap_p.add_argument("--mode", choices=("manual", "scripted"), default=None)

    run_p = sub.add_parser("run")
    run_p.add_argument("lab")
    run_p.add_argument("--variant", default=os.environ.get("VARIANT", ""))
    run_p.add_argument("--kit", default=os.environ.get("KIT", ""))
    run_p.add_argument("--port", default=os.environ.get("PORT", ""))
    run_p.add_argument("--seconds", type=int, default=None)
    run_p.add_argument("--flash", action="store_true", default=False)
    run_p.add_argument("--no-flash", action="store_true")
    run_p.add_argument("--no-build", action="store_true")
    run_p.add_argument("--backend", choices=("managed-http", "local"), default="managed-http")
    run_p.add_argument("--run-id", default=os.environ.get("RUN_ID", ""))
    run_p.add_argument("--mode", choices=("manual", "scripted"), default=None)

    parse_p = sub.add_parser("parse", help="Parse serial capture to metrics.json (make lab.<name>.parse)")
    parse_p.add_argument("lab")
    parse_p.add_argument("--run-id", default=os.environ.get("RUN_ID", ""))
    parse_p.add_argument(
        "--serial",
        default="",
        help="path to serial.txt (default: RUN_ID folder or latest capture)",
    )

    args = parser.parse_args()
    try:
        config = ProjectConfig()
        config.validate()
        if args.action == "list":
            return cmd_list(args)
        if args.action == "help":
            return cmd_help(args)
        if args.action == "parse":
            return cmd_parse(config, args)
        spec = get_lab(args.lab)
        variant = args.variant or None
        if args.action == "build":
            build_lab(config, args.lab, variant, kit=args.kit, port=args.port)
            return 0
        if args.action == "clean":
            clean_lab(config, args.lab, variant)
            return 0
        if args.action == "flash":
            var = resolve_variant(spec, variant)
            build_dir = ROOT / var.build_dir
            target = resolve_run_target(config, args.kit, args.port)
            if _variant_uses_deployment_kit_secrets(var.name):
                # Refresh the generated Phase B defaults before a direct flash;
                # a DHCP lease change must not leave the BOX pointing at the
                # workstation's previous LAN address.
                build_dir = ensure_lab_built(
                    config,
                    args.lab,
                    variant,
                    kit=target.kit_id,
                    port=target.device,
                    target=target,
                )
            port = revalidate_run_target(config, target)
            flash_lab_build(config, build_dir, port)
            print(f"Flashed lab.{spec.make_name} ({var.name}) on {port}")
            return 0
        if args.action in ("capture", "run"):
            seconds = args.seconds
            if seconds is None:
                seconds = spec.capture_seconds_default
            var = resolve_variant(spec, variant)
            flash = _flash_from_env_and_args(args, spec, for_run=args.action == "run")
            if args.action == "run":
                from lab_adapter_registry import resolve_adapter
                adapter = resolve_adapter(spec)
                result = adapter.run(host=sys.modules[__name__], config=config,
                                     spec=spec, args=args, mode=args.mode or "manual")
                if result is not None:
                    return result
                if getattr(args, "backend", "managed-http") != "managed-http":
                    raise ProjectConfigError("The selected lab adapter does not support this backend")
                from lab_runner_adapter import Adapter as GenericAdapter
                return GenericAdapter().run(sys.modules[__name__], config, spec, args,
                                            mode=args.mode or "manual")
            stamp = datetime.now(timezone.utc).strftime("%Y%m%d")
            rid = args.run_id or f"{stamp}-{var.name}-capture-{uuid.uuid4().hex[:6]}"
            target = resolve_run_target(config, args.kit, args.port)
            if args.action == "run" and not args.no_build:
                ensure_lab_built(
                    config, args.lab, variant, kit=target.kit_id, port=target.device, target=target
                )
            capture_lab(
                config,
                args.lab,
                kit=target.kit_id,
                port=target.device,
                variant=variant,
                seconds=seconds,
                flash=flash,
                run_id=rid,
                action=args.action,
                emit_operator_help=True,
                target=target,
            )
            return 0
        raise ProjectConfigError(f"Unknown action {args.action}")
    except (ProjectConfigError, OSError, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
