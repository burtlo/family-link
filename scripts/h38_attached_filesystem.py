#!/usr/bin/env python3
"""Private H38 preparation and build gate. No device operation is exposed here.

The later hardware controller must use a fresh held BOX backup, mandatory full
restore/readback, and original-application boot proof for every mutation epoch.
This module intentionally cannot flash or send an H38 command.
"""
from __future__ import annotations

import argparse
import base64
import importlib
import json
import os
import re
import secrets
import subprocess
import sys
import time
from pathlib import Path

# H37 is an established script with absolute sibling imports. Make the sibling
# directory importable for both `python scripts/...py` and package import.
SCRIPTS = str(Path(__file__).resolve().parent)
if SCRIPTS not in sys.path:
    sys.path.insert(0, SCRIPTS)
import h32_storage_qual as h32
import h37_attached_classification as h37
import h38_sd_contract as contract

ROOT = h32.ROOT
FW = h32.FW
H37_CURRENT_EPOCH = "43d7e2e5792ca6c1e494ff7cb06f3353"
SOURCE_FILES = (
    "firmware/CMakeLists.txt", "firmware/main/CMakeLists.txt",
    "firmware/main/idf_component.yml", "firmware/dependencies.lock",
    "firmware/sdkconfig.defaults", "firmware/sdkconfig.h38.defaults",
    "firmware/partitions/h38_sdmmc_filesystem.csv",
    "firmware/demos/h38_sdmmc_filesystem.c",
    "firmware/common/h38_disk_guard.c", "firmware/common/h38_disk_guard.h",
    "firmware/common/h38_fatfs_adapter.c", "firmware/common/h38_fatfs_adapter.h",
    "firmware/common/h38_filesystem_io.c", "firmware/common/h38_filesystem_io.h",
    "scripts/h38_sd_contract.py", "scripts/h38_attached_filesystem.py",
)
SDK_FILES = (
    "tools/cmake/version.cmake", "components/fatfs/src/ff.c",
    "components/fatfs/src/ffconf.h", "components/fatfs/diskio/diskio.c",
    "components/fatfs/diskio/diskio_sdmmc.c", "components/fatfs/vfs/vfs_fat.c",
    "components/fatfs/vfs/vfs_fat_sdmmc.c", "components/sdmmc/sdmmc_cmd.c",
    "components/sdmmc/sdmmc_init.c",
    "components/esp_driver_sdmmc/src/sdmmc_host.c",
    "components/esp_driver_usb_serial_jtag/src/usb_serial_jtag.c",
)
REQUIRED_CONFIG = {
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG": "y",
    "CONFIG_ESP_CONSOLE_SECONDARY_NONE": "y",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM": "y",
    "CONFIG_FATFS_LFN_HEAP": "y",
    "CONFIG_FATFS_MAX_LFN": "64",
    "CONFIG_FATFS_PER_FILE_CACHE": "y",
    "CONFIG_FATFS_SECTOR_512": "y",
    "CONFIG_FATFS_VOLUME_COUNT": "2",
}


def stop(message: str) -> None:
    raise ValueError(message)


def private_dir(path: Path, *, absent: bool = False) -> Path:
    path = path.expanduser().absolute()
    if path.is_symlink() or path.resolve().is_relative_to(ROOT.resolve()):
        stop("private directory must be outside repository and not a symlink")
    if absent and path.exists():
        stop("immutable H38 epoch directory already exists")
    if path.exists() and (not path.is_dir() or path.stat().st_mode & 0o077):
        stop("private directory must be mode 0700")
    return path


def source_identity() -> dict:
    paths = [ROOT / item for item in SOURCE_FILES]
    bsp = FW / "managed_components/espressif__esp-box-3"
    paths.extend(sorted(bsp.rglob("*.c")))
    paths.extend(sorted(bsp.rglob("*.h")))
    missing = [p for p in paths if not p.is_file()]
    if missing:
        stop("required H38 source unavailable: " + ", ".join(str(p) for p in missing))
    files = {str(p.relative_to(ROOT)): h32.digest(p) for p in paths}
    return {"files": files, "sha256": h32.dh(json.dumps(files, sort_keys=True,
                                                          separators=(",", ":")).encode())}


