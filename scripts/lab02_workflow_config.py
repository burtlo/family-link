"""Validated LAB-02 host workflow settings and firmware client URL."""
from __future__ import annotations

import ipaddress
import platform
import re
import socket
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from urllib.parse import urlsplit

import yaml

from lab_registry import LabRegistryError, validate_document_schema
from project_config import ProjectConfig, ProjectConfigError

ROOT = Path(__file__).resolve().parent.parent
_WINDOWS_INTERFACE_IPV4_RE = re.compile(
    r"^\s*(?:IPv4 Address|IPv4-Adresse)[^:]*:\s*([0-9.]+)",
    re.IGNORECASE | re.MULTILINE,
)
_FIXTURE_SIZES = {8, 16, 64, 256}
_SCENARIOS = {
    "nominal", "S-fix-8", "S-fix-16", "S-fix-64", "S-fix-256",
    "S-trav-20", "S-rev", "S-delay", "S-push", "S-rm-focus", "S-page-snap",
}


class Lab02WorkflowConfigError(ProjectConfigError):
    """Invalid LAB-02 host workflow configuration (details are redacted)."""


@dataclass(frozen=True)
class Lab02WorkflowConfig:
    server_path: Path
    bind_host: str
    bind_port: int
    fixture_size_default: int
    scenario_default: str
    deadlines_seconds: dict[str, int]
    client_url: str


def eligible_lan_ipv4(addresses: Iterable[str]) -> tuple[str, ...]:
    """Return unique private unicast IPv4 addresses suitable for a local server."""
    out: set[str] = set()
    for raw in addresses:
        try:
            address = ipaddress.ip_address(raw)
        except ValueError:
            continue
        if (
            isinstance(address, ipaddress.IPv4Address)
            and address.is_private
            and not address.is_loopback
            and not address.is_link_local
            and not address.is_unspecified
            and not address.is_multicast
        ):
            out.add(str(address))
    return tuple(sorted(out))


