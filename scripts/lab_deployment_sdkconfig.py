"""Map deployment secrets profiles and desk lab URLs into ESP-IDF sdkconfig.defaults fragments."""

from __future__ import annotations

import hashlib
from pathlib import Path

from deployment_config import Deployment, Kit


def sdkconfig_quoted_value(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def deployment_kit_defaults_path(build_dir: Path, kit: Kit) -> Path:
    """Outside build_dir so ESP-IDF set-target/fullclean never sees a dirty tree."""
    return build_dir.parent / f"{build_dir.name}.deployment.{kit.id}.defaults"


def deployment_defaults_sidecars(build_dir: Path) -> list[Path]:
    parent = build_dir.parent
    prefix = f"{build_dir.name}.deployment."
    if not parent.is_dir():
        return []
    return sorted(p for p in parent.iterdir() if p.name.startswith(prefix))


def deployment_kit_sdkconfig_lines(
    deployment: Deployment, kit: Kit, client_url: str, *, fixture_size: int = 64,
    scenario: str = "nominal", scripted: bool = False, build_id: str = "",
) -> list[str]:
    if not client_url:
        raise ValueError("validated LAB-02 client URL is required")
    lines: list[str] = [
        f"CONFIG_LAB02_SERVER_URL={sdkconfig_quoted_value(client_url)}",
        f"CONFIG_LAB02_FIXTURE_{fixture_size}=y",
        f"CONFIG_LAB02_SCENARIO={sdkconfig_quoted_value(scenario)}",
        f"CONFIG_LAB02_BUILD_ID={sdkconfig_quoted_value(build_id)}",
        f"CONFIG_LAB_AUTO_HARNESS={'y' if scripted else 'n'}",
    ]
    profile = deployment.secrets_profile_for_kit(kit)
    if profile and profile.wifi_ssid:
        lines.append(f"CONFIG_LAB02_WIFI_SSID={sdkconfig_quoted_value(profile.wifi_ssid)}")
        lines.append(
            f"CONFIG_LAB02_WIFI_PASSWORD={sdkconfig_quoted_value(profile.wifi_password)}"
        )
    return lines


def deployment_kit_sdkconfig_digest(
    deployment: Deployment, kit: Kit, client_url: str, *, fixture_size: int = 64,
    scenario: str = "nominal", scripted: bool = False, build_id: str = "",
) -> str:
    profile = deployment.secrets_profile_for_kit(kit)
    payload = "\n".join(
        [
            kit.id,
            kit.secrets_profile,
            profile.wifi_ssid if profile else "",
            profile.wifi_password if profile else "",
            client_url,
            str(fixture_size), scenario, str(scripted), build_id,
        ]
    )
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def write_deployment_kit_sdkconfig(
    build_dir: Path,
    kit: Kit,
    deployment: Deployment,
    client_url: str,
    *, fixture_size: int = 64, scenario: str = "nominal", scripted: bool = False,
    build_id: str = "",
) -> Path:
    """Write kit deployment defaults beside build_dir; invalidate sdkconfig when content changes."""
    lines = deployment_kit_sdkconfig_lines(deployment, kit, client_url, fixture_size=fixture_size,
                                           scenario=scenario, scripted=scripted, build_id=build_id)
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    path = deployment_kit_defaults_path(build_dir, kit)
    meta = path.with_suffix(path.suffix + ".sha256")
    content = "\n".join(lines) + "\n"
    digest = deployment_kit_sdkconfig_digest(deployment, kit, client_url, fixture_size=fixture_size,
                                             scenario=scenario, scripted=scripted, build_id=build_id)
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