def sdk_identity() -> dict:
    paths = [h32.IDF / item for item in SDK_FILES]
    if any(not p.is_file() for p in paths):
        stop("pinned ESP-IDF source unavailable")
    if h32.idf_version() != "ESP-IDF v5.4.2":
        stop("ESP-IDF version differs from pinned 5.4.2")
    return {str(p.relative_to(h32.IDF)): h32.digest(p) for p in paths}


def build_tools_preflight() -> dict:
    if not (h32.IDF / "export.sh").is_file():
        stop("ESP-IDF export script unavailable")
    version = h32.idf_version()
    esptool = h32.run(["python", "-m", "esptool", "version"], capture=True).stdout.strip()
    compiler = h32.run(["xtensa-esp32s3-elf-gcc", "--version"], capture=True).stdout.splitlines()[0]
    if not version or not esptool or not compiler:
        stop("build tool version probe incomplete")
    return {"idf": version, "esptool": esptool, "compiler": compiler}


def capture_preflight(importer=importlib.import_module) -> dict:
    """Validate capture and recovery dependencies without opening a device port."""
    serial, info, error = h37.resolve_serial(importer)
    if error:
        stop(error)
    port = serial.Serial(port=None, baudrate=115200, timeout=.05, write_timeout=.5)
    try:
        for name in ("read", "write", "reset_input_buffer", "close"):
            if not callable(getattr(port, name, None)):
                stop("pyserial capture API incomplete")
        if port.write_timeout != .5:
            stop("bounded serial write timeout unavailable")
    finally:
        port.close()
    for name in ("esptool", "verify_device", "require_backup", "match_read", "restore",
                 "partitions", "app_desc", "region"):
        if not callable(getattr(h32, name, None)):
            stop("BOX preservation helper API incomplete")
    for name in ("load_intent", "bind_command", "phase_command", "parse_capture"):
        if not callable(getattr(contract, name, None)):
            stop("H38 contract API incomplete")
    venv = (ROOT / ".venv").resolve()
    if Path(sys.prefix).resolve() != venv or not Path(sys.executable).is_file():
        stop("workspace .venv Python environment required")
    serial_path = Path(getattr(serial, "__file__", "")).resolve()
    if not serial_path.is_relative_to(venv):
        stop("pyserial must load from workspace .venv")
    return {"status": "ready", "python": sys.executable,
            "serial_module": getattr(serial, "__file__", "unknown"), "serial_api": info}


def _git_revision() -> str:
    value = h32.git_head()
    if not re.fullmatch(r"[0-9a-f]{40}", value):
        stop("Git revision unavailable")
    status = subprocess.run(["git", "status", "--porcelain", "--", *SOURCE_FILES],
                            cwd=ROOT, text=True, capture_output=True, check=True).stdout
    if status.strip():
        stop("H38 source is uncommitted; commit reviewed source before preparing intent")
    return value


def _backup(path: Path) -> dict:
    meta = h37.backup(private_dir(path))
    if meta["full"]["bytes"] != h32.FLASH:
        stop("full BOX backup missing")
    return meta


def _intent(epoch: str, cid: str, old_mbr: bytes, revision: str) -> dict:
    value = {
        "schema": "h38-intent-v1", "epoch": epoch, "profile": contract.PROFILE,
        "h35_reference_epoch": contract.H35_REFERENCE_EPOCH,
        "private_cid_sha256": cid, "old_mbr_sha256": h32.dh(old_mbr),
        "card_sector_bytes": contract.SECTOR_BYTES, "card_sector_count": contract.CARD_SECTORS,
        "volume_start_lba": contract.VOLUME_START,
        "volume_sector_count": contract.VOLUME_SECTORS, "mbr_bytes": 512,
        "format": dict(contract.FORMAT), "io": dict(contract.IO),
        "capture_limits": dict(contract.CAPTURE_LIMITS),
        "source_sdk_snapshot": {"source_revision": revision, "sdk_version": "5.4.2"},
        "exit_state": contract.EXIT_STATE,
    }
    return contract.validate_intent(value)