def _command_output(argv: list[str]) -> str:
    try:
        result = subprocess.run(
            argv, capture_output=True, text=True, timeout=5, check=False
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    return result.stdout if result.returncode == 0 else ""


def enumerate_interface_ipv4() -> tuple[str, ...]:
    """Enumerate assigned IPv4 addresses on macOS, Linux, or Windows."""
    system = platform.system()
    if system == "Linux":
        output = _command_output(["ip", "-o", "-4", "addr", "show"])
        if output:
            found = []
            for line in output.splitlines():
                fields = line.split()
                if "inet" in fields:
                    index = fields.index("inet")
                    if index + 1 < len(fields):
                        found.append(fields[index + 1].split("/", 1)[0])
            return eligible_lan_ipv4(found)
        output = _command_output(["ifconfig", "-a"])
    elif system == "Windows":
        output = _command_output(["ipconfig", "/all"])
    else:
        output = _command_output(["ifconfig", "-a"])
    if system == "Windows":
        found = _WINDOWS_INTERFACE_IPV4_RE.findall(output)
    else:
        # macOS and the ifconfig fallback on Linux print assigned IPv4 values
        # on lines beginning with `inet`; ignore netmask/broadcast fields.
        found = []
        for line in output.splitlines():
            fields = line.split()
            if len(fields) >= 2 and fields[0] == "inet":
                found.append(fields[1].split("/", 1)[0])
    return eligible_lan_ipv4(found)


def default_route_ipv4() -> str | None:
    """Return the IPv4 address the host would use on its default route.

    A UDP connect selects a source address without sending a packet. This is
    more useful than choosing the first interface when a workstation also has
    a VPN, virtual machine, or other private network interface.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("192.0.2.1", 9))
        address = sock.getsockname()[0]
    except OSError:
        return None
    finally:
        sock.close()
    eligible = eligible_lan_ipv4([address])
    return eligible[0] if eligible else None


def choose_workstation_ipv4(
    addresses: Iterable[str], *, route_address: str | None = None,
    discover_route: bool = True,
) -> str:
    """Choose the LAN address that a desk kit should use to reach this host."""
    candidates = eligible_lan_ipv4(addresses)
    route = (
        route_address
        if route_address is not None
        else (default_route_ipv4() if discover_route else None)
    )
    if route in candidates:
        return route
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise Lab02WorkflowConfigError(
            "could not find an eligible LAN IPv4 address for LAB-02; "
            "connect this workstation to the desk Wi-Fi/Ethernet or set "
            "LABS_LAB02_CLIENT_URL=http://<workstation-ip>:8765"
        )
    detected = ", ".join(candidates)
    raise Lab02WorkflowConfigError(
        "could not choose a workstation LAN IPv4 address for LAB-02 "
        "(ambiguous private interfaces); "
        f"detected {detected}. Set "
        "LABS_LAB02_CLIENT_URL=http://<workstation-ip>:8765 to choose one"
    )


def _positive_int(value: object) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise Lab02WorkflowConfigError("invalid LAB-02 host configuration")
    return value


def _parse_client_url(value: str, expected_port: int) -> tuple[str, str]:
    """Validate an explicit client URL and return (canonical URL, IPv4 host)."""
    try:
        parsed = urlsplit(value)
        if (
            parsed.scheme != "http"
            or "@" in parsed.netloc
            or parsed.password is not None
            or "?" in value
            or "#" in value
            or parsed.path not in ("", "/")
            or not parsed.hostname
        ):
            raise ValueError
        address = ipaddress.ip_address(parsed.hostname)
        if not isinstance(address, ipaddress.IPv4Address):
            raise ValueError
        port = parsed.port or 80
        if port != expected_port:
            raise ValueError
        canonical = f"http://{address}:{port}"
        return canonical, str(address)
    except (ValueError, UnicodeError):
        raise Lab02WorkflowConfigError(
            "LABS_LAB02_CLIENT_URL must be http://<workstation-ip>:8765 "
            "with an IPv4 address and no path, query, or credentials"
        ) from None


def load_lab02_workflow_config(
    project: ProjectConfig | None = None,
    *,
    interface_addresses: Iterable[str] | None = None,
) -> Lab02WorkflowConfig:
    """Load registry workflow defaults and the separate host client URL."""
    cfg = project if project is not None else ProjectConfig()
    try:
        registry_path = cfg.path_value("labs.registry")
        document = yaml.safe_load(registry_path.read_text(encoding="utf-8"))
        if not isinstance(document, dict):
            raise ValueError
        validate_document_schema(document, project=cfg)
        row = document["labs"]["lab02"]["workflow"]
        server_path = Path(row["server_path"])
        if server_path.is_absolute() or ".." in server_path.parts:
            raise ValueError
        server_path = (ROOT / server_path).resolve()
        if not server_path.is_file():
            raise ValueError
        bind_port = _positive_int(row["bind_port"])
        fixture_size = _positive_int(row["fixture_size_default"])
        if fixture_size not in _FIXTURE_SIZES:
            raise ValueError
        scenario = row["scenario_default"]
        if not isinstance(scenario, str) or scenario not in _SCENARIOS:
            raise ValueError
        deadlines = row["deadlines_seconds"]
        required_deadlines = {"startup", "health", "seed", "readiness", "action", "cleanup"}
        if set(deadlines) != required_deadlines:
            raise ValueError
        deadlines = {key: _positive_int(value) for key, value in deadlines.items()}
        raw_bind = row["bind_host"]
        addresses = eligible_lan_ipv4(
            interface_addresses if interface_addresses is not None else enumerate_interface_ipv4()
        )
        if raw_bind == "auto":
            configured_url = cfg.text("labs.lab02.client_url", default="").strip()
            if configured_url and configured_url.lower() != "auto":
                explicit_url = configured_url
                client_url, address = _parse_client_url(explicit_url, bind_port)
                if address not in addresses:
                    raise Lab02WorkflowConfigError(
                        f"LABS_LAB02_CLIENT_URL points to {address}, but that address "
                        "is not assigned to this workstation; use auto discovery or "
                        "set an address from the current LAN interfaces"
                    )
            else:
                # Tests and callers that provide a snapshot of interfaces must
                # remain deterministic; live route discovery is for production
                # enumeration only.
                address = choose_workstation_ipv4(
                    addresses, discover_route=interface_addresses is None
                )
                client_url = f"http://{address}:{bind_port}"
            # Listen on all interfaces; firmware still uses client_url (desk LAN IP).
            bind_host = "0.0.0.0"
        else:
            try:
                bind_ip = ipaddress.ip_address(raw_bind)
            except (ValueError, TypeError):
                raise Lab02WorkflowConfigError("invalid LAB-02 bind configuration") from None
            if not isinstance(bind_ip, ipaddress.IPv4Address) or str(bind_ip) not in addresses:
                raise Lab02WorkflowConfigError("invalid LAB-02 bind configuration")
            bind_host = str(bind_ip)
            explicit_url = cfg.text("labs.lab02.client_url", default="")
            if explicit_url:
                client_url, address = _parse_client_url(explicit_url, bind_port)
                if address != bind_host:
                    raise Lab02WorkflowConfigError("invalid LAB-02 client URL configuration")
            else:
                client_url = f"http://{bind_host}:{bind_port}"
        return Lab02WorkflowConfig(
            server_path=server_path,
            bind_host=bind_host,
            bind_port=bind_port,
            fixture_size_default=fixture_size,
            scenario_default=scenario.strip(),
            deadlines_seconds=deadlines,
            client_url=client_url,
        )
    except Lab02WorkflowConfigError:
        raise
    except (OSError, KeyError, TypeError, ValueError, yaml.YAMLError, ProjectConfigError, LabRegistryError):
        raise Lab02WorkflowConfigError("invalid LAB-02 host workflow configuration") from None
