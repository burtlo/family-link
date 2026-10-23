"""Product app firmware build and flash (client/device).

``make app:build`` / ``make app:flash`` — kit selection matches labs (KIT/PORT)
with an interactive prompt when more than one registered kit is connected.
"""
from __future__ import annotations

import argparse
import os
import sys

from deployment_config import load_active_deployment
from device_targets import (
    connected_registered_kits,
    kit_for,
    kit_nicknames,
    ports,
    validate_bindings,
)
from firmware import build as firmware_build, flash as firmware_flash
from lab_host import resolve_run_target
from project_config import ProjectConfig, ProjectConfigError


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "flash"])
    parser.add_argument("--kit", default="")
    parser.add_argument("--port", default="")
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
        else:
            kit_id = resolve_flash_kit(config, args.kit, args.port)
            firmware_flash(config, kit_id, False)
        return 0
    except (ProjectConfigError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