def current_h37_mbr(h37_run_dir: Path) -> tuple[bytes, str]:
    """Bind old LBA 0 to the completed private H37 capture, not an arbitrary file."""
    h37_run_dir = private_dir(h37_run_dir)
    run = json.loads((h37_run_dir / "run-private.json").read_text())
    capture = json.loads((h37_run_dir / "capture-metadata-private.json").read_text())
    manifest_path = h37_run_dir / "build/manifest.json"
    manifest = json.loads(manifest_path.read_text())
    raw_path = h37_run_dir / "capture-raw-private.bin"
    snapshot_path = h37_run_dir / "sector-snapshot-private.bin.json"
    snapshot = json.loads(snapshot_path.read_text())
    if run.get("schema") != 1 or run.get("epoch") != H37_CURRENT_EPOCH or \
       Path(run.get("build_dir", "")).resolve() != h37_run_dir / "build" or \
       capture.get("status") != "captured" or capture.get("epoch") != H37_CURRENT_EPOCH or \
       capture.get("manifest_sha256") != h32.digest(manifest_path) or \
       capture.get("raw_sha256") != h32.digest(raw_path) or \
       capture.get("raw_bytes") != raw_path.stat().st_size or \
       manifest.get("epoch") != H37_CURRENT_EPOCH or \
       snapshot.get("schema") != 1 or snapshot.get("epoch") != H37_CURRENT_EPOCH or \
       snapshot.get("elf_sha256") != manifest.get("elf_sha256") or \
       set(snapshot.get("sectors", {})) != {"0", "32768"}:
        stop("H37 current capture/old-MBR evidence binding differs")
    try:
        old_mbr = base64.b64decode(snapshot["sectors"]["0"], validate=True)
    except (ValueError, KeyError) as exc:
        raise ValueError("H37 old MBR sector invalid") from exc
    if len(old_mbr) != 512:
        stop("H37 old MBR is not one sector")
    return old_mbr, h32.digest(snapshot_path)


def prepare(run_dir: Path, backup_dir: Path, h37_run_dir: Path) -> dict:
    run_dir = private_dir(run_dir, absent=True)
    backup_meta = _backup(backup_dir)
    old_mbr, h37_snapshot_sha = current_h37_mbr(h37_run_dir)
    revision = _git_revision()
    source = source_identity()
    sdk = sdk_identity()
    tools = build_tools_preflight()
    capture_preflight()
    cid = h37.read_h35_identity()
    intent = _intent(secrets.token_hex(16), cid, old_mbr, revision)
    run_dir.mkdir(mode=0o700)
    raw = contract.canonical_intent_bytes(intent)
    (run_dir / "intent-private.json").write_bytes(raw)
    (run_dir / "intent-private.json").chmod(0o600)
    meta = {"schema": 1, "epoch": intent["epoch"], "intent_sha256": h32.dh(raw),
            "build_dir": str(run_dir / "build"), "backup_dir": str(backup_dir.resolve()),
            "baseline_full_sha256": backup_meta["full"]["sha256"],
            "device_fingerprint_sha256": backup_meta["device"]["fingerprint_sha256"],
            "h37_snapshot_sha256": h37_snapshot_sha,
            "h37_run_dir": str(h37_run_dir.resolve()),
            "source": source, "sdk": sdk, "tools": tools,
            "created_unix": int(time.time()), "status": "prepared"}
    h32.atomic_json(run_dir / "run-private.json", meta, 0o600)
    return {"status": "prepared", "epoch": intent["epoch"],
            "intent_sha256": meta["intent_sha256"]}


def require_run(run_dir: Path, backup_dir: Path | None = None) -> tuple[dict, dict]:
    run_dir = private_dir(run_dir)
    meta = json.loads((run_dir / "run-private.json").read_text())
    raw = (run_dir / "intent-private.json").read_bytes()
    intent = contract.load_intent(raw)
    if meta.get("schema") != 1 or meta.get("epoch") != intent["epoch"] or \
       meta.get("intent_sha256") != h32.dh(raw) or \
       Path(meta.get("build_dir", "")).resolve() != run_dir / "build":
        stop("private H38 epoch binding differs")
    h37_snapshot = Path(meta.get("h37_run_dir", "")) / "sector-snapshot-private.bin.json"
    if not h37_snapshot.is_file() or h32.digest(h37_snapshot) != meta.get("h37_snapshot_sha256"):
        stop("preserved H37 MBR source changed")
    if backup_dir is not None:
        backup_meta = _backup(backup_dir)
        if Path(meta["backup_dir"]).resolve() != backup_dir.resolve() or \
           meta["baseline_full_sha256"] != backup_meta["full"]["sha256"] or \
           meta["device_fingerprint_sha256"] != backup_meta["device"]["fingerprint_sha256"]:
            stop("BOX baseline binding differs")
    return meta, intent


