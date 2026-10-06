#!/usr/bin/env python3
"""Host-only proof of the pinned FatFs H38 format footprint.

This compiles the actual ESP-IDF FatFs source and shared H38 disk guard against a
sparse mock disk and a test-only sdkconfig/RTOS shim. It is not firmware,
hardware, or DMA qualification; the mock cannot establish internal DMA placement.
"""
from __future__ import annotations

import hashlib
import json
import os
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "scripts/fixtures/h38_fatfs"
IDF = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf")).resolve()
FATFS = IDF / "components/fatfs/src"
SOURCE_NAMES = ("ff.c", "ff.h", "ffconf.h", "diskio.h")
GUARD = ROOT / "firmware/common/h38_disk_guard.c"
GUARD_HEADER = ROOT / "firmware/common/h38_disk_guard.h"
EXPECTED_IDF_TAG = "v5.4.2"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def stop(message: str) -> None:
    raise SystemExit(f"STOP: {message}")


def main() -> int:
    if not all((FATFS / name).is_file() for name in SOURCE_NAMES):
        stop(f"pinned FatFs source unavailable under {IDF}")
    version_file = IDF / "components/esp_common/include/esp_idf_version.h"
    if not version_file.is_file():
        stop("pinned ESP-IDF version header unavailable")
    version_text = version_file.read_text(errors="strict")
    if "ESP_IDF_VERSION_MAJOR   5" not in version_text or "ESP_IDF_VERSION_MINOR   4" not in version_text or "ESP_IDF_VERSION_PATCH   2" not in version_text:
        stop("pinned SDK is not ESP-IDF v5.4.2")

    compiler = shutil.which(os.environ.get("CC", "cc"))
    if not compiler:
        stop("host C compiler unavailable")
    compiler_version = subprocess.run(
        [compiler, "--version"], check=True, text=True, capture_output=True
    ).stdout.splitlines()[0]

    inputs = {name: sha256(FATFS / name) for name in SOURCE_NAMES}
    inputs["esp_idf_version.h"] = sha256(version_file)
    inputs["host_sdkconfig.h"] = sha256(FIXTURE / "sdkconfig.h")
    inputs["host_freertos.h"] = sha256(FIXTURE / "freertos/FreeRTOS.h")
    inputs["host_semphr.h"] = sha256(FIXTURE / "freertos/semphr.h")
    inputs["mock_driver.c"] = sha256(FIXTURE / "format_mock.c")
    inputs["h38_disk_guard.c"] = sha256(GUARD)
    inputs["h38_disk_guard.h"] = sha256(GUARD_HEADER)
    inputs["host_runner.py"] = sha256(Path(__file__).resolve())

    with tempfile.TemporaryDirectory(prefix="h38-fatfs-format-") as td:
        exe = Path(td) / "format_mock"
        dead_sections = ["-Wl,-dead_strip"] if platform.system() == "Darwin" else ["-Wl,--gc-sections"]
        command = [
            compiler,
            "-std=c11",
            "-O1",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-ffunction-sections",
            "-fdata-sections",
            "-I",
            str(FIXTURE),
            "-I",
            str(ROOT / "firmware/common"),
            "-I",
            str(FATFS),
            str(FATFS / "ff.c"),
            str(GUARD),
            str(FIXTURE / "format_mock.c"),
            *dead_sections,
            "-o",
            str(exe),
        ]
        build = subprocess.run(command, text=True, capture_output=True)
        if build.returncode:
            print(build.stdout, end="")
            print(build.stderr, end="")
            stop(f"actual pinned ff.c host compilation failed with exit {build.returncode}")
        run = subprocess.run([str(exe)], text=True, capture_output=True)
        if run.returncode:
            print(run.stdout, end="")
            print(run.stderr, end="")
            stop(f"actual pinned f_mkfs mock checks failed with exit {run.returncode}")
        try:
            fields = run.stdout.split()
            if not fields or fields[0] != "PASS":
                stop("mock harness did not report PASS")
            words = dict(item.split("=", 1) for item in fields[1:])
            expected = {
                "format_sectors": "2062",
                "format_bytes": str(2062 * 512),
                "fat_sectors": "1025",
                "data_clusters": "130811",
                "max_driver_call": "8",
                "callback_write_calls": "263",
                "callback_read_calls": "2",
                "max_callback_sectors": "8",
                "driver_calls": "265",
                "trim_intercepted": "1",
                "erase_interface": "absent",
                "negative_bounds": "pass",
                "negative_budget": "pass",
                "negative_read_budget": "pass",
                "split_8_plus_1": "pass",
                "partial_dispatch": "pass",
                "physical_bounds": "pass",
                "production_guard": "pass",
            }
        except ValueError:
            stop("mock harness emitted malformed output")
        if run.stdout.strip() != "PASS " + " ".join(f"{k}={v}" for k, v in expected.items()):
            stop("mock harness output differs from the reviewed expected footprint: " + run.stdout.strip())

    print(
        json.dumps(
            {
                "status": "pass",
                "evidence_scope": "host_mock_actual_pinned_fatfs_source_only",
                "firmware_qualification": False,
                "production_guard_qualification": False,
                "sdk": EXPECTED_IDF_TAG,
                "sdk_source_path_private": True,
                "host": platform.platform(),
                "compiler": compiler_version,
                "compile_exit_code": 0,
                "harness_exit_code": 0,
                "source_sha256": inputs,
                "profile": {
                    "virtual_disk_sectors": 1048576,
                    "sector_bytes": 512,
                    "format_flags": "FM_FAT32|FM_SFD",
                    "fat_count": 2,
                    "alignment_sectors": 1,
                    "allocation_unit_bytes": 4096,
                    "work_buffer_bytes": 4096,
                    "write_budget_bytes": 4194304,
                },
                "host_ffconf": {
                    "exfat": False,
                    "trim_compiled_in": True,
                    "lfn_mode": "none",
                    "note": "Formatter-footprint fixture only; compare every effective H38 FatFs setting before firmware build.",
                },
                "observed": expected,
            "note": "The actual shared guard is exercised with a mock backend; this does not qualify firmware, hardware, or DMA placement.",
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
