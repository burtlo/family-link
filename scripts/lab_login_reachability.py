"""Point login-lab kit builds at the workstation address already chosen for the bounded-inbox lab."""
from __future__ import annotations

import hashlib
from pathlib import Path
from urllib.parse import urlsplit

from deployment_config import Deployment, Kit
from lab_deployment_sdkconfig import sdkconfig_quoted_value
from project_config import ProjectConfig, ProjectConfigError

_PORTS = {"LAB-07": 8767, "LAB-08": 8768, "LAB-09": 8769}
_URLS = {
    "LAB-07": (
        ("CONFIG_LAB07_HEALTH_URL", "/health"),
        ("CONFIG_LAB07_ROSTER_URL", "/roster?script=eight"),
        ("CONFIG_LAB07_HOLD_URL", "/roster?script=delayed"),
    ),
    "LAB-08": (
        ("CONFIG_LAB08_PIN_URL", "/pin"),
        ("CONFIG_LAB08_OBSERVE_URL", "/observe"),
        ("CONFIG_LAB08_MALFORMED_URL", "/malformed"),
    ),
    "LAB-09": (("CONFIG_LAB09_BASE_URL", ""),),
}
_WIFI = {
    "LAB-07": ("CONFIG_LAB07_WIFI_SSID", "CONFIG_LAB07_WIFI_PASSWORD"),
    "LAB-08": ("CONFIG_LAB08_WIFI_SSID", "CONFIG_LAB08_WIFI_PASSWORD"),
    "LAB-09": ("CONFIG_LAB09_WIFI_SSID", "CONFIG_LAB09_WIFI_PASSWORD"),
}


def login_lab_assignments(lab_id: str, origin: str, ssid: str, password: str) -> list[str]:
    """Sdkconfig assignments for one login lab. origin is http://<lan-ipv4>:<port>."""
    if lab_id not in _URLS:
        raise ProjectConfigError(f"unsupported login lab {lab_id}")
    if not ssid or not password:
        raise ProjectConfigError(f"{lab_id} kit build requires the desk Wi-Fi credentials")
    lines = [f"{key}={sdkconfig_quoted_value(origin + path)}" for key, path in _URLS[lab_id]]
    ssid_key, password_key = _WIFI[lab_id]
    lines.append(f"{ssid_key}={sdkconfig_quoted_value(ssid)}")
    lines.append(f"{password_key}={sdkconfig_quoted_value(password)}")
    return lines


def workstation_origin(config: ProjectConfig, port: int) -> str:
    """Reuse the bounded-inbox workstation IPv4 and substitute this lab's fixture port."""
    from lab02_workflow_config import load_lab02_workflow_config

    workflow = load_lab02_workflow_config(config)
    parts = urlsplit(workflow.client_url)
    host = parts.hostname or ""
    if parts.scheme != "http" or not host or parts.username or parts.path not in ("", "/"):
        raise ProjectConfigError("bounded-inbox client URL is not a plain http IPv4 origin")
    if host == "0.0.0.0" or host.startswith("127."):
        raise ProjectConfigError(
            "kit build needs the workstation LAN address chosen for the bounded-inbox lab"
        )
    return f"http://{host}:{port}"


def _defaults_path(build_dir: Path, kit_id: str) -> Path:
    return build_dir.parent / f"{build_dir.name}.deployment.{kit_id}.login.defaults"


def write_login_lab_defaults(build_dir: Path, kit_id: str, assignments: list[str], digest: str) -> Path:
    """Write the overlay beside the build dir and drop a stale sdkconfig when it changes."""
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    path = _defaults_path(build_dir, kit_id)
    meta = Path(str(path) + ".sha256")
    content = "\n".join(assignments) + "\n"
    if meta.is_file() and meta.read_text(encoding="utf-8").strip() == digest and path.is_file():
        return path
    sdkconfig = build_dir / "sdkconfig"
    if sdkconfig.is_file():
        print(f"Removing stale {sdkconfig} (desk fixture address changed)")
        sdkconfig.unlink()
        (build_dir / "sdkconfig.old").unlink(missing_ok=True)
    path.write_text(content, encoding="utf-8")
    meta.write_text(digest + "\n", encoding="utf-8")
    return path


def write_login_lab_overlay(
    build_dir: Path,
    lab_id: str,
    kit: Kit,
    deployment: Deployment,
    config: ProjectConfig,
) -> Path:
    port = _PORTS.get(lab_id)
    if port is None:
        raise ProjectConfigError(f"unsupported login lab {lab_id}")
    profile = deployment.secrets_profile_for_kit(kit)
    if profile is None or not profile.wifi_ssid or not profile.wifi_password:
        raise ProjectConfigError(
            f"{lab_id} kit build requires the per-kit Wi-Fi credentials used by the bounded-inbox lab"
        )
    origin = workstation_origin(config, port)
    assignments = login_lab_assignments(lab_id, origin, profile.wifi_ssid, profile.wifi_password)
    digest = hashlib.sha256(
        "\n".join([kit.id, origin, profile.wifi_ssid, profile.wifi_password]).encode("utf-8")
    ).hexdigest()
    print(f"{lab_id} kit fixture {origin}")
    return write_login_lab_defaults(build_dir, kit.id, assignments, digest)
