"""LAB-16 Wi-Fi and diary-server overlay (h22-compatible upload path)."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import socket

from lab_deployment_sdkconfig import sdkconfig_quoted_value
from project_config import ProjectConfigError


def client_address() -> str:
    override = os.environ.get("LAB16_CLIENT_ADDRESS")
    if override:
        import ipaddress
        ipaddress.IPv4Address(override)
        return override
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("192.0.2.1", 9))
        return sock.getsockname()[0]
    finally:
        sock.close()


def _token_for_kit(deployment, kit) -> str:
    endpoint_id = kit.endpoint_id
    for endpoint in deployment.endpoints:
        if endpoint.id == endpoint_id:
            return endpoint.token
    return "lab16-desk-token-v1"


def _overlay_paths(build_dir: Path) -> tuple[Path, Path]:
    overlay = build_dir.parent / f"{build_dir.name}.lab16.defaults"
    return overlay, overlay.with_suffix(overlay.suffix + ".sha256")


def prepare(host, config, spec, variant, build_dir, kit="", target=None):
    del host, config, spec, variant
    if target is None and not kit:
        return None
    deployment = target.deployment if target is not None else None
    resolved = target.kit if target is not None else None
    if deployment is None or resolved is None:
        raise ProjectConfigError("LAB-16 build needs the resolved kit")
    profile = deployment.secrets_profile_for_kit(resolved)
    if not profile or not profile.wifi_ssid or not profile.wifi_password:
        raise ProjectConfigError("LAB-16 needs per-kit Wi-Fi in the deployment roster")
    address = client_address()
    token = _token_for_kit(deployment, resolved)
    lines = [
        f"CONFIG_LAB16_SERVER_HOST={sdkconfig_quoted_value(address)}",
        f"CONFIG_LAB16_WIFI_SSID={sdkconfig_quoted_value(profile.wifi_ssid)}",
        f"CONFIG_LAB16_WIFI_PASSWORD={sdkconfig_quoted_value(profile.wifi_password)}",
        f"CONFIG_LAB16_DEVICE_TOKEN={sdkconfig_quoted_value(token)}",
    ]
    digest = hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest()
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    overlay, meta = _overlay_paths(build_dir)
    previous = meta.read_text(encoding="utf-8").strip() if meta.is_file() else ""
    if previous != digest:
        sdkconfig = build_dir / "sdkconfig"
        if sdkconfig.is_file():
            print(f"Removing stale {sdkconfig} (LAB-16 station target changed)")
            sdkconfig.unlink()
    content = "\n".join(lines) + "\n"
    flags = os.O_CREAT | os.O_WRONLY | os.O_TRUNC
    fd = os.open(overlay, flags, 0o600)
    try:
        os.chmod(overlay, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            fd = None
            out.write(content)
    finally:
        if fd is not None:
            os.close(fd)
    meta.write_text(digest + "\n", encoding="utf-8")
    print(f"LAB-16 diary target {address}:8776 for kit {resolved.id}")
    return overlay
