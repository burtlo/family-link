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
import json
import os
import re
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
    "h17": "h17_button_panel",
    "h18": "h18_playback_screen",
    "h19": "h19_message_list",
    "h20": "h20_presence",
    "h21": "h21_talk",
    "h22": "h22_diary",
    "h23": "h23_record_idle_stop",
    "h24": "h24_device_log",
    "h25": "h25_chipmunk",
    "h26": "h26_draw",
    "h27": "h27_sketch",
    "h28": "h28_video",
    "h29": "h29_pingpong",
    "x01": "x01_product_shell",
    "x02": "x02_product_shell",
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
    "p12": "p12_tamagotchi",
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


WHO_ALIASES = {
    "mazi": "box-a",
    "arlo": "box-b",
    "a": "box-a",
    "b": "box-b",
}

# 31-char unique tags in firmware/common/who.c (32-byte slots with NUL).
WHO_TAGS = {
    "id": "FLWHO/id///////////////////////",
    "token": "FLWHO/token////////////////////",
    "name": "FLWHO/name/////////////////////",
    "peer": "FLWHO/peer/////////////////////",
}

KITS_PATH = ROOT / "kits.local.yaml"

for _tag in WHO_TAGS.values():
    if len(_tag) != 31:
        raise RuntimeError(f"WHO_TAGS must be 31 chars, got {len(_tag)} for {_tag!r}")


V1_PRODUCT_DEMOS = frozenset({"x01_product_shell", "x02_product_shell"})


def resolve_who(
    who: str | None, demo: str | None = None
) -> tuple[str, str, str, str] | None:
    """Return (device_id, token, display_name, peer_name) or None if WHO is unset."""
    if not who or not str(who).strip():
        return None
    key = str(who).strip().lower()
    device_id = WHO_ALIASES.get(key, key)
    use_v1_tokens = demo in V1_PRODUCT_DEMOS
    fallback = {
        "box-a": ("box-a", "change-me-a", "Mazi", "Arlo"),
        "box-b": ("box-b", "change-me-b", "Arlo", "Mazi"),
    }
    v1_tokens = {
        "box-a": "change-me-mazi",
        "box-b": "change-me-arlo",
        "mazi": "change-me-mazi",
        "arlo": "change-me-arlo",
        "lynn": "change-me-lynn",
    }
    try:
        if str(ROOT) not in sys.path:
            sys.path.insert(0, str(ROOT))
        from demos.server._shared.registry import load_devices

        devices = load_devices()
    except Exception:
        devices = None
    if devices and device_id in devices:
        dev = devices[device_id]
        peer = devices.get(dev.peer)
        if peer and peer.name:
            peer_name = peer.name
        elif device_id in fallback:
            peer_name = fallback[device_id][3]
        else:
            peer_name = "friend"
        token = dev.token
        if use_v1_tokens:
            token = v1_tokens.get(key) or v1_tokens.get(device_id) or dev.token
        return dev.id, token, dev.name or dev.id, peer_name
    if device_id in fallback:
        row = list(fallback[device_id])
        if use_v1_tokens and (key in v1_tokens or device_id in v1_tokens):
            row[1] = v1_tokens.get(key) or v1_tokens[device_id]
        return tuple(row)
    raise FlashError(
        f"unknown WHO={who!r}. Try WHO=mazi or WHO=arlo (also box-a / box-b)."
    )


def identity_from_secrets() -> tuple[str, str, str, str]:
    path = FIRMWARE / "secrets.h"
    if not path.is_file():
        path = FIRMWARE / "secrets.example.h"
    text = path.read_text(encoding="utf-8") if path.is_file() else ""

    def grab(key: str, default: str) -> str:
        match = re.search(rf'#define\s+{key}\s+"([^"]*)"', text)
        return match.group(1) if match else default

    device_id = grab("DEMO_DEVICE_ID", "box-a")
    try:
        resolved = resolve_who(device_id)
        if resolved:
            return resolved
    except FlashError:
        pass
    token = grab("DEMO_DEVICE_TOKEN", "change-me-a")
    return device_id, token, device_id, "friend"


def _norm_serial(value: str | None) -> str:
    if not value:
        return ""
    return "".join(c for c in value.upper() if c.isalnum())


def load_kits() -> dict:
    if not KITS_PATH.is_file():
        return {}
    try:
        import yaml

        raw = yaml.safe_load(KITS_PATH.read_text(encoding="utf-8")) or {}
    except Exception:
        return {}
    kits = raw.get("kits") if isinstance(raw, dict) else None
    return dict(kits) if isinstance(kits, dict) else {}


def save_kits(kits: dict) -> None:
    try:
        import yaml
    except ImportError:
        print("-> PyYAML missing; not saving kits.local.yaml (make install-server)")
        return
    KITS_PATH.write_text(
        yaml.safe_dump({"kits": kits}, sort_keys=False),
        encoding="utf-8",
    )


