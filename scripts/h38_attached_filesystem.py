#!/usr/bin/env python3
"""Private H38 source/build gates and bounded held-device controller.

The hardware path remains gated by independent source, linked-image, and fresh
current-backup reviews, unless the operator explicitly selects the existing verified
restore image and waives current-content preservation. Hardware use is a separate operator action.
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
    "scripts/h32_storage_qual.py", "scripts/h37_attached_classification.py",
    "scripts/h35_attached_discovery.py", "scripts/h37_sd_metadata.py",
    "scripts/h38_controller_checks.py",
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
    plan = json.loads((h37_run_dir / "read-plan-private.json").read_text())
    ledger = json.loads((h37_run_dir / "dispatch-ledger-private.json").read_text())
    if plan.get("schema") != 1 or plan.get("epoch") != H37_CURRENT_EPOCH or \
       plan.get("elf_sha256") != manifest.get("elf_sha256") or \
       ledger.get("schema") != 1 or ledger.get("epoch") != H37_CURRENT_EPOCH or \
       ledger.get("elf_sha256") != manifest.get("elf_sha256"):
        stop("H37 read-plan/dispatch binding differs")
    plan_rows = [row for stage in plan.get("stages", []) for row in stage]
    planned = [(int(row["lba"]), int(row["count"])) for row in plan_rows]
    dispatched = [(int(row["lba"]), int(row["count"])) for row in ledger.get("dispatches", [])]
    md = importlib.import_module("h37_sd_metadata")
    parsed = md.parse_capture(raw_path.read_bytes(), H37_CURRENT_EPOCH,
                              manifest["elf_sha256"], contract.H35_REFERENCE_EPOCH,
                              h37.read_h35_identity(), planned, dispatched)
    if parsed.terminal.get("result") != "read_complete":
        stop("H37 raw capture is not a complete successful metadata read")
    h37.replay_classification(parsed, h37_run_dir, H37_CURRENT_EPOCH, manifest["elf_sha256"])
    return old_mbr, h32.digest(snapshot_path)


def layout_reference_request(prior_dir: Path, backup_dir: Path) -> tuple[bytes, dict]:
    """Validate actual prior layout evidence without trusting a sector sidecar."""
    prior_dir = private_dir(prior_dir)
    meta = json.loads((prior_dir / "run-private.json").read_text())
    intent_path = prior_dir / "intent-private.json"
    intent = contract.load_intent(intent_path.read_bytes())
    manifest_path = prior_dir / "build/manifest-private.json"
    manifest = json.loads(manifest_path.read_text())
    baseline = _backup(backup_dir)
    if meta.get("epoch") != intent["epoch"] or manifest.get("epoch") != intent["epoch"] or \
       meta.get("intent_sha256") != h32.digest(intent_path) or \
       manifest.get("intent_sha256") != h32.digest(intent_path) or \
       meta.get("source") != manifest.get("source") or meta.get("sdk") != manifest.get("sdk") or \
       meta.get("tools") != manifest.get("tools") or \
       manifest.get("git_revision") != intent["source_sdk_snapshot"]["source_revision"] or \
       Path(meta.get("build_dir", "")).resolve() != prior_dir / "build" or \
       Path(meta.get("backup_dir", "")).resolve() != backup_dir.resolve() or \
       meta.get("baseline_full_sha256") != baseline["full"]["sha256"] or \
       meta.get("device_fingerprint_sha256") != baseline["device"]["fingerprint_sha256"]:
        stop("prior H38 layout source/build/baseline binding differs")
    _review_gate(prior_dir, meta)
    _linked_review_gate(prior_dir, manifest)
    for name, artifact in manifest["artifacts"].items():
        path = prior_dir / "build" / name
        if not path.is_file() or path.stat().st_size != artifact["bytes"] or h32.digest(path) != artifact["sha256"]:
            stop("prior H38 layout artifact differs")
    for name, digest in manifest["source"]["files"].items():
        if h32.digest(prior_dir / "build/source-snapshot" / name) != digest:
            stop("prior H38 layout source snapshot differs")
    _validate_runtime_elf_binding(manifest, manifest["app_descriptor"])
    result_path = prior_dir / "run-result-private.json"
    restore_path = prior_dir / "restore-proof-private.json"
    result = json.loads(result_path.read_text())
    restore = json.loads(restore_path.read_text())
    if result.get("epoch") != intent["epoch"] or result.get("restore_verified") is not True or \
       restore.get("status") != "verified" or restore.get("full_sha256") != baseline["full"]["sha256"]:
        stop("prior H38 device restoration is not verified")
    raw_path = prior_dir / "capture-raw-private.bin"
    raw = raw_path.read_bytes()
    if len(raw) > 1_048_576:
        stop("prior H38 layout capture exceeds bound")
    rows = [contract._parse_record(line) for line in raw.splitlines(keepends=True)
            if line.startswith(b"H38,")]
    prefix = rows[:10]
    elf = manifest["runtime_elf_sha256"]
    if [row.event for row in prefix] != contract.SUCCESS_EVENT_SCHEDULE[:10] or \
       any(row.fields["epoch"] != intent["epoch"] or row.fields["elf_sha256"] != elf for row in prefix):
        stop("prior H38 layout successful prefix differs")
    fields = {row.event: row.fields for row in prefix}
    if fields["BOOT"]["reset_reason"] not in {"1", "3", "11"} or \
       any(fields["TRANSPORT"][key] != value for key, value in contract.TRANSPORT.items()) or \
       any(fields[event]["error"] != "0" for event in ("HOST", "SLOT", "CARD")) or \
       {key: fields["GEOMETRY"][key] for key in ("sectors", "sector_bytes", "capacity_bytes", "bus_width", "real_freq_khz", "ddr")} != \
       {"sectors": str(contract.CARD_SECTORS), "sector_bytes": "512", "capacity_bytes": str(contract.CARD_SECTORS * 512),
        "bus_width": "4", "real_freq_khz": "20000", "ddr": "0"} or \
       fields["READY"]["accepts"] != "BIND" or \
       any(fields["IDENTITY_MATCH"][key] != value for key, value in
           {"reference_epoch": contract.H35_REFERENCE_EPOCH, "match": "1", "error": "0"}.items()):
        stop("prior H38 layout transport/discovery/bind prefix differs")
    cid = {key: value for key, value in fields["CID_PRIVATE"].items() if key not in ("epoch", "elf_sha256")}
    if contract.h35_cid_digest(contract.H35_REFERENCE_EPOCH, cid) != intent["private_cid_sha256"] or \
       intent["private_cid_sha256"] != h37.read_h35_identity():
        stop("prior H38 layout card identity differs")
    layout = fields["LAYOUT_RESULT"]
    mbr = base64.b64decode(layout["readback_base64_private"], validate=True)
    contract.validate_mbr(mbr, intent["epoch"])
    if h32.dh(mbr) != layout["readback_sha256"] or any(layout[key] != value for key, value in
        {"physical_lba": "0", "write_sectors": "1", "write_bytes": "512", "write_count": "1",
         "readback_match": "1", "trim_requests": "0", "erase_calls": "0", "status": "ok", "error": "0"}.items()):
        stop("prior H38 actual MBR write/readback evidence differs")
    ledger_path = prior_dir / "dispatch-ledger-private.jsonl"
    ledger = [json.loads(line) for line in ledger_path.read_text().splitlines()]
    expected = ["BIND", "LAYOUT", "FORMAT", "IO", "FINISH"]
    if not 2 <= len(ledger) <= 5 or [row.get("command") for row in ledger] != expected[:len(ledger)] or \
       any(row.get("schema") != 1 or row.get("epoch") != intent["epoch"] or row.get("elf_sha256") != elf or
           type(row.get("elapsed_ms")) is not int or row["elapsed_ms"] < 0 for row in ledger) or \
       any(a["elapsed_ms"] > b["elapsed_ms"] for a, b in zip(ledger, ledger[1:])):
        stop("prior H38 layout dispatch binding/order differs")
    return mbr, {"schema": 1, "status": "pending", "epoch": intent["epoch"],
                 "intent_sha256": h32.digest(intent_path), "manifest_sha256": h32.digest(manifest_path),
                 "capture_sha256": h32.digest(raw_path), "dispatch_sha256": h32.digest(ledger_path),
                 "result_sha256": h32.digest(result_path), "restore_sha256": h32.digest(restore_path),
                 "mbr_sha256": h32.dh(mbr), "reviewer": None}


def prior_h38_layout(prior_dir: Path, backup_dir: Path) -> tuple[bytes, str]:
    mbr, request = layout_reference_request(prior_dir, backup_dir)
    path = prior_dir / "layout-reference-review-private.json"
    review = json.loads(path.read_text())
    expected = {**request, "status": "approved", "reviewer": review.get("reviewer")}
    if review != expected or not isinstance(review.get("reviewer"), str) or not review["reviewer"].strip():
        stop("independent prior H38 layout reference review differs")
    return mbr, h32.digest(path)


def prepare(run_dir: Path, backup_dir: Path, h37_run_dir: Path,
            prior_h38_run_dir: Path | None = None) -> dict:
    run_dir = private_dir(run_dir, absent=True)
    backup_meta = _backup(backup_dir)
    old_mbr, h37_snapshot_sha = current_h37_mbr(h37_run_dir)
    prior_review_sha = None
    if prior_h38_run_dir is not None:
        old_mbr, prior_review_sha = prior_h38_layout(prior_h38_run_dir, backup_dir)
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
    if prior_h38_run_dir is not None:
        meta["prior_h38_layout_run_dir"] = str(prior_h38_run_dir.resolve())
        meta["prior_h38_layout_review_sha256"] = prior_review_sha
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
    if "prior_h38_layout_run_dir" in meta:
        mbr, review_sha = prior_h38_layout(Path(meta["prior_h38_layout_run_dir"]), Path(meta["backup_dir"]))
        if review_sha != meta.get("prior_h38_layout_review_sha256") or h32.dh(mbr) != intent["old_mbr_sha256"]:
            stop("prior H38 layout source changed")
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


def _validate_runtime_elf_binding(manifest: dict, descriptor: dict) -> None:
    """Bind firmware-reported runtime ELF identity to the reviewed ELF file."""
    artifacts = manifest.get("artifacts", {})
    elf = artifacts.get("family_link_demo.elf", {})
    if descriptor.get("elf_sha256") != manifest.get("runtime_elf_sha256") or \
       manifest.get("runtime_elf_sha256") != elf.get("sha256"):
        stop("runtime ELF descriptor differs from reviewed ELF artifact")


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
    if not desc or desc != manifest.get("app_descriptor") or desc["idf"] != "v5.4.2":
        stop("runtime ELF descriptor differs")
    _validate_runtime_elf_binding(manifest, desc)
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


def _linked_review_gate(run_dir: Path, manifest: dict) -> None:
    """A separate independent reviewer must approve the exact linked image."""
    path = run_dir / "linked-review-private.json"
    try:
        review = json.loads(path.read_text())
    except (OSError, ValueError) as exc:
        raise ValueError("independent linked-image review gate missing") from exc
    expected = {"schema": 1, "status": "approved", "epoch": manifest["epoch"],
                "manifest_sha256": h32.digest(run_dir / "build/manifest-private.json"),
                "elf_sha256": manifest["artifacts"]["family_link_demo.elf"]["sha256"],
                "app_sha256": manifest["artifacts"]["family_link_demo.bin"]["sha256"],
                "reviewer": review.get("reviewer")}
    if review != expected or not isinstance(review["reviewer"], str) or not review["reviewer"]:
        stop("independent linked-image review differs from exact H38 build")


class DeadlineError(TimeoutError):
    def __init__(self, name: str):
        self.name = name
        super().__init__(name + " deadline reached")


class TransportError(RuntimeError):
    """Command transport failed; no phase timer may be started."""


class CaptureTerminalError(RuntimeError):
    """Capture ended at a parser-validated firmware terminal with failed evidence."""


class RunClocks:
    """Monotonic controller clocks; a timeout never extends the recovery reserve."""

    def __init__(self, now, limits=contract.CAPTURE_LIMITS):
        self.now = now
        self.master = now()
        self.master_end = self.master + limits["flash_restore_ms"] / 1000
        self.recovery_start = self.master_end - limits["restore_reserve_ms"] / 1000
        self.capture_start = None
        self.capture_end = None
        self.session_start = None
        self.session_end = None
        self.phase_start = None
        self.phase_name = None
        self.phase_history = []
        self.limits = limits

    def require_new_work(self, operation_ms: int) -> None:
        if self.now() + operation_ms / 1000 > self.recovery_start:
            raise DeadlineError("restore_reserve")

    def timeout_seconds(self, future_ms: int, cap_seconds: int) -> int:
        """Timeout for one subprocess, leaving all future work and restore time."""
        seconds = int(self.recovery_start - self.now() - future_ms / 1000)
        if seconds < 1:
            raise DeadlineError("restore_reserve")
        return min(seconds, cap_seconds)

    def start_capture(self) -> None:
        self.require_new_work(self.limits["capture_wall_ms"])
        self.capture_start = self.now()

    def remaining_capture_ms(self) -> int:
        if self.capture_start is None:
            return self.limits["capture_wall_ms"]
        remaining = self.limits["capture_wall_ms"] - int((self.now() - self.capture_start) * 1000)
        if self.session_start is not None:
            remaining = min(remaining, self.limits["session_ms"] -
                            int((self.now() - self.session_start) * 1000))
        return max(0, remaining)

    def start_session(self) -> None:
        if self.session_start is not None:
            raise ValueError("duplicate BOOT session")
        self.session_start = self.now()

    def start_phase(self, name: str) -> None:
        if name not in ("FORMAT", "IO") or self.phase_start is not None:
            raise ValueError("phase clock overlap")
        self.phase_name = name
        self.phase_start = self.now()

    def end_phase(self, name: str) -> None:
        self.check()
        if self.phase_name != name:
            raise ValueError("phase completion differs")
        self._finish_phase(self.now(), "milestone")

    def _finish_phase(self, ended: float, reason: str) -> None:
        if self.phase_start is None:
            return
        self.phase_history.append({
            "name": self.phase_name,
            "start_elapsed_ms": int((self.phase_start - self.master) * 1000),
            "end_elapsed_ms": int((ended - self.master) * 1000),
            "elapsed_ms": int((ended - self.phase_start) * 1000),
            "end_reason": reason,
        })
        self.phase_start = None
        self.phase_name = None

    def freeze_capture(self, reason: str, ended_at: float | None = None) -> None:
        """Freeze capture/session/active phase at a terminal or abort boundary."""
        ended = self.now() if ended_at is None else ended_at
        if self.capture_start is not None and self.capture_end is None:
            self.capture_end = ended
        if self.session_start is not None and self.session_end is None:
            self.session_end = ended
        self._finish_phase(ended, reason)

    def check(self) -> None:
        now = self.now()
        if now >= self.recovery_start:
            raise DeadlineError("restore_reserve")
        if self.capture_end is None and self.capture_start is not None and \
           now - self.capture_start > self.limits["capture_wall_ms"] / 1000:
            raise DeadlineError("capture_wall")
        if self.session_end is None and self.session_start is not None and \
           now - self.session_start > self.limits["session_ms"] / 1000:
            raise DeadlineError("firmware_session")
        if self.phase_start is not None:
            cap = contract.FORMAT["time_limit_ms"] if self.phase_name == "FORMAT" else contract.IO["time_limit_ms"]
            if now - self.phase_start > cap / 1000:
                raise DeadlineError(self.phase_name.lower() + "_phase")

    def summary(self) -> dict:
        now = self.now()
        capture_end = self.capture_end if self.capture_end is not None else now
        session_end = self.session_end if self.session_end is not None else now
        return {"master_elapsed_ms": int((now - self.master) * 1000),
                "master_overrun_ms": max(0, int((now - self.master_end) * 1000)),
                "capture_elapsed_ms": None if self.capture_start is None else int((capture_end - self.capture_start) * 1000),
                "session_elapsed_ms": None if self.session_start is None else int((session_end - self.session_start) * 1000),
                "capture_end_elapsed_ms": None if self.capture_end is None else int((self.capture_end - self.master) * 1000),
                "session_end_elapsed_ms": None if self.session_end is None else int((self.session_end - self.master) * 1000),
                "phases": self.phase_history + ([{
                    "name": self.phase_name,
                    "start_elapsed_ms": int((self.phase_start - self.master) * 1000),
                    "end_elapsed_ms": int((now - self.master) * 1000),
                    "elapsed_ms": int((now - self.phase_start) * 1000),
                    "end_reason": "running",
                }] if self.phase_start is not None else [])}


PREFLASH_ALLOWANCE_MS = 1_020_000
RECOVERY_WORST_CASE_MS = 2_685_000  # emergency reset, probes, full write/readback, and boot
ROM_WATCHDOG_RESET_CODES = frozenset((7, 8, 9, 11, 13, 16, 17, 18))
ROM_WATCHDOG_RESET_TAGS = frozenset((
    "TG0WDT_SYS_RESET", "TG1WDT_SYS_RESET", "RTCWDT_SYS_RESET",
    "TG0WDT_CPU_RESET", "TGWDT_CPU_RESET", "RTCWDT_CPU_RESET", "RTCWDT_RTC_RESET",
    "TG1WDT_CPU_RESET", "SUPER_WDT_RESET",
))


def _wait_for_capture_abort(clocks: RunClocks, sleep=time.sleep):
    """Wait for an authoritative deadline only while firmware capture is active."""
    if clocks.capture_start is None or clocks.capture_end is not None:
        return None
    while True:
        try:
            clocks.check()
        except DeadlineError as deadline:
            clocks.freeze_capture("deadline:" + deadline.name)
            return deadline
        sleep(.05)


def _bounded_esptool(port: str, args: list[str], clocks: RunClocks,
                     future_ms: int, cap_seconds: int, *, capture=False):
    timeout = clocks.timeout_seconds(future_ms, cap_seconds)
    try:
        # Device output is private evidence; never forward it to the caller.
        result = h32.esptool(port, args, timeout, capture=True)
    except subprocess.TimeoutExpired as exc:
        raise DeadlineError("master_preflash") from exc
    clocks.require_new_work(future_ms)
    return result


def _bounded_verify_device(port: str, baseline: dict, clocks: RunClocks,
                           future_ms: int) -> dict:
    """H32 chip/flash identity probe with a master-clock-aware subprocess cap."""
    chip = _bounded_esptool(port, ["--after", "no_reset", "chip_id"], clocks,
                            future_ms + 30_000, 30, capture=True)
    flash = _bounded_esptool(port, ["--after", "no_reset", "flash_id"], clocks,
                             future_ms, 30, capture=True)
    text = "\n".join((chip.stdout, chip.stderr, flash.stdout, flash.stderr))
    description = h32.field(r"Chip is\s+([^\r\n]+)", text, "chip")
    if "esp32-s3" not in description or \
       h32.field(r"Detected flash size:\s*([^\s]+)", text, "flash size") not in ("16mb", "16mib"):
        stop("connected BOX geometry differs")
    device = {"chip": "esp32-s3", "chip_description": description,
              "mac": h32.field(r"MAC:\s*([0-9a-f:]{17})", text, "MAC"),
              "flash_manufacturer": h32.field(r"Manufacturer:\s*([^\r\n]+)", text, "manufacturer"),
              "flash_device": h32.field(r"Device:\s*([^\r\n]+)", text, "flash device"),
              "flash_bytes": h32.FLASH}
    device["fingerprint_sha256"] = h32.dh(json.dumps(device, sort_keys=True).encode())
    if device["fingerprint_sha256"] != baseline["device"]["fingerprint_sha256"]:
        stop("connected BOX identity differs from backup")
    return device


def _fresh_current_backup(port: str, run_dir: Path, baseline: dict,
                          clocks: RunClocks) -> tuple[Path, dict]:
    """Take and verify current held BOX/NVS; permit only NVS drift from baseline."""
    future = PREFLASH_ALLOWANCE_MS + contract.CAPTURE_LIMITS["capture_wall_ms"]
    current = _bounded_verify_device(port, baseline, clocks, future)
    full = run_dir / "current-full-private.bin"
    pt = run_dir / "current-partition-private.bin"
    nvs = run_dir / "current-nvs-private.bin"
    if any(p.exists() for p in (full, pt, nvs)):
        stop("fresh current backup paths already used")
    _bounded_esptool(port, ["--after", "no_reset", "read_flash", "0", hex(h32.FLASH), str(full)],
                     clocks, future + 120_000, 600)
    _bounded_esptool(port, ["--after", "no_reset", "read_flash", hex(h32.PT_OFF), hex(h32.PT_SIZE), str(pt)],
                     clocks, future + 60_000, 60)
    nv = baseline["nvs"]
    _bounded_esptool(port, ["--after", "no_reset", "read_flash", hex(nv["offset"]), hex(nv["bytes"]), str(nvs)],
                     clocks, future, 60)
    if full.stat().st_size != h32.FLASH or pt.stat().st_size != h32.PT_SIZE or nvs.stat().st_size != nv["bytes"]:
        stop("fresh current BOX backup has short read")
    if h32.region(full, h32.PT_OFF, h32.PT_SIZE) != pt.read_bytes() or \
       h32.region(full, nv["offset"], nv["bytes"]) != nvs.read_bytes():
        stop("separate current partition/NVS readback differs from full BOX image")
    if h32.partitions(pt.read_bytes()) != baseline["partition_table"]["entries"]:
        stop("current partition table differs from preserved baseline")
    base_full = Path(baseline["_directory"]) / "original-flash.bin"
    old = base_full.read_bytes()
    new = full.read_bytes()
    start, end = nv["offset"], nv["offset"] + nv["bytes"]
    if len(old) != h32.FLASH or new[:start] != old[:start] or new[end:] != old[end:]:
        stop("current non-NVS BOX flash differs from preserved baseline")
    apps = []
    for app in baseline["apps"]:
        current_bytes = h32.region(full, app["offset"], app["size"])
        if h32.dh(current_bytes) != app["partition_sha256"] or h32.app_desc(current_bytes) != app["descriptor"]:
            stop("current original application differs from preserved baseline")
        apps.append({"name": app["name"], "offset": app["offset"],
                     "descriptor": app["descriptor"], "sha256": h32.dh(current_bytes)})
    proof = {"schema": 1, "status": "verified", "full_sha256": h32.digest(full),
             "partition_sha256": h32.digest(pt), "nvs_sha256": h32.digest(nvs),
             "device_fingerprint_sha256": current["fingerprint_sha256"], "apps": apps,
             "reset_held": True}
    h32.atomic_json(run_dir / "current-backup-private.json", proof, 0o600)
    return full, proof


def _record_stream(ser, raw_file, pending: bytearray, clocks: RunClocks,
                   captured: bytearray, host_errors: list[str]):
    """Yield bounded ASCII H38 records and retain all raw bytes, including ROM lines."""
    while True:
        clocks.check()
        newline = pending.find(b"\n")
        if newline >= 0:
            line = bytes(pending[:newline + 1])
            del pending[:newline + 1]
            if len(line) > contract.MAX_RECORD:
                yield contract.Record("MALFORMED", {})
                continue
            if line.startswith(b"H38,"):
                try:
                    yield contract._parse_record(line)
                except contract.ContractError:
                    yield contract.Record("MALFORMED", {})
            elif line.startswith(b"H38") and not line.startswith(b"H38C,"):
                yield contract.Record("MALFORMED", {})
            continue
        try:
            chunk = ser.read(512)
        except Exception as exc:
            if not host_errors:
                host_errors.append("serial read failed: " + type(exc).__name__)
            time.sleep(.05)
            continue
        if not chunk:
            continue
        if len(captured) + len(chunk) > 1_048_576:
            if not host_errors:
                host_errors.append("capture exceeded private raw evidence bound")
            continue
        captured.extend(chunk)
        try:
            raw_file.write(chunk)
            raw_file.flush()
        except Exception as exc:
            if not host_errors:
                host_errors.append("raw capture write failed: " + type(exc).__name__)
        pending.extend(chunk)
        if len(pending) > contract.MAX_RECORD and b"\n" not in pending:
            pending.clear()
            yield contract.Record("MALFORMED", {})


def _send_command(ser, command: bytes, clocks: RunClocks, operation_ms: int) -> None:
    clocks.check()
    clocks.require_new_work(operation_ms)
    if len(command) > contract.MAX_COMMAND or not command.endswith(b"\n"):
        stop("H38 command exceeds frozen framing")
    try:
        sent = ser.write(command)
    except Exception as exc:
        raise TransportError("H38 command serial write failed") from exc
    if sent != len(command):
        raise TransportError("short H38 command transport write")
    # Phase clock starts only after the complete bounded write returned.
    if command.startswith(b"H38C,1,FORMAT,"):
        clocks.start_phase("FORMAT")
    elif command.startswith(b"H38C,1,IO,"):
        clocks.start_phase("IO")


def _safe_terminal(raw: bytes, intent: dict, elf: str):
    """Only a parser-validated epoch/ELF-bound COMPLETE releases active SD."""
    try:
        terminal = contract.parse_capture(raw, intent["epoch"], elf, intent)
    except (ValueError, contract.ContractError):
        return None
    if terminal.result not in ("io_complete", "failed"):
        return None
    cleanup = [row.fields for row in terminal.records if row.event == "CLEANUP"]
    complete = [row.fields for row in terminal.records if row.event == "COMPLETE"]
    required = ("sd_unmount_attempted", "host_deinit_attempted", "power_off_attempted")
    if len(cleanup) != 1 or len(complete) != 1 or any(cleanup[0][key] != "1" for key in required):
        return None
    if any(complete[0][key] != cleanup[0][key] for key in
           (*required, "sd_unmount_error", "host_deinit_error", "power_off_error")):
        return None
    return terminal


def _capture_h38(port: str, run_dir: Path, manifest: dict, intent: dict,
                 serial_module, clocks: RunClocks) -> dict:
    raw_path = run_dir / "capture-raw-private.bin"
    ledger_path = run_dir / "dispatch-ledger-private.jsonl"
    if raw_path.exists() or ledger_path.exists():
        stop("H38 capture paths already used")
    elf = manifest["runtime_elf_sha256"]
    commands = [contract.bind_command(intent, elf)] + [
        contract.phase_command(name, intent["epoch"], elf)
        for name in ("LAYOUT", "FORMAT", "IO", "FINISH")]
    contract.validate_command_sequence(commands, intent, elf)
    dispatches = []
    records = []
    live_failure = None
    pending = bytearray()
    captured = bytearray()
    host_errors: list[str] = []
    serial_port = serial_module.Serial(port=None, baudrate=115200, timeout=.05, write_timeout=.5)
    serial_port.dtr = False
    serial_port.rts = False
    serial_port.port = port
    with raw_path.open("xb") as raw_file, ledger_path.open("xb") as ledger_file, serial_port as ser:
        os.chmod(raw_path, 0o600)
        os.chmod(ledger_path, 0o600)
        def record_dispatch(name: str) -> None:
            entry = {"schema": 1, "epoch": intent["epoch"], "elf_sha256": elf,
                     "command": name, "elapsed_ms": clocks.summary()["master_elapsed_ms"]}
            try:
                ledger_file.write((json.dumps(entry, sort_keys=True) + "\n").encode("ascii"))
                ledger_file.flush()
                os.fsync(ledger_file.fileno())
                dispatches.append(entry)
            except OSError as exc:
                if not host_errors:
                    host_errors.append("dispatch ledger write failed: " + type(exc).__name__)
        try:
            ser.reset_input_buffer()
            clocks.start_capture()
            # Listener is open before the first RTS/reset; clock includes boot.
            ser.rts = True
            time.sleep(.05)
            ser.rts = False
        except DeadlineError:
            raise
        except BaseException as exc:
            live_failure = "capture setup failed: " + type(exc).__name__
        safe_terminal = None
        for record in _record_stream(ser, raw_file, pending, clocks, captured, host_errors):
            event, fields = record.event, record.fields
            terminal_received_at = clocks.now() if event == "COMPLETE" else None
            if host_errors and live_failure is None:
                live_failure = host_errors[0]
            if event == "MALFORMED":
                live_failure = "malformed H38 serial record"
                continue
            if live_failure is not None:
                # Keep draining to a parser-validated terminal. A malformed or
                # unbound COMPLETE is not proof that SD cleanup finished.
                if event == "COMPLETE":
                    safe_terminal = _safe_terminal(bytes(captured), intent, elf)
                    if safe_terminal is not None:
                        clocks.freeze_capture("validated_terminal", terminal_received_at)
                        break
                continue
            try:
                if fields["epoch"] != intent["epoch"] or fields["elf_sha256"] != elf:
                    stop("H38 runtime epoch/ELF differs")
                if not records and event != "BOOT":
                    stop("H38 BOOT must be first record")
                if event == "BOOT":
                    clocks.start_session()
                records.append(event)
                if event in {"READY", "IDENTITY_MATCH", "LAYOUT_RESULT", "FORMAT_RESULT",
                             "RECLAIM_RESULT", "COMPLETE"} and records.count(event) != 1:
                    stop("duplicate H38 phase milestone")
                # Critical live checks precede the corresponding write command.
                if event == "GEOMETRY":
                    expected = {"sectors": "121503744", "sector_bytes": "512",
                                "capacity_bytes": "62209916928", "bus_width": "4",
                                "real_freq_khz": "20000", "ddr": "0"}
                    if any(fields[k] != v for k, v in expected.items()):
                        stop("fresh H38 card geometry differs")
                elif event == "CID_PRIVATE":
                    digest = contract.h35_cid_digest(intent["h35_reference_epoch"], {
                        "mfg_id": int(fields["mfg_id"]), "oem_id": int(fields["oem_id"]),
                        "revision": int(fields["revision"]), "serial": int(fields["serial"]),
                        "date": int(fields["date"]), "name_size": int(fields["name_size"]),
                        "name_hex": fields["name_hex"]})
                    if digest != intent["private_cid_sha256"]:
                        stop("fresh H38 card CID differs from private H35 reference")
                elif event == "READY":
                    if records != contract.SUCCESS_EVENT_SCHEDULE[:8] or fields["accepts"] != "BIND":
                        stop("H38 READY without validated card binding")
                    _send_command(ser, commands[0], clocks, clocks.remaining_capture_ms())
                    record_dispatch("BIND")
                elif event == "IDENTITY_MATCH":
                    if records != contract.SUCCESS_EVENT_SCHEDULE[:9]:
                        stop("H38 identity record order differs")
                    if fields["match"] != "1" or fields["error"] != "0" or fields["reference_epoch"] != intent["h35_reference_epoch"]:
                        stop("H38 current MBR/CID/intent identity mismatch")
                    _send_command(ser, commands[1], clocks, clocks.remaining_capture_ms())
                    record_dispatch("LAYOUT")
                elif event == "LAYOUT_RESULT":
                    if records != contract.SUCCESS_EVENT_SCHEDULE[:10]:
                        stop("H38 layout record order differs")
                    if fields["status"] != "ok":
                        continue
                    sector = base64.b64decode(fields["readback_base64_private"], validate=True)
                    contract.validate_mbr(sector, intent["epoch"])
                    if fields["readback_match"] != "1" or fields["status"] != "ok" or fields["error"] != "0" or \
                       h32.dh(sector) != fields["readback_sha256"] or \
                       {k: fields[k] for k in ("physical_lba", "write_sectors", "write_bytes",
                                                       "write_count", "trim_requests", "erase_calls")} != \
                       {"physical_lba": "0", "write_sectors": "1", "write_bytes": "512",
                        "write_count": "1", "trim_requests": "0", "erase_calls": "0"}:
                        stop("H38 bounded MBR readback differs")
                    _send_command(ser, commands[2], clocks, contract.FORMAT["time_limit_ms"])
                    record_dispatch("FORMAT")
                elif event == "FORMAT_START":
                    if records != contract.SUCCESS_EVENT_SCHEDULE[:11] or \
                       {k: fields[k] for k in ("volume_sectors", "sector_bytes", "fat_type",
                                                       "fat_count", "allocation_unit_bytes", "format_flags",
                                                       "work_buffer_bytes", "write_limit_bytes",
                                                       "read_limit_bytes", "time_limit_ms")} != \
                       {"volume_sectors": "1048576", "sector_bytes": "512", "fat_type": "FAT32",
                        "fat_count": "2", "allocation_unit_bytes": "4096", "format_flags": "10",
                        "work_buffer_bytes": "4096", "write_limit_bytes": "4194304",
                        "read_limit_bytes": "16777216", "time_limit_ms": "120000"}:
                        stop("H38 formatter request differs from fixed profile")
                elif event == "FORMAT_RESULT":
                    if records != contract.SUCCESS_EVENT_SCHEDULE[:12]:
                        stop("H38 format record order differs")
                    if fields["status"] != "ok":
                        continue
                    bpb = base64.b64decode(fields["bpb_base64_private"], validate=True)
                    contract.validate_bpb(bpb)
                    if h32.dh(bpb) != fields["bpb_sha256"] or fields["status"] != "ok" or fields["error"] != "0" or \
                       int(fields["write_bytes"]) > contract.FORMAT["write_limit_bytes"] or \
                       int(fields["read_bytes"]) > contract.FORMAT["read_limit_bytes"] or \
                       int(fields["max_call_sectors"]) > contract.IO["driver_call_max_sectors"] or \
                       fields["erase_calls"] != "0" or fields["f_result"] != "0":
                        stop("H38 format/BPB evidence differs")
                elif event == "MOUNT_RESULT" and fields["kind"] == "initial":
                    if records != contract.SUCCESS_EVENT_SCHEDULE[:13]:
                        stop("H38 initial mount record order differs")
                    if fields["status"] != "ok":
                        continue
                    if fields["mounted"] != "1" or fields["fs_type"] != "3" or \
                       fields["sector_bytes"] != "512" or fields["volume_sectors"] != "1048576" or \
                       fields["allocation_unit_bytes"] != "4096" or fields["cluster_count"] != "130811":
                        stop("H38 initial mount evidence differs")
                    clocks.end_phase("FORMAT")
                    _send_command(ser, commands[3], clocks, contract.IO["time_limit_ms"])
                    record_dispatch("IO")
                elif event == "RECLAIM_RESULT":
                    if fields["status"] != "ok":
                        continue
                    clocks.end_phase("IO")
                    _send_command(ser, commands[4], clocks, clocks.remaining_capture_ms())
                    record_dispatch("FINISH")
                elif event == "COMPLETE":
                    clocks.check()
                    safe_terminal = _safe_terminal(bytes(captured), intent, elf)
                    if safe_terminal is None:
                        live_failure = "unvalidated H38 COMPLETE"
                        continue
                    clocks.freeze_capture("validated_terminal", terminal_received_at)
                    break
            except DeadlineError:
                raise
            except Exception as exc:
                # Stop all phase dispatches; leave the listener attached until
                # a validated terminal arrives or the authoritative wall deadline fires.
                live_failure = "evidence or command mismatch: " + type(exc).__name__
                continue
            except BaseException as exc:
                live_failure = "capture dispatch failed: " + type(exc).__name__
                continue
        try:
            raw_file.flush()
            os.fsync(raw_file.fileno())
        except OSError as exc:
            if live_failure is None:
                live_failure = "raw evidence finalization failed: " + type(exc).__name__
    parsed = safe_terminal
    if parsed is None:
        raise DeadlineError("capture_terminal_missing")
    if live_failure is not None:
        raise CaptureTerminalError("H38 live evidence mismatch: " + live_failure)
    if parsed.result != "io_complete":
        raise CaptureTerminalError("H38 firmware result did not complete successfully")
    return {"raw_sha256": h32.digest(raw_path), "raw_bytes": raw_path.stat().st_size,
            "result": parsed.result, "dispatch_count": len(dispatches)}


def _boot_original(port: str, run_dir: Path, serial_module, baseline: dict) -> dict:
    """Open listener before reset and verify the restored product startup."""
    path = run_dir / "restored-boot-private.bin"
    if path.exists():
        stop("restored boot capture path already used")
    original = [a for a in baseline["apps"] if a["name"] == "factory"]
    if len(original) != 1 or not original[0].get("descriptor"):
        stop("preserved factory application descriptor unavailable")
    expected = original[0]["descriptor"]
    serial_port = serial_module.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=.5)
    serial_port.dtr = False
    serial_port.rts = False
    serial_port.port = port
    with path.open("xb") as file, serial_port as ser:
        os.chmod(path, 0o600)
        ser.reset_input_buffer()
        ser.rts = True
        time.sleep(.05)
        ser.rts = False
        until = time.monotonic() + 45
        while time.monotonic() < until:
            chunk = ser.read(512)
            if chunk:
                if file.tell() + len(chunk) > 65_536:
                    stop("restored boot capture exceeds bound")
                file.write(chunk)
                file.flush()
                if b"Calling app_main" in path.read_bytes():
                    break
        os.fsync(file.fileno())
    raw = path.read_bytes()
    lower = raw.lower()
    panic = any(x in lower for x in (b"guru meditation", b"backtrace:",
                                    b"abort() was called", b"panic'ed"))
    runtime_watchdog = any(x in lower for x in (b"task watchdog got triggered",
                                               b"interrupt wdt timeout"))
    lines = raw.decode("utf-8", errors="replace").splitlines()
    plain_lines = [re.sub(r"\x1b\[[0-9;]*m", "", line) for line in lines]
    rom_reset_events = []
    for index, line in enumerate(plain_lines):
        # ROM reset evidence is the anchored boot event. Do not infer from a
        # legend, arbitrary mention, or the runtime ESP_RST_USB enum value.
        match = re.match(r"^\s*rst:\s*0x([0-9a-f]+)\b(?:\s*\(([^)]*)\))?", line, re.I)
        if match:
            code = int(match.group(1), 16)
            tag = (match.group(2) or "").strip().upper()
            if tag.endswith("_RST"):
                tag = tag[:-4] + "_RESET"
            rom_reset_events.append((index, code, tag))
    first_rom_reset = rom_reset_events[0] if rom_reset_events else None
    rom_watchdog = any(code in ROM_WATCHDOG_RESET_CODES or tag in ROM_WATCHDOG_RESET_TAGS
                       for _, code, tag in rom_reset_events)
    watchdog = runtime_watchdog or rom_watchdog
    loaded = next((i for i, line in enumerate(lines) if re.search(
        r"Loaded app from partition at offset 0x0*10000\b", line, re.I)), None)
    app_main = next((i for i, line in enumerate(lines) if "Calling app_main" in line), None)

    def log_value(label: str) -> tuple[str, int] | None:
        for index, plain in enumerate(plain_lines):
            match = re.search(rf"(?:^|\s){re.escape(label)}:\s*(.*?)\s*$", plain)
            if match:
                return match.group(1).strip(), index
        return None

    project_entry = log_value("Project name")
    version_entry = log_value("App version")
    sdk_entry = log_value("IDF version") or log_value("ESP-IDF")
    elf_entry = log_value("ELF file SHA256")
    project_match = project_entry is not None and project_entry[0] == expected["project"]
    version_match = version_entry is not None and version_entry[0] == expected["version"]
    sdk_match = sdk_entry is not None and sdk_entry[0] == expected["idf"]
    elf_hash = None if elf_entry is None else elf_entry[0]
    elf_match = isinstance(elf_hash, str) and elf_hash.lower().startswith(expected["elf_sha256"][:8].lower())
    startup_indices = [entry[1] for entry in (project_entry, version_entry, sdk_entry, elf_entry)
                       if entry is not None]
    startup_order = loaded is not None and app_main is not None and \
        len(startup_indices) == 4 and loaded < min(startup_indices) and max(startup_indices) < app_main
    rom_usb_reset = first_rom_reset is not None and first_rom_reset[1] == 21
    result = {"full_boot_bytes": len(raw), "project_match": project_match,
              "version_match": version_match, "sdk_match": sdk_match,
              "elf_prefix_match": elf_match, "factory_offset_match": loaded is not None,
              "startup_order_match": startup_order, "app_main_seen": app_main is not None,
              "panic_seen": panic, "watchdog_seen": watchdog,
              "usb_reset_code": "21" if rom_usb_reset else "none",
              "usb_reset_namespace": "rom" if rom_usb_reset else "none"}
    if not all(result[k] for k in ("project_match", "version_match", "sdk_match",
                                    "elf_prefix_match", "factory_offset_match",
                                    "startup_order_match", "app_main_seen")) or panic or watchdog:
        stop("restored original application boot validation failed")
    return result


def _restore_current(port: str, run_dir: Path, full: Path, current: dict,
                     baseline: dict, serial_module, clocks: RunClocks) -> dict:
    """Never abandon recovery because a controller deadline elapsed."""
    readback = run_dir / "restore-readback-private.bin"
    pt = run_dir / "restore-partition-private.bin"
    nvs = run_dir / "restore-nvs-private.bin"
    if any(p.exists() for p in (readback, pt, nvs)):
        stop("restore evidence path already used")
    recovery_processes = []
    recovery_processes.extend(_verify_recovery_device(port, baseline)["processes"])
    commands = []
    process_codes = []

    def invoke(name: str, args: list[str], timeout: int):
        try:
            result = h32.esptool(port, args, timeout, capture=True)
        except subprocess.CalledProcessError as exc:
            process_codes.append({"operation": name, "returncode": exc.returncode,
                                  "error_type": type(exc).__name__})
            return None
        except BaseException as exc:
            process_codes.append({"operation": name, "returncode": None,
                                  "error_type": type(exc).__name__})
            return None
        returncode = getattr(result, "returncode", None)
        process_codes.append({"operation": name, "returncode": returncode,
                              **({} if returncode is not None else {"error_type": "missing_exit_code"})})
        return result

    commands.append(invoke("write_flash", ["--after", "no_reset", "write_flash", "--flash_size", "16MB",
                                             "0", str(full)], 1200))
    commands.append(invoke("full_readback", ["--after", "no_reset", "read_flash", "0", hex(h32.FLASH), str(readback)], 1200))
    commands.append(invoke("partition_readback", ["--after", "no_reset", "read_flash", hex(h32.PT_OFF), hex(h32.PT_SIZE), str(pt)], 60))
    nv = baseline["nvs"]
    commands.append(invoke("nvs_readback", ["--after", "no_reset", "read_flash", hex(nv["offset"]),
                                              hex(nv["bytes"]), str(nvs)], 60))
    readback_ok = _recovery_artifact_matches(readback, h32.FLASH, current["full_sha256"])
    initial_readback_ok = readback_ok
    if not readback_ok:
        # A timed-out/failed write is uncertain. Once recovery is underway,
        # make one best-effort complete rewrite and verify it independently.
        commands.append(invoke("recovery_retry_write_flash", ["--after", "no_reset", "write_flash",
                                                               "--flash_size", "16MB", "0", str(full)], 1200))
        commands.append(invoke("recovery_retry_full_readback", ["--after", "no_reset", "read_flash",
                                                                  "0", hex(h32.FLASH), str(readback)], 1200))
        readback_ok = _recovery_artifact_matches(readback, h32.FLASH, current["full_sha256"])
    if not readback_ok:
        stop("full current BOX restore/readback differs after best-effort retry")
    if not _recovery_artifact_matches(pt, h32.PT_SIZE, current["partition_sha256"]) or \
       h32.region(readback, h32.PT_OFF, h32.PT_SIZE) != pt.read_bytes() or \
       h32.partitions(pt.read_bytes()) != baseline["partition_table"]["entries"]:
        stop("restored partition table differs")
    if not _recovery_artifact_matches(nvs, nv["bytes"], current["nvs_sha256"]) or \
       h32.region(readback, nv["offset"], nv["bytes"]) != nvs.read_bytes():
        stop("restored current NVS differs")
    for app in current["apps"]:
        data = h32.region(readback, app["offset"],
                          next(p["size"] for p in baseline["apps"] if p["name"] == app["name"]))
        if h32.dh(data) != app["sha256"] or h32.app_desc(data) != app["descriptor"]:
            stop("restored original application descriptor differs")
    live = _verify_recovery_device(port, baseline)
    recovery_processes.extend(live["processes"])
    process_codes.extend(recovery_processes)
    boot = _boot_original(port, run_dir, serial_module, baseline)
    all_processes_checked = bool(process_codes) and all(row["returncode"] == 0 for row in process_codes) and initial_readback_ok
    boot_ok = all(boot.get(key) is True for key in
                  ("project_match", "version_match", "sdk_match", "elf_prefix_match",
                   "factory_offset_match", "startup_order_match", "app_main_seen")) and \
        boot.get("panic_seen") is False and boot.get("watchdog_seen") is False
    device_binding_ok = live["device"]["fingerprint_sha256"] == current["device_fingerprint_sha256"]
    proof = {"schema": 1, "status": "verified" if all_processes_checked and boot_ok and device_binding_ok else "failed",
             "full_image_readback": True, "initial_full_image_readback_match": initial_readback_ok,
             "nvs_readback": True, "partition_match": True,
             "app_descriptor_match": True, "device_binding_match": device_binding_ok,
             "original_project_match": boot["project_match"], "sdk_match": boot["sdk_match"],
             "elf_prefix_match": boot["elf_prefix_match"], "panic_seen": boot["panic_seen"],
             "watchdog_seen": boot["watchdog_seen"], "usb_reset_code": boot["usb_reset_code"],
             "usb_reset_namespace": boot["usb_reset_namespace"],
             "process_exit_code": next((row["returncode"] for row in reversed(process_codes)
                                         if row["operation"] == "nvs_readback"), None),
             "process_exit_source": "checked_recovery_subprocesses",
             "process_exit_codes": process_codes, "full_sha256": current["full_sha256"],
             "nvs_sha256": current["nvs_sha256"], "boot": boot, "clocks": clocks.summary()}
    h32.atomic_json(run_dir / "restore-proof-private.json", proof, 0o600)
    if not proof["device_binding_match"]:
        stop("device identity differs after restore")
    if not boot_ok:
        stop("restored original application boot proof differs")
    if not all_processes_checked:
        stop("restore subprocess exit proof is incomplete or nonzero")
    return proof


def _recovery_artifact_matches(path: Path, byte_count: int, sha256: str) -> bool:
    """Validate one exact private recovery readback without trusting its filename."""
    return path.is_file() and path.stat().st_size == byte_count and h32.digest(path) == sha256


def _verify_recovery_device(port: str, baseline: dict) -> dict:
    """Bound each identity subprocess so four probes fit the frozen reserve."""
    try:
        chip = h32.esptool(port, ["--after", "no_reset", "chip_id"], 15, capture=True)
        flash = h32.esptool(port, ["--after", "no_reset", "flash_id"], 15, capture=True)
    except subprocess.CalledProcessError as exc:
        raise ValueError("recovery identity subprocess exited nonzero: " + str(exc.returncode)) from exc
    except BaseException as exc:
        raise ValueError("recovery identity subprocess failed: " + type(exc).__name__) from exc
    chip_code = getattr(chip, "returncode", None)
    flash_code = getattr(flash, "returncode", None)
    if chip_code != 0 or flash_code != 0:
        stop("recovery device identity subprocess failed")
    text = "\n".join((chip.stdout, chip.stderr, flash.stdout, flash.stderr))
    description = h32.field(r"Chip is\s+([^\r\n]+)", text, "chip")
    if "esp32-s3" not in description or \
       h32.field(r"Detected flash size:\s*([^\s]+)", text, "flash size") not in ("16mb", "16mib"):
        stop("recovery BOX geometry differs")
    device = {"chip": "esp32-s3", "chip_description": description,
              "mac": h32.field(r"MAC:\s*([0-9a-f:]{17})", text, "MAC"),
              "flash_manufacturer": h32.field(r"Manufacturer:\s*([^\r\n]+)", text, "manufacturer"),
              "flash_device": h32.field(r"Device:\s*([^\r\n]+)", text, "flash device"),
              "flash_bytes": h32.FLASH}
    device["fingerprint_sha256"] = h32.dh(json.dumps(device, sort_keys=True).encode())
    if device["fingerprint_sha256"] != baseline["device"]["fingerprint_sha256"]:
        stop("recovery BOX identity differs from baseline")
    return {"device": device, "processes": [
        {"operation": "recovery_chip_id", "returncode": chip_code},
        {"operation": "recovery_flash_id", "returncode": flash_code}]}


def _current_backup_review_request(run_dir: Path, meta: dict, intent: dict,
                                   proof: dict) -> dict:
    """Create the immutable facts an independent reviewer must approve."""
    proof_path = run_dir / "current-backup-private.json"
    proof_sha = h32.digest(proof_path)
    request = {"schema": 1, "status": "pending", "epoch": intent["epoch"],
               "intent_sha256": h32.dh(contract.canonical_intent_bytes(intent)),
               "manifest_sha256": h32.digest(run_dir / "build/manifest-private.json"),
               "proof_sha256": proof_sha, "full_sha256": proof["full_sha256"],
               "partition_sha256": proof["partition_sha256"],
               "nvs_sha256": proof["nvs_sha256"],
               "device_fingerprint_sha256": proof["device_fingerprint_sha256"],
               "reviewer": None}
    path = run_dir / "current-backup-review-request-private.json"
    if path.exists() or (run_dir / "current-backup-review-private.json").exists():
        stop("current-backup review request path already used")
    h32.atomic_json(path, request, 0o600)
    return request


def _wait_current_backup_review(run_dir: Path, request: dict,
                                clocks: RunClocks) -> dict:
    """Wait for a human-authored review bound to this exact fresh held backup."""
    review_path = run_dir / "current-backup-review-private.json"
    expected = {**request, "status": "approved", "reviewer": None}
    while True:
        clocks.require_new_work(PREFLASH_ALLOWANCE_MS + contract.CAPTURE_LIMITS["capture_wall_ms"])
        try:
            review = json.loads(review_path.read_text())
        except FileNotFoundError:
            time.sleep(.25)
            continue
        if not isinstance(review, dict):
            stop("current-backup reviewer approval is not an object")
        reviewer = review.get("reviewer")
        if review != {**expected, "reviewer": reviewer} or \
           not isinstance(reviewer, str) or not reviewer.strip():
            stop("current-backup approval does not bind the exact fresh backup")
        return review


def _existing_restore_target(baseline: dict) -> tuple[Path, dict]:
    """Use the validated immutable image; never describe it as a current backup."""
    full = Path(baseline["_directory"]) / "original-flash.bin"
    for app in baseline["apps"]:
        data = h32.region(full, app["offset"], app["size"])
        if h32.dh(data) != app["partition_sha256"] or h32.app_desc(data) != app["descriptor"]:
            stop("existing restore application metadata differs from validated full image")
    proof = {"schema": 1, "status": "verified", "restore_source": "existing_verified_baseline",
             "full_sha256": baseline["full"]["sha256"],
             "partition_sha256": baseline["partition_table"]["sha256"],
             "nvs_sha256": baseline["nvs"]["sha256"],
             "device_fingerprint_sha256": baseline["device"]["fingerprint_sha256"],
             "apps": [{"name": app["name"], "offset": app["offset"],
                       "descriptor": app["descriptor"], "sha256": app["partition_sha256"]}
                      for app in baseline["apps"]]}
    return full, proof


def run_epoch(port: str, run_dir: Path, baseline_dir: Path, *,
              now=time.monotonic, serial_importer=importlib.import_module,
              use_existing_restore_image: bool = False) -> dict:
    """Hardware path: callable only after source, build, and independent link gates."""
    run_dir = private_dir(run_dir)
    manifest = validate(run_dir, baseline_dir)
    _review_gate(run_dir, require_run(run_dir, baseline_dir)[0])
    _linked_review_gate(run_dir, manifest)
    capture_preflight(serial_importer)
    serial_module = serial_importer("serial")
    baseline = _backup(baseline_dir)
    baseline["_directory"] = str(baseline_dir.resolve())
    meta, intent = require_run(run_dir, baseline_dir)
    for name in ("run-attempt-private.json", "restore-proof-private.json",
                 "run-result-private.json"):
        if (run_dir / name).exists():
            stop("H38 mutation epoch already attempted")
    clocks = RunClocks(now)
    if RECOVERY_WORST_CASE_MS > contract.CAPTURE_LIMITS["restore_reserve_ms"]:
        stop("frozen recovery reserve is smaller than checked recovery allowance")
    h32.atomic_json(run_dir / "run-attempt-private.json", {
        "schema": 1, "epoch": intent["epoch"], "status": "attempted",
        "manifest_sha256": h32.digest(run_dir / "build/manifest-private.json"),
        "baseline_full_sha256": meta["baseline_full_sha256"],
        "restore_reserve_ms": contract.CAPTURE_LIMITS["restore_reserve_ms"],
        "started_unix": int(time.time())}, 0o600)
    current_full = None
    current_proof = None
    capture = None
    failure = None
    reset_result = "not_needed"
    reset_exit_code = None
    restore = None
    current_review_approved = False
    experiment_flash_started = False
    outside_interval_checked = False
    capture_abort_exception = None
    try:
        # The master clock already runs. All calls hold the chip in the loader.
        clocks.require_new_work(PREFLASH_ALLOWANCE_MS + contract.CAPTURE_LIMITS["capture_wall_ms"])
        if use_existing_restore_image:
            _bounded_verify_device(port, baseline, clocks,
                                   PREFLASH_ALLOWANCE_MS + contract.CAPTURE_LIMITS["capture_wall_ms"])
            current_full, current_proof = _existing_restore_target(baseline)
            h32.atomic_json(run_dir / "existing-restore-target-private.json", current_proof, 0o600)
        else:
            current_full, current_proof = _fresh_current_backup(port, run_dir, baseline, clocks)
            review_request = _current_backup_review_request(run_dir, meta, intent, current_proof)
            current_review = _wait_current_backup_review(run_dir, review_request, clocks)
            current_review_approved = True
            h32.atomic_json(run_dir / "current-backup-review-proof-private.json", {
                "schema": 1, "status": "approved", "reviewer": current_review["reviewer"],
                "request_sha256": h32.digest(run_dir / "current-backup-review-request-private.json"),
                "review_sha256": h32.digest(run_dir / "current-backup-review-private.json")}, 0o600)
        # The application flash itself is bounded to 20 minutes; reserve the
        # entire subsequent capture allowance before starting this mutation.
        clocks.require_new_work(PREFLASH_ALLOWANCE_MS + contract.CAPTURE_LIMITS["capture_wall_ms"])
        app = run_dir / "build/family_link_demo.bin"
        future = contract.CAPTURE_LIMITS["capture_wall_ms"]
        experiment_flash_started = True
        _bounded_esptool(port, ["--after", "no_reset", "write_flash", "--flash_size", "16MB",
                                hex(h32.APP_OFF), str(app)], clocks, future + 840_000, 180)
        app_readback = run_dir / "app-readback-private.bin"
        _bounded_esptool(port, ["--after", "no_reset", "read_flash", hex(h32.APP_OFF),
                                hex(app.stat().st_size), str(app_readback)], clocks, future + 660_000, 180)
        if app_readback.stat().st_size != app.stat().st_size or h32.digest(app_readback) != h32.digest(app):
            stop("H38 application flash readback differs")
        if not use_existing_restore_image:
            post = run_dir / "postflash-full-private.bin"
            _bounded_esptool(port, ["--after", "no_reset", "read_flash", "0", hex(h32.FLASH), str(post)],
                             clocks, future + 60_000, 600)
            old = current_full.read_bytes()
            new = post.read_bytes()
            erase_end = h32.APP_OFF + ((app.stat().st_size + 4095) // 4096) * 4096
            if len(new) != h32.FLASH or new[:h32.APP_OFF] != old[:h32.APP_OFF] or new[erase_end:] != old[erase_end:]:
                stop("H38 flash changed bytes outside reviewed application erase interval")
            outside_interval_checked = True
        _bounded_verify_device(port, baseline, clocks, future)
        try:
            capture = _capture_h38(port, run_dir, manifest, intent, serial_module, clocks)
        except CaptureTerminalError as exc:
            failure = exc
        except BaseException as exc:
            if clocks.capture_start is not None:
                if clocks.capture_end is None:
                    capture_abort_exception = type(exc).__name__
                    deadline = _wait_for_capture_abort(clocks)
                    failure = deadline if deadline is not None else exc
                else:
                    failure = exc
                raise failure
            else:
                raise
    except BaseException as exc:
        if failure is None:
            failure = exc
        if isinstance(exc, DeadlineError):
            try:
                # Only recovery-purpose loader reset after a wall timeout.
                reset_process = h32.esptool(port, ["--before", "default_reset", "--after", "no_reset", "chip_id"],
                                             60, capture=True)
                reset_exit_code = reset_process.returncode
                reset_result = "loader_reset_succeeded"
            except BaseException as reset_exc:
                reset_result = "loader_reset_failed:" + type(reset_exc).__name__
    finally:
        if current_full is not None and current_proof is not None:
            try:
                restore = _restore_current(port, run_dir, current_full, current_proof,
                                           baseline, serial_module, clocks)
            except BaseException as restore_exc:
                if failure is None:
                    failure = restore_exc
                h32.atomic_json(run_dir / "restore-failure-private.json", {
                    "status": "failed", "error_type": type(restore_exc).__name__,
                    "error": str(restore_exc), "clocks": clocks.summary()}, 0o600)
    summary = clocks.summary()
    status = "verified" if failure is None and capture and restore and summary["master_overrun_ms"] == 0 else "failed_or_incomplete"
    if summary["master_overrun_ms"] and failure is None:
        failure = DeadlineError("master_overrun")
    result = {"schema": 1, "epoch": intent["epoch"], "status": status,
              "capture": capture, "restore_verified": restore is not None,
              "trigger": None if failure is None else type(failure).__name__,
              "trigger_detail": None if failure is None else
                  (failure.name if isinstance(failure, DeadlineError) else type(failure).__name__),
              "trigger_detail_private": None if failure is None else str(failure),
              "reset_to_loader": reset_result,
              "reset_to_loader_process_exit_code": reset_exit_code,
              "current_backup_verified": current_proof is not None and not use_existing_restore_image,
              "restore_source": "existing_verified_baseline" if use_existing_restore_image else "fresh_current_backup",
              "current_content_preservation_waived": use_existing_restore_image,
              "outside_app_erase_interval_readback_checked": outside_interval_checked,
              "current_backup_review_approved": current_review_approved,
              "experiment_flash_started": experiment_flash_started,
              "capture_abort_exception": capture_abort_exception,
              "device_disposition": "restored_and_booted" if restore is not None else
                  (("loader_held_current_backup_unverified" if reset_result == "loader_reset_succeeded"
                    else "device_state_unknown_current_backup_unverified")
                   if current_proof is None and not experiment_flash_started else "recovery_unverified"),
              "clocks": clocks.summary()}
    h32.atomic_json(run_dir / "run-result-private.json", result, 0o600)
    if failure is not None:
        raise failure
    return {"status": "verified", "epoch": intent["epoch"],
            "restored": True, "profile": contract.PROFILE}


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
            cmd.add_argument("--prior-h38-run-dir", type=Path,
                             help="explicit independently reviewed actual changed-layout reference")
    run = sub.add_parser("run", help="execute one reviewed H38 mutation epoch")
    run.add_argument("--use-existing-restore-image", action="store_true",
                     help="operator waives current contents; restore validated existing full image")
    run.add_argument("--port", required=True)
    run.add_argument("--run-dir", type=Path, required=True)
    run.add_argument("--backup-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.action == "preflight":
            result = {"capture": capture_preflight(), "build_tools": build_tools_preflight()}
        elif args.action == "prepare":
            result = prepare(args.run_dir, args.backup_dir, args.h37_run_dir, args.prior_h38_run_dir)
        elif args.action == "build":
            result = build(args.run_dir, args.backup_dir)
        elif args.action == "run":
            result = run_epoch(args.port, args.run_dir, args.backup_dir,
                               use_existing_restore_image=args.use_existing_restore_image)
        else:
            result = validate(args.run_dir, args.backup_dir)
        print(json.dumps(result if args.action == "prepare" else
                         {"status": "ready", "epoch": result.get("epoch")}, sort_keys=True))
    except SystemExit:
        raise SystemExit("STOP: a required H38 gate or helper rejected the operation") from None
    except DeadlineError as exc:
        raise SystemExit("STOP: H38 deadline reached (" + exc.name + ")") from None
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        label = "subprocess failure" if isinstance(exc, subprocess.SubprocessError) else \
            "private evidence or validation failure" if isinstance(exc, (ValueError, KeyError)) else \
            "private file or I/O failure"
        raise SystemExit("STOP: H38 " + label + " (" + type(exc).__name__ + ")") from None


if __name__ == "__main__":
    main()
