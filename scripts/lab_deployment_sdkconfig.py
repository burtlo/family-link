"""Map deployment secrets profiles and desk lab URLs into ESP-IDF sdkconfig.defaults fragments."""

from __future__ import annotations

import hashlib
from pathlib import Path

from deployment_config import Deployment, Kit


def sdkconfig_quoted_value(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def deployment_kit_sdkconfig_lines(deployment: Deployment, kit: Kit) -> list[str]:
    lines: list[str] = []
    if deployment.lab.phaseb_server_url:
        lines.append(
            f"CONFIG_LAB02_SERVER_URL={sdkconfig_quoted_value(deployment.lab.phaseb_server_url)}"
        )
    profile = deployment.secrets_profile_for_kit(kit)
    if profile and profile.wifi_ssid:
        lines.append(f"CONFIG_LAB02_WIFI_SSID={sdkconfig_quoted_value(profile.wifi_ssid)}")
        lines.append(
            f"CONFIG_LAB02_WIFI_PASSWORD={sdkconfig_quoted_value(profile.wifi_password)}"
        )
    return lines


def deployment_kit_sdkconfig_digest(deployment: Deployment, kit: Kit) -> str:
    profile = deployment.secrets_profile_for_kit(kit)
    payload = "\n".join(
        [
            kit.id,
            kit.secrets_profile,
            profile.wifi_ssid if profile else "",
            profile.wifi_password if profile else "",
            deployment.lab.phaseb_server_url,
        ]
    )
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def write_deployment_kit_sdkconfig(
    build_dir: Path,
    kit: Kit,
    deployment: Deployment,
) -> Path | None:
    """Write sdkconfig.defaults.deployment.kit; invalidate sdkconfig when content changes."""
    lines = deployment_kit_sdkconfig_lines(deployment, kit)
    if not lines:
        return None
    build_dir.mkdir(parents=True, exist_ok=True)
    path = build_dir / "sdkconfig.defaults.deployment.kit"
    meta = build_dir / "sdkconfig.defaults.deployment.kit.sha256"
    content = "\n".join(lines) + "\n"
    digest = deployment_kit_sdkconfig_digest(deployment, kit)
    if meta.is_file() and meta.read_text(encoding="utf-8").strip() == digest and path.is_file():
        return path
    sdkconfig = build_dir / "sdkconfig"
    if sdkconfig.is_file() and (not meta.is_file() or meta.read_text(encoding="utf-8").strip() != digest):
        print(f"Removing stale {sdkconfig} (deployment kit secrets changed for {kit.id})")
        sdkconfig.unlink()
        old = build_dir / "sdkconfig.old"
        if old.is_file():
            old.unlink()
    path.write_text(content, encoding="utf-8")
    meta.write_text(digest + "\n", encoding="utf-8")
    print(f"Deployment Phase B sdkconfig for kit {kit.id} -> {path}")
    return path


def resolve_kit_for_lab(
    deployment: Deployment,
    kit_name: str,
    port_name: str,
    available_ports,
) -> Kit | None:
    """Resolve kit nickname or exactly one connected kit; None when not inferable."""
    from device_targets import kit_for, select

    if kit_name:
        return kit_for(deployment, kit_name)
    if port_name:
        port_matches = [p for p in available_ports if p.device == port_name]
        if len(port_matches) != 1:
            return None
        serial = port_matches[0].serial
        if not serial:
            return None
        from device_targets import serial_key

        key = serial_key(serial)
        matches = [
            k
            for k in deployment.kits
            if k.usb_serial and serial_key(k.usb_serial) == key
        ]
        return matches[0] if len(matches) == 1 else None
    try:
        targets = select(deployment, available_ports, "", False)
    except Exception:
        return None
    return targets[0][0] if targets else None