def _review_gate(run_dir: Path, meta: dict) -> None:
    path = run_dir / "source-review-private.json"
    try:
        review = json.loads(path.read_text())
    except (OSError, ValueError) as exc:
        raise ValueError("independent H38 source review gate missing") from exc
    expected = {"schema": 1, "status": "approved", "epoch": meta["epoch"],
                "source_sha256": meta["source"]["sha256"],
                "sdk_sha256": h32.dh(json.dumps(meta["sdk"], sort_keys=True).encode()),
                "reviewer": review.get("reviewer")}
    if review != expected or not isinstance(review["reviewer"], str) or not review["reviewer"]:
        stop("independent H38 source review gate differs from epoch")


def _header(intent: dict) -> str:
    fields = {
        "H38_PRIVATE_CID_SHA256": intent["private_cid_sha256"],
        "H38_OLD_MBR_SHA256": intent["old_mbr_sha256"],
        "H38_WRITE_MANIFEST_SHA256": contract.intent_sha256(intent),
        "H38_SOURCE_REVISION": intent["source_sdk_snapshot"]["source_revision"],
        "H38_INTENT_SHA256": contract.intent_sha256(intent),
    }
    return "/* Private, immutable H38 epoch binding. */\n" + "".join(
        f'#define {name} "{value}"\n' for name, value in fields.items())


def _config(path: Path) -> dict[str, str]:
    settings = {}
    for line in path.read_text().splitlines():
        found = re.fullmatch(r"(CONFIG_[A-Z0-9_]+)=(.*)", line)
        unset = re.fullmatch(r"# (CONFIG_[A-Z0-9_]+) is not set", line)
        if found:
            settings[found[1]] = found[2]
        elif unset:
            settings[unset[1]] = "n"
    return settings


def validate(run_dir: Path, backup_dir: Path, *, historical: bool = False) -> dict:
    meta, intent = require_run(run_dir, backup_dir)
    build = run_dir / "build"
    manifest = json.loads((build / "manifest-private.json").read_text())
    if manifest.get("schema") != 1 or manifest.get("epoch") != intent["epoch"] or \
       manifest.get("intent_sha256") != meta["intent_sha256"] or \
       manifest.get("source") != meta["source"] or manifest.get("sdk") != meta["sdk"] or \
       manifest.get("git_revision") != intent["source_sdk_snapshot"]["source_revision"]:
        stop("H38 build manifest binding differs")
    if not historical and source_identity() != meta["source"]:
        stop("source changed after H38 intent preparation")
    if sdk_identity() != meta["sdk"] or build_tools_preflight() != meta["tools"]:
        stop("pinned SDK or build tools changed")
    for rel, digest in meta["source"]["files"].items():
        if h32.digest(build / "source-snapshot" / rel) != digest:
            stop("source snapshot differs")
    for name, expected in manifest["artifacts"].items():
        artifact = (build / name).resolve()
        if not artifact.is_relative_to(build.resolve()) or not artifact.is_file() or \
           artifact.stat().st_size != expected["bytes"] or h32.digest(artifact) != expected["sha256"]:
            stop("H38 artifact binding differs")
    if _config(build / "sdkconfig") != manifest["effective_config"]:
        stop("effective config differs from manifest")
    for key, value in REQUIRED_CONFIG.items():
        if manifest["effective_config"].get(key) != value:
            stop("H38 effective config differs from frozen profile")
    backup_meta = _backup(backup_dir)
    table = h32.partitions((build / "partition_table/partition-table.bin").read_bytes())
    if table != backup_meta["partition_table"]["entries"]:
        stop("built partition table differs from current BOX baseline")
    factory = [p for p in table if p["name"] == "factory" and p["type"] == 0 and
               p["subtype"] == 0 and p["offset"] == h32.APP_OFF]
    app = build / "family_link_demo.bin"
    if len(factory) != 1 or app.stat().st_size > factory[0]["size"] * 85 // 100:
        stop("H38 app exceeds reviewed factory headroom")
    desc = h32.app_desc(app.read_bytes())
    if not desc or desc != manifest.get("app_descriptor") or \
       desc["elf_sha256"] != manifest["runtime_elf_sha256"] or \
       desc["idf"] != "v5.4.2":
        stop("runtime ELF descriptor differs")
    return manifest


