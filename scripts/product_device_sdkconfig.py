"""Desk kit secrets + P1 wire URL for product device sdkconfig overlays."""

from __future__ import annotations

import hashlib
import json
import os
import socket
from pathlib import Path
from urllib.parse import urlsplit

from deployment_config import Deployment, Kit, load_active_deployment
from device_targets import kit_for
from lab02_workflow_config import choose_workstation_ipv4
from lab_deployment_sdkconfig import deployment_kit_defaults_path, sdkconfig_quoted_value
from project_config import ProjectConfig, ProjectConfigError, ROOT

P1_FIXTURE_ACCOUNTS = ROOT / "server/device/fixtures/accounts.json"
P1_DEFAULT_PORT = 8777


def _lan_ipv4_addresses() -> list[str]:
    addresses: list[str] = []
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addr = info[4][0]
            if addr and addr not in addresses:
                addresses.append(addr)
    except OSError:
        pass
    return addresses


def _normalize_p1_base_url(raw: str) -> str:
    text = raw.strip().rstrip("/")
    if not text:
        raise ProjectConfigError("P1 base URL override is empty")
    if not text.startswith("http://") and not text.startswith("https://"):
        text = f"http://{text}"
    parts = urlsplit(text)
    if parts.port is None:
        host = parts.hostname or ""
        return f"{parts.scheme}://{host}:{P1_DEFAULT_PORT}"
    return text


def p1_base_url_for_host() -> str:
    for key in ("FAMILY_P1_HOST", "PRODUCT_P1_HOST", "P1_HOST"):
        override = os.environ.get(key, "").strip()
        if override:
            return _normalize_p1_base_url(override)
    address = choose_workstation_ipv4(_lan_ipv4_addresses())
    return f"http://{address}:{P1_DEFAULT_PORT}"


def load_p1_desk_token() -> str:
    data = json.loads(P1_FIXTURE_ACCOUNTS.read_text(encoding="utf-8"))
    token = data.get("desk_token", "")
    if not isinstance(token, str) or not token.strip():
        raise ProjectConfigError("P1 fixture accounts.json missing desk_token")
    return token.strip()


def product_kit_sdkconfig_lines(deployment: Deployment, kit: Kit) -> list[str]:
    profile = deployment.secrets_profile_for_kit(kit)
    if profile is None or not profile.wifi_ssid:
        raise ProjectConfigError(
            f"Kit {kit.id} needs secrets_profile Wi-Fi in deployment local.yaml "
            "(wifi_ssid / wifi_password)"
        )
    lines = [
        f"CONFIG_FAMILY_P1_BASE_URL={sdkconfig_quoted_value(p1_base_url_for_host())}",
        f"CONFIG_FAMILY_DESK_TOKEN={sdkconfig_quoted_value(load_p1_desk_token())}",
        f"CONFIG_FAMILY_WIFI_SSID={sdkconfig_quoted_value(profile.wifi_ssid)}",
        f"CONFIG_FAMILY_WIFI_PASSWORD={sdkconfig_quoted_value(profile.wifi_password)}",
    ]
    lines.extend(diagnostic_sdkconfig_lines())
    return lines


def _env_flag(name: str) -> bool:
    return os.environ.get(name, "").strip().lower() in ("1", "y", "yes", "true")


def diagnostic_sdkconfig_lines() -> list[str]:
    """Desk-only diagnostics, opted in per build through the environment."""
    if not _env_flag("FAMILY_DEVICE_MEM_DIAGNOSTIC"):
        return []
    lines = [
        "CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC=y",
        "CONFIG_LV_USE_LOG=y",
        "CONFIG_LV_LOG_LEVEL_WARN=y",
        "CONFIG_LV_LOG_PRINTF=y",
    ]
    if _env_flag("FAMILY_DEVICE_DIAG_SKIP_RING"):
        lines.append("CONFIG_FAMILY_DEVICE_DIAG_SKIP_RING=y")
    return lines


def write_product_kit_sdkconfig(build_dir: Path, kit: Kit, deployment: Deployment) -> Path:
    lines = product_kit_sdkconfig_lines(deployment, kit)
    build_dir.mkdir(parents=True, exist_ok=True)
    path = deployment_kit_defaults_path(build_dir, kit)
    content = "\n".join(lines) + "\n"
    digest = hashlib.sha256(content.encode("utf-8")).hexdigest()
    meta = path.with_suffix(path.suffix + ".sha256")
    sdkconfig = build_dir / "sdkconfig"
    if sdkconfig.is_file() and (not meta.is_file() or meta.read_text(encoding="utf-8").strip() != digest):
        print(f"Removing stale {sdkconfig} (product kit defaults changed for {kit.id})")
        sdkconfig.unlink()
        old = build_dir / "sdkconfig.old"
        if old.is_file():
            old.unlink()
    path.write_text(content, encoding="utf-8")
    meta.write_text(digest + "\n", encoding="utf-8")
    print(f"Product desk sdkconfig for kit {kit.id} -> {path}")
    print(f"  P1 URL {p1_base_url_for_host()} (server on port {P1_DEFAULT_PORT})")
    return path


def apply_product_kit_build(config: ProjectConfig, kit_nickname: str) -> list[Path]:
    deployment = load_active_deployment(config)
    kit = kit_for(deployment, kit_nickname)
    build_dir = config.path_value("firmware.build")
    source = config.path_value("firmware.source")
    defaults = [source / "sdkconfig.defaults"]
    fragment = write_product_kit_sdkconfig(build_dir, kit, deployment)
    return defaults + [fragment]
