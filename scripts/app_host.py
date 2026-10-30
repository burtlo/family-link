"""Product app firmware build, flash, and serial monitor (client/device).

``make app:build`` / ``make app:flash`` / ``make app:monitor`` — kit selection
matches labs (KIT/PORT) with an interactive prompt when more than one registered
kit is connected.

``make app:log`` is a non-interactive serial logger for agent debugging sessions:
it needs no TTY, writes host-timestamped lines to a file as they arrive, and
releases the port when the process is stopped.
"""
from __future__ import annotations

import argparse
import datetime
import os
import subprocess
import sys
import time
from pathlib import Path

from deployment_config import load_active_deployment
from device_targets import (
    connected_registered_kits,
    kit_for,
    kit_nicknames,
    ports,
    resolve_port,
    validate_bindings,
)
from firmware import (
    build as firmware_build,
    firmware_build_id_from_package,
    flash as firmware_flash,
    idf_environment,
    idf_product_cmake_args,
)
from lab_host import resolve_run_target
from project_config import ProjectConfig, ProjectConfigError, ROOT


def _format_kit_line(deployment, kit, index: int | None = None) -> str:
    names = ", ".join(kit_nicknames(deployment, kit))
    prefix = f"{index}. " if index is not None else ""
    return f"{prefix}{names} ({kit.id})"


def prompt_kit_choice(deployment, kits) -> str:
    print("Multiple registered kits are connected. Choose one:", file=sys.stderr)
    for index, kit in enumerate(kits, start=1):
        print(f"  {_format_kit_line(deployment, kit, index)}", file=sys.stderr)
    while True:
        try:
            line = input("KIT (number or nickname): ").strip()
        except EOFError:
            raise ProjectConfigError("KIT selection required; set KIT=<nickname> or connect one kit")
        if not line:
            continue
        if line.isdigit():
            choice = int(line)
            if 1 <= choice <= len(kits):
                return kits[choice - 1].id
            print(f"Enter 1–{len(kits)} or a nickname.", file=sys.stderr)
            continue
        try:
            return kit_for(deployment, line).id
        except ProjectConfigError:
            print(f"Unknown kit nickname: {line}", file=sys.stderr)


def resolve_build_kit(config: ProjectConfig, kit: str = "") -> str:
    """Nickname/id for deployment sdkconfig overlay, or empty to use menuconfig defaults."""
    if kit.strip():
        return kit.strip()
    deployment = load_active_deployment(config)
    connected = connected_registered_kits(deployment, ports())
    if len(connected) == 1:
        return connected[0].id
    return ""


def resolve_flash_kit(config: ProjectConfig, kit: str = "", port: str = "") -> str:
    if kit or port:
        return resolve_run_target(config, kit, port).kit_id
    deployment = load_active_deployment(config)
    validate_bindings(deployment)
    available = ports()
    connected = connected_registered_kits(deployment, available)
    if not connected:
        raise ProjectConfigError(
            "app:flash needs exactly one connected registered kit; run make devices"
        )
    if len(connected) == 1:
        return connected[0].id
    if not sys.stdin.isatty():
        summary = "; ".join(_format_kit_line(deployment, item) for item in connected)
        raise ProjectConfigError(
            f"app:flash found {len(connected)} connected kits ({summary}); set KIT=<nickname>"
        )
    return prompt_kit_choice(deployment, connected)


def resolve_app_serial_port(config: ProjectConfig, kit: str = "", port: str = "") -> str:
    if kit.strip() or port.strip():
        return resolve_run_target(config, kit.strip(), port.strip()).device
    kit_id = resolve_flash_kit(config, "", "")
    deployment = load_active_deployment(config)
    kit_obj = next((item for item in deployment.kits if item.id == kit_id), None)
    if kit_obj is None:
        raise ProjectConfigError(f"Unknown kit id {kit_id}")
    return resolve_port(kit_obj, ports()).device


def monitor_app(config: ProjectConfig, kit: str = "", port: str = "", print_filter: str = "") -> int:
    env, idf = idf_environment(config)
    device = resolve_app_serial_port(config, kit, port)
    build_id = firmware_build_id_from_package(config)
    argv = [*idf, *idf_product_cmake_args(config, build_id), "-p", device, "monitor"]
    filter_text = print_filter.strip()
    if filter_text:
        argv.extend(["--print-filter", filter_text])
    print(f"Serial monitor {device} build_id={build_id}", file=sys.stderr)
    if filter_text:
        print(f"  print-filter: {filter_text}", file=sys.stderr)
    completed = subprocess.run([str(part) for part in argv], cwd=ROOT, env=env)
    return int(completed.returncode)


