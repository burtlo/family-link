#!/usr/bin/env python3
"""Build and flash BOX-3 demos via ESP-IDF.

Finds IDF (IDF_PATH, ~/esp/esp-idf, or repo .tools/esp-idf), picks the USB
port the same way `make connect` does, then runs idf.py build/flash/monitor.

  python scripts/flash.py --demo h02
  python scripts/flash.py --demo h02 --monitor
  python scripts/flash.py --idf-install
  python scripts/flash.py --example display_audio_photo   # h01

Do not flash through the dock USB-C (power only).
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "firmware"
TOOLS = ROOT / ".tools"
IDF_CLONE_DEFAULT = Path.home() / "esp" / "esp-idf"
IDF_VERSION = os.environ.get("FAMILY_IDF_VERSION", "v5.4.2")

# Short ids used by `make h02`. Values are firmware/demos/<name>.c
# except h01, which is Espressif's BSP example.
DEMOS = {
    "h01": None,  # example, not our tree
    "h02": "h02_display_count",
    "h03": "h03_touch_pin",
    "h04": "h04_mute_ptt",
    "h05": "h05_loopback",
    "h06": "h06_wifi_join",
    "h07": "h07_http_me",
    "h08": "h08_record_upload",
    "h09": "h09_download_play",
    "h10": "h10_playhead_reboot",
    "h11": "h11_hangout_ptt",
    "h12": "h12_live_screen",
    "h13": "h13_show_photo",
    "h14": "h14_heartbeat_inbox",
    "h15": "h15_text_after_pin",
    "h16": "h16_https_me",
    "x01": "x01_product_shell",
    "p01": "p01_static_face",
    "p02": "p02_idle_life",
    "p03": "p03_moods",
    "p04": "p04_transitions",
    "p05": "p05_press_react",
    "p06": "p06_sfx",
    "p07": "p07_pet_loop",
    "p08": "p08_notice",
    "p09": "p09_talk_or_freeze",
    "p10": "p10_portrait",
    "p11": "p11_greeting",
}

H01_EXAMPLE = "espressif/esp-box-3:display_audio_photo"


class FlashError(Exception):
    pass


def _import_device():
    sys.path.insert(0, str(ROOT / "scripts"))
    import device  # noqa: E402

    return device


def find_idf() -> Path:
    env = os.environ.get("IDF_PATH")
    candidates = []
    if env:
        candidates.append(Path(env))
    candidates.extend(
        [
            IDF_CLONE_DEFAULT,
            TOOLS / "esp-idf",
            Path("/opt/esp/idf"),
        ]
    )
    for path in candidates:
        if (path / "tools" / "idf.py").is_file() or (path / "export.sh").is_file():
            return path.resolve()
    raise FlashError(
        "ESP-IDF not found. Run: make idf-install\n"
        "  (clones ESP-IDF into ~/esp/esp-idf and runs install.sh esp32s3)\n"
        "Or set IDF_PATH to an existing tree."
    )


def bash_idf(idf: Path, inner: str, extra_env: dict[str, str] | None = None) -> int:
    """Run a command with IDF export.sh loaded (required on macOS/Linux)."""
    env = os.environ.copy()
    env["IDF_PATH"] = str(idf)
    if extra_env:
        env.update(extra_env)
    export = idf / "export.sh"
    if not export.is_file():
        raise FlashError(f"missing {export}")
    quoted = inner.replace("'", "'\\''")
    # Do not use bash -l: macOS login shells put /usr/bin/python3 (3.9) first,
    # while install.sh created the venv for Homebrew 3.14.
    cmd = f"set -euo pipefail; source '{export}'; {quoted}"
    print(f"-> idf: {idf}", flush=True)
    print(f"-> {inner}", flush=True)
    return subprocess.run(["bash", "-c", cmd], cwd=str(ROOT), env=env).returncode


def cmd_idf_install(dest: Path) -> int:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not (dest / "export.sh").is_file():
        print(f"-> git clone --progress --depth 1 --branch {IDF_VERSION} → {dest}", flush=True)
        rc = subprocess.run(
            [
                "git",
                "clone",
                "--progress",
                "--depth",
                "1",
                "--branch",
                IDF_VERSION,
                "https://github.com/espressif/esp-idf.git",
                str(dest),
            ]
        ).returncode
        if rc != 0:
            raise FlashError("git clone of esp-idf failed (network / disk?)")
        print("-> git submodule update --init --depth 1 --progress", flush=True)
        rc = subprocess.run(
            [
                "git",
                "submodule",
                "update",
                "--init",
                "--depth",
                "1",
                "--progress",
                "--jobs",
                "8",
            ],
            cwd=str(dest),
        ).returncode
        if rc != 0:
            raise FlashError("esp-idf submodule update failed")
    install = dest / "install.sh"
    if not install.is_file():
        raise FlashError(f"no install.sh in {dest}")
    print("-> ./install.sh esp32s3 (downloads the xtensa-esp32s3 compiler)", flush=True)
    rc = subprocess.run(["bash", str(install), "esp32s3"], cwd=str(dest)).returncode
    if rc != 0:
        raise FlashError("IDF install.sh failed")
    print()
    print(f"Installed. IDF_PATH={dest}")
    print("Next: make flash DEMO=h02")
    return 0


def pick_serial_port() -> str:
    device = _import_device()
    try:
        return device.pick_port().port
    except device.TaskError as exc:
        raise FlashError(str(exc)) from exc


def ensure_secrets() -> None:
    example = FIRMWARE / "secrets.example.h"
    dest = FIRMWARE / "secrets.h"
    if dest.is_file() or not example.is_file():
        return
    dest.write_text(example.read_text(encoding="utf-8"), encoding="utf-8")
    print(f"-> copied {example.name} → secrets.h (edit SSID/token; gitignored)")


def cmd_flash(demo: str, monitor: bool, port: str | None, build_only: bool) -> int:
    if demo == "h01":
        return cmd_flash_h01(monitor=monitor, port=port)

    name = DEMOS.get(demo, demo)
    if name is None:
        raise FlashError(f"demo {demo} is not a firmware file (h01 is --example)")
    src = FIRMWARE / "demos" / f"{name}.c"
    if not src.is_file():
        raise FlashError(f"missing {src}. That demo is not in the tree yet.")

    idf = find_idf()
    ensure_secrets()
    build_dir = FIRMWARE / "build" / name
    build_dir.mkdir(parents=True, exist_ok=True)

    opts = f'-D FAMILY_DEMO={name} -B "{build_dir}" -C "{FIRMWARE}"'
    if build_only:
        inner = f"idf.py {opts} build"
    else:
        serial = port or pick_serial_port()
        inner = f'idf.py {opts} -p "{serial}" build flash'
        if monitor:
            inner += " monitor"
    rc = bash_idf(idf, inner)
    if rc != 0:
        raise FlashError(f"idf.py failed for {demo} (exit {rc})")
    return 0


def cmd_flash_h01(monitor: bool, port: str | None) -> int:
    """Espressif BSP example display_audio_photo — not our firmware."""
    idf = find_idf()
    work = TOOLS / "h01_display_audio_photo"
    if not (work / "CMakeLists.txt").is_file():
        work.parent.mkdir(parents=True, exist_ok=True)
        print(f"-> idf.py create-project-from-example {H01_EXAMPLE}")
        rc = bash_idf(
            idf,
            f'cd "{work.parent}" && idf.py create-project-from-example "{H01_EXAMPLE}"',
        )
        if rc != 0:
            raise FlashError(
                "could not fetch display_audio_photo. "
                "Check the network and that ESP-IDF component manager works."
            )
        # Example folder name varies; find CMakeLists one level down.
        if not (work / "CMakeLists.txt").is_file():
            found = list(work.parent.glob("**/display_audio_photo/CMakeLists.txt"))
            if not found:
                raise FlashError(f"example extracted but CMakeLists.txt not under {work.parent}")
            work = found[0].parent
    serial = port or pick_serial_port()
    inner = f'idf.py -C "{work}" -p "{serial}" build flash'
    if monitor:
        inner += " monitor"
    rc = bash_idf(idf, inner)
    if rc != 0:
        raise FlashError(f"h01 flash failed (exit {rc})")
    return 0


def cmd_monitor(port: str | None) -> int:
    idf = find_idf()
    serial = port or pick_serial_port()
    # Prefer idf.py monitor so USB-JTAG reset works; fall back to pyserial.
    rc = bash_idf(idf, f'idf.py -p "{serial}" monitor')
    if rc == 0:
        return 0
    print("idf.py monitor failed; trying 115200 serial capture (Ctrl-C to quit)")
    device = _import_device()
    try:
        import serial
    except ImportError as exc:
        raise FlashError("pyserial missing. make install") from exc
    with serial.Serial(serial, 115200, timeout=0.2) as ser:
        while True:
            chunk = ser.read(4096)
            if chunk:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
    return 0


def known_demos() -> str:
    lines = []
    for key, val in DEMOS.items():
        if key == "h01":
            lines.append("h01  Espressif display_audio_photo (BSP example)")
        else:
            path = FIRMWARE / "demos" / f"{val}.c"
            mark = "ok" if path.is_file() else "missing"
            lines.append(f"{key}  {val}.c  [{mark}]")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Flash BOX-3 demos (ESP-IDF).")
    parser.add_argument("--demo", help="Short id (h02, h05, p01, …) or C file stem")
    parser.add_argument("--example", help="Flash Espressif example (h01 uses display_audio_photo)")
    parser.add_argument("--monitor", action="store_true", help="Attach serial monitor after flash")
    parser.add_argument("--build-only", action="store_true", help="Compile, do not flash")
    parser.add_argument("--idf-install", action="store_true", help="Clone ESP-IDF and install esp32s3 tools")
    parser.add_argument("--idf-dir", type=Path, default=IDF_CLONE_DEFAULT, help="Clone destination")
    parser.add_argument("--list", action="store_true", help="List demo ids")
    parser.add_argument("--port", help="Override serial port (or PORT= / ESPPORT=)")
    args = parser.parse_args(argv)

    try:
        if args.list:
            print(known_demos())
            return 0
        if args.idf_install:
            return cmd_idf_install(args.idf_dir.expanduser())
        if args.example:
            if args.example != "display_audio_photo":
                raise FlashError("only display_audio_photo is wired as h01 for now")
            return cmd_flash_h01(monitor=args.monitor, port=args.port)
        if args.demo:
            return cmd_flash(
                args.demo,
                monitor=args.monitor,
                port=args.port,
                build_only=args.build_only,
            )
        parser.print_help()
        print("\nDemos:\n" + known_demos())
        return 2
    except FlashError as exc:
        print(f"  [fail] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
