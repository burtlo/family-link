"""Generate safe, derived LAB-02 experiment defaults for local/eager builds."""
from __future__ import annotations

import hashlib
from pathlib import Path


def write_lab02_local_overlay(build_dir: Path, *, fixture_size: int,
                              scenario: str, scripted: bool, build_id: str = "") -> Path:
    """Freeze local backend and experiment flags without private deployment values."""
    if fixture_size not in {8, 16, 64, 256} or not scenario:
        raise ValueError("invalid LAB-02 experiment overlay")
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    path = build_dir.parent / f"{build_dir.name}.experiment.defaults"
    lines = [
        'CONFIG_LAB02_SERVER_URL=""',
        'CONFIG_LAB02_WIFI_SSID=""',
        'CONFIG_LAB02_WIFI_PASSWORD=""',
        f"CONFIG_LAB02_FIXTURE_{fixture_size}=y",
        f'CONFIG_LAB02_SCENARIO="{scenario}"',
        f'CONFIG_LAB02_BUILD_ID="{build_id}"',
        f"CONFIG_LAB_AUTO_HARNESS={'y' if scripted else 'n'}",
    ]
    content = "\n".join(lines) + "\n"
    digest = hashlib.sha256(content.encode("utf-8")).hexdigest()
    stamp = path.with_suffix(path.suffix + ".sha256")
    changed = not path.is_file() or not stamp.is_file() or stamp.read_text(encoding="utf-8").strip() != digest
    if changed:
        path.write_text(content, encoding="utf-8")
        stamp.write_text(digest + "\n", encoding="utf-8")
        sdkconfig = build_dir / "sdkconfig"
        if sdkconfig.is_file():
            sdkconfig.unlink()
    return path
