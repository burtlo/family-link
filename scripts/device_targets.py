"""Physical kit selection; aliases are nicknames, never logged-in users."""
from dataclasses import dataclass
import os

from deployment_config import Deployment, Kit, load_deployment
from project_config import ProjectConfigError


@dataclass(frozen=True)
class Port:
    device: str
    serial: str


def serial_key(value: str) -> str:
    return value.strip().upper().replace(":", "").replace("-", "")


def ports() -> list[Port]:
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise ProjectConfigError("pyserial missing; run make install and use the repository Python environment") from exc
    return [Port(p.device, p.serial_number or "") for p in list_ports.comports()]


def kit_for(deployment: Deployment, name: str) -> Kit:
    endpoint = deployment.resolve_alias(name) or name
    matches = [k for k in deployment.kits if k.id == name or k.endpoint_id == endpoint]
    if len(matches) != 1:
        raise ProjectConfigError(f"Unknown or ambiguous kit nickname: {name}")
    return matches[0]


def validate_bindings(deployment: Deployment) -> None:
    seen = set()
    for kit in deployment.kits:
        key = serial_key(kit.usb_serial)
        if key and key in seen:
            raise ProjectConfigError("Duplicate USB serial binding in deployment roster")
        if key:
            seen.add(key)


def resolve_port(kit: Kit, available: list[Port]) -> Port:
    if not kit.usb_serial:
        raise ProjectConfigError(f"{kit.id} is unbound; use make device.bind KIT=<nickname> PORT=<port>")
    matches = [p for p in available if p.serial and serial_key(p.serial) == serial_key(kit.usb_serial)]
    if len(matches) != 1:
        raise ProjectConfigError(f"{kit.id}: expected one connected USB match, found {len(matches)}")
    return matches[0]


def select(deployment: Deployment, available: list[Port], name: str = "", all_kits: bool = False):
    validate_bindings(deployment)
    if all_kits:
        kits = list(deployment.kits)
    elif name:
        kits = [kit_for(deployment, name)]
    else:
        kits = [k for k in deployment.kits if k.usb_serial and any(
            p.serial and serial_key(p.serial) == serial_key(k.usb_serial) for p in available)]
        if len(kits) != 1:
            raise ProjectConfigError("make flash needs exactly one connected registered kit; use make flash.<nickname>")
    targets = [(k, resolve_port(k, available)) for k in kits]
    if len({p.device for _, p in targets}) != len(targets):
        raise ProjectConfigError("Multiple kits resolve to the same port")
    return targets


def bind(deployment: Deployment, name: str, port_name: str, available: list[Port], project=None) -> None:
    import yaml
    if deployment.source != "local":
        raise ProjectConfigError("Copy the example roster to config/deployment/local.yaml before binding")
    kit = kit_for(deployment, name)
    matches = [p for p in available if p.device == port_name and p.serial]
    if len(matches) != 1:
        raise ProjectConfigError("Binding needs one connected port with a readable USB serial")
    selected = matches[0]
    for other in deployment.kits:
        if other.id != kit.id and other.usb_serial and serial_key(other.usb_serial) == serial_key(selected.serial):
            raise ProjectConfigError(f"USB serial already belongs to {other.id}")
    raw = yaml.safe_load(deployment.path.read_text(encoding="utf-8"))
    for row in raw["kits"]:
        if row["id"] == kit.id:
            row["usb_serial"] = selected.serial
    temporary = deployment.path.with_suffix(".binding.tmp")
    try:
        temporary.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
        load_deployment(temporary, source="local", project=project)
        os.replace(temporary, deployment.path)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Bound {name} ({kit.id}) to USB serial {selected.serial}")