def stream_serial_lines(connection, log, echo, seconds: float = 0.0, clock=time.monotonic, now=None) -> int:
    """Copy serial lines to ``log`` with a host timestamp; return lines written.

    ``seconds`` <= 0 runs until the connection raises or the process is stopped.
    """
    now = now or _log_stamp
    deadline = clock() + seconds if seconds > 0 else None
    pending = b""
    written = 0
    try:
        while deadline is None or clock() < deadline:
            chunk = connection.read(2048)
            if not chunk:
                continue
            pending += chunk
            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                text = raw.rstrip(b"\r").decode("utf-8", errors="replace")
                _write_log_line(log, echo, f"[{now()}] {text}\n")
                written += 1
    finally:
        if pending:
            _write_log_line(log, echo, f"[{now()}] {pending.decode('utf-8', errors='replace')}\n")
            written += 1
    return written


def _log_stamp() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def _write_log_line(log, echo, line: str) -> None:
    log.write(line)
    log.flush()
    echo.write(line)
    echo.flush()


def default_log_path(config: ProjectConfig, label: str) -> Path:
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    return config.path_value("firmware.logs") / "debug" / f"{stamp}-{label}.log"


def _quiet_serial(serial_module, device: str):
    connection = serial_module.Serial()
    connection.port = device
    connection.baudrate = 115200
    connection.timeout = 0.25
    # Deasserted before open so attaching the logger does not reset the chip.
    connection.dtr = False
    connection.rts = False
    return connection


def log_app(config: ProjectConfig, kit: str = "", port: str = "", out: str = "", seconds: float = 0.0) -> int:
    """Log until stopped or ``seconds`` elapse; reopen the port after a reset re-enumerates USB."""
    import serial

    device = resolve_app_serial_port(config, kit, port)
    label = (kit or Path(device).name).replace(":", "").replace("/", "_") or "kit"
    log_path = Path(out) if out else default_log_path(config, label)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + seconds if seconds > 0 else None
    print(f"Serial log {device} -> {log_path}", file=sys.stderr, flush=True)
    lost = False
    try:
        with log_path.open("w", encoding="utf-8") as log:
            while deadline is None or time.monotonic() < deadline:
                try:
                    with _quiet_serial(serial, device) as connection:
                        if lost:
                            _write_log_line(log, sys.stdout, f"[{_log_stamp()}] [host] serial reconnected {device}\n")
                            lost = False
                        remaining = 0.0 if deadline is None else max(0.01, deadline - time.monotonic())
                        stream_serial_lines(connection, log, sys.stdout, remaining)
                except (serial.SerialException, OSError) as exc:
                    if not lost:
                        _write_log_line(log, sys.stdout, f"[{_log_stamp()}] [host] serial lost: {exc}\n")
                        lost = True
                    time.sleep(0.5)
                    try:
                        device = resolve_app_serial_port(config, kit, port)
                    except ProjectConfigError:
                        pass
    except KeyboardInterrupt:
        pass
    print(f"Serial log closed: {log_path}", file=sys.stderr, flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "flash", "monitor", "log"])
    parser.add_argument("--kit", default="")
    parser.add_argument("--port", default="")
    parser.add_argument("--out", default="", help="log: output file (default logs/device/debug/<stamp>-<kit>.log)")
    parser.add_argument("--seconds", type=float, default=0.0, help="log: stop after N seconds (0 = until stopped)")
    parser.add_argument(
        "--print-filter",
        default="",
        help="IDF monitor filter (e.g. 'product_mute:I product_login:I')",
    )
    parser.add_argument(
        "--p1-host",
        default="",
        help="Workstation IPv4 or P1 base URL for desk (also FAMILY_P1_HOST / P1_HOST env)",
    )
    args = parser.parse_args()
    try:
        if args.p1_host.strip():
            os.environ["FAMILY_P1_HOST"] = args.p1_host.strip()
        config = ProjectConfig()
        config.validate()
        if args.action == "build":
            build_kit = resolve_build_kit(config, args.kit)
            firmware_build(config, product_kit=build_kit)
            return 0
        if args.action == "flash":
            kit_id = resolve_flash_kit(config, args.kit, args.port)
            firmware_flash(config, kit_id, False)
            return 0
        if args.action == "log":
            return log_app(config, args.kit, args.port, args.out, args.seconds)
        return monitor_app(config, args.kit, args.port, args.print_filter)
    except (ProjectConfigError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