def remember_usb(device_id: str, usb_serial: str | None, port: str) -> None:
    if not usb_serial:
        print(
            "-> USB serial unknown on this port; pass PORT= next time if both kits are plugged in",
            flush=True,
        )
        return
    kits = load_kits()
    row = dict(kits.get(device_id) or {})
    row["usb_serial"] = usb_serial
    row["last_port"] = port
    kits[device_id] = row
    save_kits(kits)
    print(
        f"-> remembered {device_id} as USB serial {usb_serial} (any USB jack next time)",
        flush=True,
    )


def pick_kit_port(device_id: str | None, port: str | None) -> tuple[str, str | None]:
    """Choose a serial port. Prefers remembered USB serial over /dev path."""
    device = _import_device()
    try:
        cands = device.candidate_devices()
    except Exception:
        cands = []

    if not port:
        port = os.environ.get("ESPPORT") or os.environ.get("PORT")

    kits = load_kits()
    want = _norm_serial((kits.get(device_id) or {}).get("usb_serial")) if device_id else ""
    if want:
        for dev in cands:
            if _norm_serial(dev.serial) == want:
                return dev.port, dev.serial
        if not port:
            last = (kits.get(device_id) or {}).get("last_port")
            if last:
                for dev in cands:
                    if dev.port == last:
                        print(
                            f"-> USB serial unreadable; using last port for {device_id}: {last}",
                            flush=True,
                        )
                        return dev.port, dev.serial
            shown = (kits.get(device_id) or {}).get("usb_serial")
            raise FlashError(
                f"{device_id} is not plugged in (USB serial {shown}). "
                "Plug that kit's USB-C (not the dock) into any jack."
            )

    if port:
        for dev in cands:
            if dev.port == port:
                return dev.port, dev.serial
        return port, None

    if device_id and len(cands) > 1:
        mapped = {
            _norm_serial((kits.get(kid) or {}).get("usb_serial"))
            for kid in kits
            if (kits.get(kid) or {}).get("usb_serial")
        }
        unmapped = [d for d in cands if _norm_serial(d.serial) not in mapped]
        if len(unmapped) == 1:
            return unmapped[0].port, unmapped[0].serial
        listed = ", ".join(d.port for d in cands)
        who = device_id
        for alias, kid in WHO_ALIASES.items():
            if kid == device_id:
                who = alias
                break
        raise FlashError(
            f"Several BOX-3 ports ({listed}). Bind this kit once:\n"
            f"  make flash DEMO=h26 WHO={who} PORT=/dev/cu.usbmodem…\n"
            "After that it can move to any USB jack (we remember the USB serial)."
        )

    try:
        picked = device.pick_port(devices=cands or None)
    except device.TaskError as exc:
        raise FlashError(str(exc)) from exc
    return picked.port, picked.serial


def repair_esp_image(path: Path) -> None:
    """Rewrite the ESP32 XOR checksum and SHA-256 after in-place .bin edits.

    The bootloader rejects the app if those are stale (black screen, boot loop:
    ``Checksum failed. Calculated … read …`` / ``Factory app partition is not bootable``).
    esptool's write_flash updates the SHA digest but not this 1-byte checksum.
    """
    import hashlib
    import struct

    raw = bytearray(path.read_bytes())
    if not raw or raw[0] != 0xE9:
        raise FlashError(f"{path.name} is not an ESP image (missing 0xE9 magic)")
    nseg = raw[1]
    append_digest = raw[23] == 1
    off = 24
    checksum = 0xEF
    for _ in range(nseg):
        if off + 8 > len(raw):
            raise FlashError(f"{path.name} truncated in segment table")
        _addr, size = struct.unpack_from("<II", raw, off)
        off += 8
        if off + size > len(raw):
            raise FlashError(f"{path.name} truncated in segment data")
        for b in raw[off : off + size]:
            checksum ^= b
        off += size
    cs_off = off | 15
    if cs_off >= len(raw):
        raise FlashError(f"{path.name} has no room for the image checksum")
    raw[cs_off] = checksum
    if append_digest:
        digest_at = cs_off + 1
        if digest_at + 32 > len(raw):
            raise FlashError(f"{path.name} has no room for the SHA-256 digest")
        digest = hashlib.sha256(bytes(raw[: cs_off + 1])).digest()
        raw[digest_at : digest_at + 32] = digest
    path.write_bytes(raw)