def build(run_dir: Path, backup_dir: Path) -> dict:
    run_dir = private_dir(run_dir)
    meta, intent = require_run(run_dir, backup_dir)
    _review_gate(run_dir, meta)
    if source_identity() != meta["source"] or sdk_identity() != meta["sdk"]:
        stop("reviewed source or SDK changed")
    if _git_revision() != intent["source_sdk_snapshot"]["source_revision"]:
        stop("source revision changed after intent preparation")
    tools = build_tools_preflight()
    if tools != meta["tools"]:
        stop("build tools changed")
    build_dir = run_dir / "build"
    if build_dir.exists():
        stop("H38 build directory already exists")
    build_dir.mkdir(mode=0o700)
    backup_meta = _backup(backup_dir)
    table = backup_meta["partition_table"]["entries"]
    csv = build_dir / "original-partitions-private.csv"
    csv.write_text("# Current preserved BOX partition map\n" + "".join(
        f"{p['name']},0x{p['type']:x},0x{p['subtype']:x},0x{p['offset']:x},0x{p['size']:x},"
        f"{'encrypted' if p['flags'] == 1 else ''}\n" for p in table))
    defaults = build_dir / "partition.defaults"
    defaults.write_text(f'CONFIG_PARTITION_TABLE_CUSTOM=y\nCONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{csv}"\nCONFIG_PARTITION_TABLE_OFFSET=0x8000\n')
    header = build_dir / "h38-intent-private.h"
    header.write_text(_header(intent))
    for path in (csv, defaults, header):
        path.chmod(0o600)
    h32.run(["idf.py", "-D", "FAMILY_DEMO=h38_sdmmc_filesystem",
             "-D", f"H38_RUN_EPOCH={intent['epoch']}",
             "-D", f"H35_REFERENCE_EPOCH={contract.H35_REFERENCE_EPOCH}",
             "-D", f"H38_INTENT_HEADER={header}",
             "-D", f"SDKCONFIG={build_dir / 'sdkconfig'}",
             "-D", f"SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.h38.defaults;{defaults}",
             "-B", str(build_dir), "-C", str(FW), "build"])
    if source_identity() != meta["source"] or sdk_identity() != meta["sdk"]:
        stop("reviewed source or SDK changed during build")
    snap = build_dir / "source-snapshot"
    for rel, expected in meta["source"]["files"].items():
        source = ROOT / rel
        target = snap / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        if h32.digest(target) != expected:
            stop("source snapshot failed")
    artifacts = {}
    for name in ("family_link_demo.bin", "family_link_demo.elf",
                 "family_link_demo.map", "partition_table/partition-table.bin",
                 "bootloader/bootloader.bin", "sdkconfig", "original-partitions-private.csv",
                 "partition.defaults", "h38-intent-private.h"):
        path = build_dir / name
        artifacts[name] = {"bytes": path.stat().st_size, "sha256": h32.digest(path)}
    desc = h32.app_desc((build_dir / "family_link_demo.bin").read_bytes())
    if not desc:
        stop("built application descriptor missing")
    manifest = {"schema": 1, "epoch": intent["epoch"],
                "intent_sha256": meta["intent_sha256"], "source": meta["source"],
                "sdk": meta["sdk"], "tools": meta["tools"],
                "artifacts": artifacts, "effective_config": _config(build_dir / "sdkconfig"),
                "runtime_elf_sha256": desc["elf_sha256"],
                "app_descriptor": desc, "git_revision": _git_revision()}
    h32.atomic_json(build_dir / "manifest-private.json", manifest, 0o600)
    return validate(run_dir, backup_dir)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("preflight")
    for action in ("prepare", "build", "validate"):
        cmd = sub.add_parser(action)
        cmd.add_argument("--run-dir", type=Path, required=True)
        cmd.add_argument("--backup-dir", type=Path, required=True)
        if action == "prepare":
            cmd.add_argument("--h37-run-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.action == "preflight":
            result = {"capture": capture_preflight(), "build_tools": build_tools_preflight()}
        elif args.action == "prepare":
            result = prepare(args.run_dir, args.backup_dir, args.h37_run_dir)
        elif args.action == "build":
            result = build(args.run_dir, args.backup_dir)
        else:
            result = validate(args.run_dir, args.backup_dir)
        print(json.dumps(result if args.action == "prepare" else
                         {"status": "ready", "epoch": result.get("epoch")}, sort_keys=True))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        raise SystemExit("STOP: " + str(exc)) from exc


if __name__ == "__main__":
    main()