def stamp_who(app_bin: Path, identity: tuple[str, str, str, str]) -> Path:
    """Copy family_link_demo.bin with WHO slots filled. One compile, many kits."""
    data = bytearray(app_bin.read_bytes())
    device_id, token, name, peer = identity
    values = {
        "id": device_id,
        "token": token,
        "name": name,
        "peer": peer,
    }
    for key, tag in WHO_TAGS.items():
        needle = tag.encode("ascii")
        idx = data.find(needle)
        if idx < 0:
            raise FlashError(
                f"who slot {key} missing in {app_bin.name}. Rebuild this demo (who.c tags changed)."
            )
        if data.find(needle, idx + 1) >= 0:
            raise FlashError(f"who slot {key} is not unique in {app_bin.name}")
        raw = values[key].encode("ascii") + b"\0"
        if len(raw) > 32:
            raise FlashError(f"{key}={values[key]!r} is longer than 31 chars")
        data[idx : idx + 32] = raw.ljust(32, b"\0")
    dest = app_bin.with_name("family_link_demo.who.bin")
    dest.write_bytes(data)
    repair_esp_image(dest)
    return dest


def flash_image(idf: Path, build_dir: Path, port: str, app_bin: Path) -> int:
    """write_flash using IDF's flasher_args.json, substituting the (stamped) app."""
    meta_path = build_dir / "flasher_args.json"
    if not meta_path.is_file():
        raise FlashError(f"missing {meta_path} after build")
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    extra = meta.get("extra_esptool_args") or {}
    settings = meta.get("flash_settings") or {}
    files = meta.get("flash_files") or {}
    if not files:
        raise FlashError(f"no flash_files in {meta_path}")
    parts: list[str] = []
    for offset, rel in files.items():
        path = Path(rel)
        if not path.is_absolute():
            path = build_dir / rel
        if Path(rel).name == "family_link_demo.bin":
            path = app_bin
        parts.append(f'{offset} "{path}"')
    chip = extra.get("chip") or "esp32s3"
    before = extra.get("before") or "default_reset"
    after = extra.get("after") or "hard_reset"
    write_args = meta.get("write_flash_args")
    if not write_args:
        mode = settings.get("flash_mode") or "dio"
        freq = settings.get("flash_freq") or "80m"
        write_args = ["--flash_mode", mode, "--flash_freq", freq, "--flash_size", "detect"]
    inner = (
        f'python -m esptool --chip {chip} -p "{port}" -b 460800 '
        f"--before {before} --after {after} write_flash "
        + " ".join(str(a) for a in write_args)
        + " "
        + " ".join(parts)
    )
    return bash_idf(idf, inner)


def cmd_flash(
    demo: str,
    monitor: bool,
    port: str | None,
    build_only: bool,
    who: str | None = None,
) -> int:
    if demo == "h01":
        return cmd_flash_h01(monitor=monitor, port=port)

    name = DEMOS.get(demo, demo)
    if name is None:
        raise FlashError(f"demo {demo} is not a firmware file (h01 is --example)")
    src = FIRMWARE / "demos" / f"{name}.c"
    if not src.is_file():
        raise FlashError(f"missing {src}. That demo is not in the tree yet.")

    identity = resolve_who(who, demo=name) or identity_from_secrets()
    idf = find_idf()
    ensure_secrets()
    # One build dir per demo, not per WHO. Second kit is stamp + flash only.
    build_dir = FIRMWARE / "build" / name
    build_dir.mkdir(parents=True, exist_ok=True)

    opts = f'-D FAMILY_DEMO={name} -B "{build_dir}" -C "{FIRMWARE}"'
    print(
        f"-> identity {identity[2]} ({identity[0]}), peer {identity[3]}",
        flush=True,
    )

    rc = bash_idf(idf, f"idf.py {opts} build")
    if rc != 0:
        raise FlashError(f"idf.py build failed for {demo} (exit {rc})")
    if build_only:
        return 0

    serial, usb_serial = pick_kit_port(identity[0], port)
    print(f"-> port {serial}", flush=True)
    app = build_dir / "family_link_demo.bin"
    if not app.is_file():
        raise FlashError(f"missing {app} after build")
    stamped = stamp_who(app, identity)
    print(f"-> stamped {identity[2]} into {stamped.name}", flush=True)
    rc = flash_image(idf, build_dir, serial, stamped)
    if rc != 0:
        raise FlashError(f"flash failed for {demo} (exit {rc})")
    remember_usb(identity[0], usb_serial, serial)
    if monitor:
        return cmd_monitor(serial)
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
    parser.add_argument(
        "--who",
        help="Which person this kit is: mazi / arlo (or box-a / box-b). "
        "Stamped into the already-built .bin (no second compile). "
        "Also reads WHO= from the environment.",
    )
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
                who=args.who or os.environ.get("WHO") or os.environ.get("FAMILY_WHO"),
            )
        parser.print_help()
        print("\nDemos:\n" + known_demos())
        return 2
    except FlashError as exc:
        print(f"  [fail] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
