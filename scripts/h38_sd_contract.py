#!/usr/bin/env python3
"""Closed, offline H38 v1 intent, command, media-layout and capture contract.

This module performs no device I/O. Private hashes and payloads are kept in
returned objects only; callers must sanitize before publishing evidence.
"""
from __future__ import annotations

import base64
import hashlib
import json
import re
import struct
from dataclasses import dataclass
from functools import lru_cache
from typing import Any, Iterable, Mapping, Sequence

try:
    from .h37_sd_metadata import h35_cid_digest
except ImportError:
    from h37_sd_metadata import h35_cid_digest

SECTOR_BYTES = 512
VOLUME_START = 32_768
VOLUME_SECTORS = 1_048_576
VOLUME_END = VOLUME_START + VOLUME_SECTORS
# Published SENSOR accessory class: SDHC up to 32 GiB (512-byte sectors).
SDHC_MIN_SECTORS = VOLUME_END
SDHC_MAX_SECTORS = 67_108_864
# Reference capacity for offline synthetic fixtures (not a hardware constant).
REFERENCE_SDHC32_SECTORS = 62_586_880
# Retired 64 GB card; do not use for new intent or hardware.
RETIRED_64GB_SECTORS = 121_503_744
CARD_SECTORS = REFERENCE_SDHC32_SECTORS  # legacy alias for tests importing CARD_SECTORS
H35_REFERENCE_EPOCH_LEGACY = "a1617be8cb2d2343e744c31cc7d1b933"
H35_REFERENCE_EPOCH = H35_REFERENCE_EPOCH_LEGACY
MAX_COMMAND = 512
MAX_RECORD = 8192
PROFILE = "sdmmc_bounded_fat32_v1"
H35_REFERENCE_EPOCH = "a1617be8cb2d2343e744c31cc7d1b933"
EXIT_STATE = "retain_owned_verified"

HEX32 = re.compile(r"[0-9a-f]{32}\Z")
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
UINT = re.compile(r"(?:0|[1-9][0-9]*)\Z")
SINT = re.compile(r"(?:0|[1-9][0-9]*|-[1-9][0-9]*)\Z")


class ContractError(ValueError):
    """Invalid H38 intent, command, or capture."""


FORMAT = {
    "fat_type": "FAT32", "format_flags": 10, "fat_count": 2,
    "alignment_sectors": 1, "allocation_unit_bytes": 4096,
    "work_buffer_bytes": 4096, "read_limit_bytes": 16_777_216,
    "write_limit_bytes": 4_194_304, "time_limit_ms": 120_000,
}
IO = {
    "matrix_sizes_bytes": [65_536, 196_608], "iterations_per_size": 20,
    "seed_base": 1_048_576, "retained_indices": [0, 10, 19, 20, 30, 39],
    "probe_count": 5, "probe_size_bytes": 65_536, "probe_seed": 849_697_315,
    "replacement_target_size_bytes": 16_384, "replacement_target_seed": 6_356_993,
    "replacement_source_size_bytes": 24_576, "replacement_source_seed": 6_356_994,
    "driver_call_max_sectors": 8, "dma_buffer_bytes": 4096,
    "max_file_bytes": 196_608, "max_generated_logical_bytes": 33_554_432,
    "max_retained_file_bytes": 2_097_152, "read_limit_bytes": 67_108_864,
    "write_limit_bytes": 33_554_432, "time_limit_ms": 900_000,
    "file_transaction_ms": 30_000, "replacement_errno": 17,
    "remount_cycles": 5,
}
CAPTURE_LIMITS = {
    "command_line_max": 512, "record_max": 8192, "usb_rx_bytes": 512,
    "usb_tx_bytes": 8192, "session_ms": 1_200_000,
    "capture_wall_ms": 1_200_000, "flash_restore_ms": 5_400_000,
    "restore_reserve_ms": 2_700_000,
}


def _strict_int(v: Any, what: str, lo: int = 0, hi: int = (1 << 64) - 1) -> int:
    if type(v) is not int or not lo <= v <= hi:
        raise ContractError(f"{what} has invalid integer type or range")
    return v


def _closed(obj: Any, expected: Mapping[str, Any], what: str) -> None:
    if type(obj) is not dict or set(obj) != set(expected):
        raise ContractError(f"{what} fields are not the closed schema")
    for k, want in expected.items():
        got = obj[k]
        if type(want) is int:
            _strict_int(got, f"{what}.{k}")
        elif type(want) is list:
            if type(got) is not list or len(got) != len(want):
                raise ContractError(f"{what}.{k} has invalid array")
            for i, x in enumerate(got):
                _strict_int(x, f"{what}.{k}[{i}]")
        elif type(want) is str and (type(got) is not str or not got.isascii()):
            raise ContractError(f"{what}.{k} must be ASCII string")
        if got != want or type(got) is not type(want):
            raise ContractError(f"{what}.{k} differs from frozen profile")


def validate_intent(value: Any) -> dict[str, Any]:
    fields = {
        "schema", "epoch", "profile", "h35_reference_epoch", "private_cid_sha256",
        "old_mbr_sha256", "card_sector_bytes", "card_sector_count", "volume_start_lba",
        "volume_sector_count", "mbr_bytes", "format", "io", "capture_limits",
        "source_sdk_snapshot", "exit_state",
    }
    if type(value) is not dict or set(value) != fields:
        raise ContractError("intent fields are not the closed H38 schema")
    for key, pattern in (("epoch", HEX32), ("h35_reference_epoch", HEX32),
                         ("private_cid_sha256", HEX64), ("old_mbr_sha256", HEX64)):
        if type(value[key]) is not str or not pattern.fullmatch(value[key]):
            raise ContractError(f"intent {key} has invalid encoding")
    for k, expected in (("schema", "h38-intent-v1"), ("profile", PROFILE), ("exit_state", EXIT_STATE)):
        if type(value[k]) is not str or value[k] != expected:
            raise ContractError(f"intent {k} differs from frozen profile")
    if _strict_int(value["card_sector_bytes"], "card_sector_bytes") != SECTOR_BYTES:
        raise ContractError("intent card_sector_bytes differs from frozen profile")
    validate_card_sector_count(value["card_sector_count"])
    fixed_volume = {
        "volume_start_lba": VOLUME_START, "volume_sector_count": VOLUME_SECTORS,
        "mbr_bytes": 512,
    }
    for k, expected in fixed_volume.items():
        if _strict_int(value[k], k) != expected:
            raise ContractError(f"intent {k} differs from frozen volume geometry")
    _closed(value["format"], FORMAT, "format")
    _closed(value["io"], IO, "io")
    _closed(value["capture_limits"], CAPTURE_LIMITS, "capture_limits")
    sdk = value["source_sdk_snapshot"]
    if type(sdk) is not dict or set(sdk) != {"source_revision", "sdk_version"}:
        raise ContractError("source_sdk_snapshot fields are not closed")
    if type(sdk["sdk_version"]) is not str or sdk["sdk_version"] != "5.4.2":
        raise ContractError("SDK version differs from frozen profile")
    if type(sdk["source_revision"]) is not str or not HEX40.fullmatch(sdk["source_revision"]):
        raise ContractError("source revision has invalid encoding")
    return value


def _pairs_no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for key, value in pairs:
        if key in out:
            raise ContractError("duplicate JSON key")
        out[key] = value
    return out


def load_intent(raw: bytes) -> dict[str, Any]:
    if type(raw) is not bytes or len(raw) > 32_768:
        raise ContractError("intent must be bounded bytes")
    try:
        value = json.loads(raw.decode("ascii"), object_pairs_hook=_pairs_no_duplicates,
                           parse_constant=lambda _: (_ for _ in ()).throw(ContractError("invalid JSON number")))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ContractError("intent JSON invalid") from exc
    value = validate_intent(value)
    if canonical_intent_bytes(value) != raw:
        raise ContractError("intent JSON is not canonical serialization")
    return value


def canonical_intent_bytes(value: Mapping[str, Any]) -> bytes:
    validate_intent(dict(value))
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
                      allow_nan=False).encode("ascii")


def intent_sha256(value: Mapping[str, Any]) -> str:
    return hashlib.sha256(canonical_intent_bytes(value)).hexdigest()


def validate_card_sector_count(sectors: Any) -> int:
    count = _strict_int(sectors, "card_sector_count", SDHC_MIN_SECTORS, SDHC_MAX_SECTORS)
    return count


def geometry_record_fields(card_sector_count: int) -> dict[str, str]:
    count = validate_card_sector_count(card_sector_count)
    return {
        "sectors": str(count),
        "sector_bytes": str(SECTOR_BYTES),
        "capacity_bytes": str(count * SECTOR_BYTES),
        "bus_width": "4",
        "real_freq_khz": "20000",
        "ddr": "0",
    }


def build_mbr(epoch: str) -> bytes:
    if type(epoch) is not str or not HEX32.fullmatch(epoch):
        raise ContractError("invalid epoch for MBR")
    mbr = bytearray(SECTOR_BYTES)
    struct.pack_into("<I", mbr, 440, int(epoch[:8], 16))
    entry = bytes((0, 0xFE, 0xFF, 0xFF, 0x0C, 0xFE, 0xFF, 0xFF))
    mbr[446:454] = entry
    struct.pack_into("<II", mbr, 454, VOLUME_START, VOLUME_SECTORS)
    mbr[510:512] = b"\x55\xaa"
    return bytes(mbr)


def validate_mbr(raw: bytes, epoch: str, card_sector_count: int | None = None) -> None:
    expected = build_mbr(epoch)
    if type(raw) is not bytes or len(raw) != SECTOR_BYTES or raw != expected:
        raise ContractError("MBR differs from exact canonical sector")
    limit = validate_card_sector_count(card_sector_count or SDHC_MAX_SECTORS)
    start, count = struct.unpack_from("<II", raw, 454)
    if start > limit or count > limit - start or start + count != VOLUME_END:
        raise ContractError("MBR extent overflow or mismatch")


def validate_bpb(raw: bytes) -> None:
    if type(raw) is not bytes or len(raw) != 512 or raw[510:] != b"\x55\xaa":
        raise ContractError("BPB sector size/signature invalid")
    bps = struct.unpack_from("<H", raw, 11)[0]
    spc = raw[13]
    reserved = struct.unpack_from("<H", raw, 14)[0]
    fats = raw[16]
    roots = struct.unpack_from("<H", raw, 17)[0]
    total16 = struct.unpack_from("<H", raw, 19)[0]
    fatsz16 = struct.unpack_from("<H", raw, 22)[0]
    total32 = struct.unpack_from("<I", raw, 32)[0]
    fatsz32 = struct.unpack_from("<I", raw, 36)[0]
    root = struct.unpack_from("<I", raw, 44)[0]
    data_sectors = total32 - reserved - fats * fatsz32
    clusters = data_sectors // spc if spc else -1
    expected = (bps == 512 and spc == 8 and reserved == 32 and fats == 2 and roots == 0
                and total16 == 0 and fatsz16 == 0 and total32 == VOLUME_SECTORS
                and fatsz32 == 1025 and root == 2 and clusters == 130_811)
    if not expected:
        raise ContractError("BPB differs from pinned FAT32 geometry")


def _hex(s: str, pattern: re.Pattern[str], field: str) -> str:
    if type(s) is not str or not pattern.fullmatch(s):
        raise ContractError(f"{field} encoding invalid")
    return s


def bind_command(intent: Mapping[str, Any], elf_sha256: str) -> bytes:
    intent = validate_intent(dict(intent))
    elf = _hex(elf_sha256, HEX64, "ELF digest")
    fields = ("H38C", "1", "BIND", intent["epoch"], elf,
              intent["h35_reference_epoch"], intent["private_cid_sha256"],
              intent["old_mbr_sha256"], intent_sha256(intent))
    return _command(fields)


def phase_command(phase: str, epoch: str, elf_sha256: str) -> bytes:
    seqs = {"LAYOUT": 1, "FORMAT": 2, "IO": 3, "FINISH": 4}
    if phase not in seqs:
        raise ContractError("unknown H38 phase")
    _hex(epoch, HEX32, "epoch")
    _hex(elf_sha256, HEX64, "ELF digest")
    return _command(("H38C", "1", phase, epoch, elf_sha256, str(seqs[phase])))


def validate_command_sequence(commands: Sequence[bytes], intent: Mapping[str, Any], elf_sha256: str) -> None:
    """Validate exact BIND plus four immutable stop-and-wait commands."""
    i = validate_intent(dict(intent))
    expected = [bind_command(i, elf_sha256)] + [
        phase_command(p, i["epoch"], elf_sha256) for p in ("LAYOUT", "FORMAT", "IO", "FINISH")
    ]
    if type(commands) not in (list, tuple) or len(commands) != len(expected):
        raise ContractError("command cardinality invalid")
    if any(type(command) is not bytes or len(command) > MAX_COMMAND or not command.endswith(b"\n") for command in commands):
        raise ContractError("command transport framing invalid")
    if list(commands) != expected:
        raise ContractError("command order or binding differs from immutable intent")


def _command(parts: Sequence[str]) -> bytes:
    try:
        raw = (",".join(parts) + "\n").encode("ascii")
    except UnicodeEncodeError as exc:
        raise ContractError("command must be ASCII") from exc
    if len(raw) > MAX_COMMAND:
        raise ContractError("command exceeds frozen line limit")
    return raw


EVENT_FIELDS: dict[str, tuple[str, ...]] = {
    "BOOT": ("reset_reason",),
    "TRANSPORT": ("profile","backend","mode","console","usb_host","slot","width_requested","max_freq_khz","command_timeout_ms","command_line_max","record_max","session_ms","format_read_limit_bytes","format_write_limit_bytes","format_ms","io_read_limit_bytes","io_write_limit_bytes","io_ms","file_transaction_ms","capture_wall_ms","flash_restore_ms","max_driver_call_sectors","dma_buffer_bytes","max_file_bytes","max_generated_logical_bytes","max_retained_file_bytes","usb_rx_bytes","usb_tx_bytes"),
    "POWER": ("gpio","active_level","error"), "HOST": ("error",), "SLOT": ("error",), "CARD": ("error",),
    "GEOMETRY": ("sectors","sector_bytes","capacity_bytes","bus_width","real_freq_khz","ddr"),
    "CID_PRIVATE": ("mfg_id","oem_id","revision","serial","date","name_size","name_hex"),
    "READY": ("accepts",), "IDENTITY_MATCH": ("reference_epoch","match","error"),
    "LAYOUT_RESULT": ("physical_lba","write_sectors","write_bytes","write_count","readback_match","readback_sha256","readback_base64_private","trim_requests","erase_calls","status","error"),
    "FORMAT_START": ("volume_sectors","sector_bytes","fat_type","fat_count","allocation_unit_bytes","format_flags","work_buffer_bytes","write_limit_bytes","read_limit_bytes","time_limit_ms"),
    "FORMAT_RESULT": ("f_result","volume_sectors","sector_bytes","fat_type","fat_count","allocation_unit_bytes","cluster_count","fat_sectors","root_cluster_sectors","read_bytes","write_bytes","write_calls","max_call_sectors","trim_requests","erase_calls","bpb_valid","bpb_sha256","bpb_base64_private","elapsed_us","status","error"),
    "MOUNT_RESULT": ("kind","cycle","mounted","fs_type","sector_bytes","volume_sectors","allocation_unit_bytes","cluster_count","total_bytes","free_bytes","read_bytes","write_bytes","elapsed_us","status","error"),
    "IO_RESULT": ("index","size_bytes","seed","expected_sha256","actual_sha256","payload_write_bytes","payload_read_bytes","total_bytes","free_before","free_after","fflush_ok","fsync_ok","close_ok","rename_rc","rename_errno","checksum_match","retained","deleted","failure_op","open_us","write_us","fflush_us","fsync_us","close_us","rename_us","read_verify_us","delete_us","status","error"),
    "PROBE_RESULT": ("cycle","size_bytes","seed","verified","elapsed_us","status","error"),
    "RENAME_RESULT": ("old_target_valid","source_present","rename_rc","rename_errno","expected_errno","old_target_preserved","target_sha256","source_sha256","status","error"),
    "RETAINED_RESULT": ("cycle","index","size_bytes","seed","expected_sha256","actual_sha256","checksum_match","status","error"),
    "DIRECTORY_RESULT": ("entries","owned_parts","unknown_entries","cleanup_count","status","error"),
    "RECLAIM_RESULT": ("bytes_before","bytes_after","reclaimed_bytes","expected_bytes","residual_owned_files","status","error"),
    "CLEANUP": ("sd_unmount_attempted","sd_unmount_error","host_deinit_attempted","host_deinit_error","power_off_attempted","power_off_error"),
    "COMPLETE": ("result","failure_stage","error","command_count","read_bytes","write_bytes","mbr_write_count","format_write_bytes","io_write_bytes","trim_requests","erase_calls","out_of_bounds_attempts","io_file_count","retained_file_bytes","probe_count","mount_count","remount_count","retained_check_count","reclaim_status","sd_unmount_attempted","sd_unmount_error","host_deinit_attempted","host_deinit_error","power_off_attempted","power_off_error","bound","scope","media_writes"),
}

SUCCESS_EVENT_SCHEDULE = (
    ["BOOT","TRANSPORT","HOST","SLOT","CARD","GEOMETRY","CID_PRIVATE","READY","IDENTITY_MATCH",
     "LAYOUT_RESULT","FORMAT_START","FORMAT_RESULT","MOUNT_RESULT"]
    + ["IO_RESULT"] * 40
    + sum((["MOUNT_RESULT","PROBE_RESULT"] + ["RETAINED_RESULT"] * 6 for _ in range(5)), [])
    + ["DIRECTORY_RESULT","RENAME_RESULT","RECLAIM_RESULT","CLEANUP","COMPLETE"]
)

UINT_FIELDS = {"gpio","active_level","sectors","sector_bytes","capacity_bytes","bus_width","real_freq_khz","ddr","mfg_id","oem_id","revision","serial","date","name_size","physical_lba","write_sectors","write_bytes","write_count","trim_requests","erase_calls","volume_sectors","fat_count","allocation_unit_bytes","format_flags","work_buffer_bytes","write_limit_bytes","read_limit_bytes","time_limit_ms","f_result","cluster_count","fat_sectors","root_cluster_sectors","read_bytes","write_calls","max_call_sectors","elapsed_us","cycle","mounted","total_bytes","free_bytes","index","size_bytes","seed","payload_write_bytes","payload_read_bytes","total_bytes","free_before","free_after","fflush_ok","fsync_ok","close_ok","rename_errno","checksum_match","retained","deleted","open_us","write_us","fflush_us","fsync_us","close_us","rename_us","read_verify_us","delete_us","verified","expected_errno","old_target_valid","source_present","old_target_preserved","entries","owned_parts","unknown_entries","cleanup_count","bytes_before","bytes_after","reclaimed_bytes","expected_bytes","residual_owned_files","sd_unmount_attempted","host_deinit_attempted","power_off_attempted","command_count","mbr_write_count","format_write_bytes","io_write_bytes","out_of_bounds_attempts","io_file_count","retained_file_bytes","probe_count","mount_count","remount_count","retained_check_count","bound","media_writes"}
SINT_FIELDS = {"error","rename_rc","sd_unmount_error","host_deinit_error","power_off_error"}
BOOL_FIELDS = {"readback_match","bpb_valid","checksum_match","retained","deleted","fflush_ok","fsync_ok","close_ok","mounted","verified","old_target_valid","source_present","old_target_preserved","sd_unmount_attempted","host_deinit_attempted","power_off_attempted","bound"}
HASH_FIELDS = {"readback_sha256","bpb_sha256","expected_sha256","actual_sha256","target_sha256","source_sha256"}

TRANSPORT = {
    "profile":"sdmmc_bounded_fat32_v1","backend":"sdmmc","mode":"bounded_fat32","console":"usb_serial_jtag","usb_host":"disabled","slot":"0","width_requested":"4","max_freq_khz":"20000","command_timeout_ms":"1000","command_line_max":"512","record_max":"8192","session_ms":"1200000","format_read_limit_bytes":"16777216","format_write_limit_bytes":"4194304","format_ms":"120000","io_read_limit_bytes":"67108864","io_write_limit_bytes":"33554432","io_ms":"900000","file_transaction_ms":"30000","capture_wall_ms":"1200000","flash_restore_ms":"5400000","max_driver_call_sectors":"8","dma_buffer_bytes":"4096","max_file_bytes":"196608","max_generated_logical_bytes":"33554432","max_retained_file_bytes":"2097152","usb_rx_bytes":"512","usb_tx_bytes":"8192",
}


@dataclass(frozen=True)
class Record:
    event: str
    fields: Mapping[str, str]


@dataclass(frozen=True)
class ParsedCapture:
    epoch: str
    elf_sha256: str
    records: tuple[Record, ...]
    result: str
    raw_bytes: int


def _parse_record(line: bytes) -> Record:
    if len(line) > MAX_RECORD or not line.endswith(b"\n"):
        raise ContractError("record size or termination invalid")
    payload = line[:-1]
    if payload.endswith(b"\r"):
        payload = payload[:-1]
    if any(byte < 0x20 or byte == 0x7f for byte in payload):
        raise ContractError("record contains invalid control character")
    try:
        text = payload.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ContractError("record is not ASCII") from exc
    parts = text.split(",")
    if len(parts) < 5 or parts[:2] != ["H38", "1"] or parts[2] not in EVENT_FIELDS:
        raise ContractError("record header/event invalid")
    event = parts[2]
    expected = ("epoch", "elf_sha256", *EVENT_FIELDS[event])
    if len(parts) != 3 + len(expected):
        raise ContractError("record field count invalid")
    values: dict[str, str] = {}
    for token, key in zip(parts[3:], expected):
        if "=" not in token:
            raise ContractError("record field encoding invalid")
        name, value = token.split("=", 1)
        if name != key or (not value and key != "name_hex") or key in values:
            raise ContractError("record field order/value invalid")
        values[key] = value
        if key in UINT_FIELDS and not UINT.fullmatch(value):
            raise ContractError(f"{event}.{key} is not canonical unsigned decimal")
        if key in UINT_FIELDS and int(value) > (1 << 64) - 1:
            raise ContractError(f"{event}.{key} is outside uint64")
        if key in SINT_FIELDS and not SINT.fullmatch(value):
            raise ContractError(f"{event}.{key} is not canonical signed decimal")
        if key in SINT_FIELDS and not -(1 << 31) <= int(value) < (1 << 31):
            raise ContractError(f"{event}.{key} is outside signed int32")
        if key in BOOL_FIELDS and value not in ("0", "1"):
            raise ContractError(f"{event}.{key} is not boolean")
        if key in HASH_FIELDS and not HEX64.fullmatch(value):
            raise ContractError(f"{event}.{key} is not SHA-256")
    if not HEX32.fullmatch(values["epoch"]) or not HEX64.fullmatch(values["elf_sha256"]):
        raise ContractError("record identity encoding invalid")
    for key in ("status",):
        if key in values and values[key] not in ("ok", "failed"):
            raise ContractError(f"{event}.{key} enum invalid")
        if key in values and "error" in values:
            if values[key] == "ok" and int(values["error"]) != 0:
                raise ContractError(f"{event} successful status has nonzero error")
            if values[key] == "failed" and int(values["error"]) == 0:
                raise ContractError(f"{event} failed status lacks error")
    enums = {
        ("READY", "accepts"): {"BIND"},
        ("IDENTITY_MATCH", "match"): {"0", "1"},
        ("FORMAT_START", "fat_type"): {"FAT32"},
        ("FORMAT_RESULT", "fat_type"): {"FAT32"},
        ("MOUNT_RESULT", "kind"): {"initial", "remount"},
        ("IO_RESULT", "failure_op"): {"none","create","write","fflush","fsync","fclose","rename","readback","checksum","delete","free_space","retention"},
        ("COMPLETE", "result"): {"io_complete", "failed"},
        ("COMPLETE", "failure_stage"): {"none","resources","reset","power","host","slot","card","geometry","bind","layout","format","mount","io_create","io_write","io_fflush","io_fsync","io_fclose","io_rename","io_readback","io_checksum","io_delete","probe","semantics","remount","reclaim","budget","timeout","cleanup"},
        ("COMPLETE", "scope"): {"bounded_fat32_filesystem_io"},
        ("COMPLETE", "reclaim_status"): {"ok", "failed"},
    }
    for (ev, key), choices in enums.items():
        if event == ev and key in values and values[key] not in choices:
            raise ContractError(f"{event}.{key} enum invalid")
    if event == "MOUNT_RESULT" and values["fs_type"] not in ({"3"} if values["status"] == "ok" else {"0", "3"}):
        raise ContractError("MOUNT_RESULT.fs_type invalid for status")
    if event == "CID_PRIVATE":
        size = int(values["name_size"])
        if size > 16 or any(int(values[k]) > 0xFFFFFFFF for k in ("mfg_id","oem_id","revision","serial","date")) or len(values["name_hex"]) != size * 2 or not re.fullmatch(r"[0-9a-f]*", values["name_hex"]):
            raise ContractError("CID name encoding invalid")
    if event == "BOOT" and (not UINT.fullmatch(values["reset_reason"]) or int(values["reset_reason"]) > 15):
        raise ContractError("BOOT.reset_reason is outside pinned ESP-IDF enum")
    if event == "IO_RESULT" and values["rename_rc"] not in ("0", "-1"):
        raise ContractError("IO_RESULT.rename_rc invalid")
    if event == "POWER" and int(values["error"]) == 0:
        raise ContractError("POWER record must represent an actual error")
    return Record(event, values)


def _n(r: Record, field: str) -> int:
    return int(r.fields[field])


@lru_cache(maxsize=128)
def _pattern_sha256(size: int, seed: int) -> str:
    """H32 byte_at() pattern, reproduced for private hash validation."""
    h = hashlib.sha256()
    chunk = bytearray()
    for at in range(size):
        x = (seed ^ ((at * 2654435761) & 0xFFFFFFFF)) & 0xFFFFFFFF
        x ^= x >> 13
        x = (x * 1274126177) & 0xFFFFFFFF
        x ^= x >> 16
        chunk.append(x & 0xFF)
        if len(chunk) == 4096:
            h.update(chunk)
            chunk.clear()
    if chunk:
        h.update(chunk)
    return h.hexdigest()


def _check_write_call_bounds(byte_count: int, call_count: int, what: str) -> None:
    if byte_count % SECTOR_BYTES:
        raise ContractError(f"{what} byte charge is not sector aligned")
    if byte_count == 0:
        if call_count != 0:
            raise ContractError(f"{what} has calls without bytes")
        return
    minimum = (byte_count + 4095) // 4096
    maximum = byte_count // SECTOR_BYTES
    if not minimum <= call_count <= maximum:
        raise ContractError(f"{what} write-call count inconsistent with byte charge")


def _one(rows: Sequence[Record], event: str) -> Record:
    matches = [r for r in rows if r.event == event]
    if len(matches) != 1:
        raise ContractError(f"{event} cardinality invalid")
    return matches[0]


def parse_capture(raw: bytes, expected_epoch: str, expected_elf_sha256: str,
                  expected_intent: Mapping[str, Any] | None = None) -> ParsedCapture:
    if type(raw) is not bytes or len(raw) > 1_048_576:
        raise ContractError("capture exceeds private raw bound")
    _hex(expected_epoch, HEX32, "expected epoch")
    _hex(expected_elf_sha256, HEX64, "expected ELF")
    lines: list[bytes] = []
    for line in raw.splitlines(keepends=True):
        if line.startswith(b"H38,"):
            lines.append(line)
        elif line.startswith(b"H38") and not line.startswith(b"H38C,"):
            raise ContractError("malformed H38-like line")
    rows = tuple(_parse_record(line) for line in lines)
    if not rows or rows[-1].event != "COMPLETE":
        raise ContractError("partial capture lacks terminal COMPLETE")
    if any(r.fields["epoch"] != expected_epoch or r.fields["elf_sha256"] != expected_elf_sha256 for r in rows):
        raise ContractError("capture identity binding mismatch")
    events = [r.event for r in rows]
    complete = _one(rows, "COMPLETE")
    raw_lower = raw.lower()
    panic_markers = (b"guru meditation error", b"backtrace:", b"task watchdog got triggered",
                     b"interrupt wdt timeout", b"abort() was called", b"panic'ed")
    if any(marker in raw_lower for marker in panic_markers):
        raise ContractError("capture contains panic or watchdog evidence")
    if complete.fields["result"] == "failed":
        failure_intent = None
        if expected_intent is not None:
            failure_intent = validate_intent(dict(expected_intent))
            if failure_intent["epoch"] != expected_epoch:
                raise ContractError("failed capture intent epoch mismatch")
        if int(complete.fields["error"]) == 0 or complete.fields["failure_stage"] == "none":
            raise ContractError("failed terminal lacks failure")
        if any(e == "COMPLETE" for e in events[:-1]):
            raise ContractError("duplicate terminal COMPLETE")
        if events[0] != "BOOT" or events[-1] != "COMPLETE":
            raise ContractError("failed transcript lacks ordered startup/terminal prefix")
        base = events[:-1]
        if base and base[-1] == "CLEANUP":
            base = base[:-1]
        if base.count("POWER") > 1:
            raise ContractError("duplicate POWER failure record")
        if "POWER" in base:
            p = base.index("POWER")
            if p < 2 or ("HOST" in base and p > base.index("HOST")):
                raise ContractError("POWER failure record order invalid")
            base.pop(p)
        schedule = SUCCESS_EVENT_SCHEDULE[:-2]
        if len(base) > len(schedule) or base != schedule[:len(base)]:
            raise ContractError("failed transcript is not a valid event prefix")
        transport_rows = [r for r in rows if r.event == "TRANSPORT"]
        if transport_rows and dict((k, transport_rows[0].fields[k]) for k in TRANSPORT) != TRANSPORT:
            raise ContractError("failed capture transport profile mismatch")
        stage = complete.fields["failure_stage"]
        if stage == "resources":
            # Only this pre-transport-resource prefix can establish that no
            # card/host resource was acquired and therefore omit CLEANUP.
            if base != ["BOOT", "TRANSPORT"]:
                raise ContractError("resource failure is not the pre-resource prefix")
        elif stage in {"host","slot","card","power"}:
            corresponding = {"host":"HOST","slot":"SLOT","card":"CARD","power":"POWER"}[stage]
            match = [r for r in rows if r.event == corresponding]
            if len(match) != 1 or int(match[0].fields["error"]) == 0 or int(match[0].fields["error"]) != int(complete.fields["error"]):
                raise ContractError("failed terminal lacks matching nonzero resource error record")
        elif stage == "bind":
            match = [r for r in rows if r.event == "IDENTITY_MATCH"]
            if len(match) != 1 or match[0].fields["match"] != "0" or int(match[0].fields["error"]) == 0 or int(match[0].fields["error"]) != int(complete.fields["error"]):
                raise ContractError("bind failure lacks matching identity error")
        elif stage == "layout":
            match = [r for r in rows if r.event == "LAYOUT_RESULT"]
            if len(match) != 1 or match[0].fields["status"] != "failed" or int(match[0].fields["error"]) != int(complete.fields["error"]):
                raise ContractError("layout failure lacks matching result")
        elif stage == "format":
            match = [r for r in rows if r.event == "FORMAT_RESULT"]
            if len(match) != 1 or match[0].fields["status"] != "failed" or int(match[0].fields["error"]) != int(complete.fields["error"]):
                raise ContractError("format failure lacks matching result")
        elif stage in {"mount","remount"}:
            kind = "initial" if stage == "mount" else "remount"
            match = [r for r in rows if r.event == "MOUNT_RESULT" and r.fields["kind"] == kind and r.fields["status"] == "failed"]
            if not match or int(match[-1].fields["error"]) != int(complete.fields["error"]):
                raise ContractError("mount failure lacks matching result")
        elif stage == "probe":
            match = [r for r in rows if r.event == "PROBE_RESULT" and r.fields["status"] == "failed"]
            if not match or int(match[-1].fields["error"]) != int(complete.fields["error"]):
                raise ContractError("probe failure lacks matching result")
        elif stage == "reclaim":
            match = [r for r in rows if r.event == "RECLAIM_RESULT" and r.fields["status"] == "failed"]
            if not match or int(match[-1].fields["error"]) != int(complete.fields["error"]):
                raise ContractError("reclaim failure lacks matching result")
        elif stage == "cleanup":
            if "CLEANUP" not in events:
                raise ContractError("cleanup failure lacks actual cleanup error")
            if (base != SUCCESS_EVENT_SCHEDULE[:-2]
                    or complete.fields["command_count"] != "4"):
                raise ContractError("cleanup failure did not follow complete I/O and FINISH")
            cleanup_errors = [int(_one(rows,"CLEANUP").fields[k])
                              for k in ("sd_unmount_error","host_deinit_error","power_off_error")]
            first_cleanup_error = next((error for error in cleanup_errors if error != 0), 0)
            if first_cleanup_error == 0 or int(complete.fields["error"]) != first_cleanup_error:
                raise ContractError("cleanup failure lacks first actual cleanup error")
        elif stage.startswith("io_"):
            # Checked against the first IO_RESULT failure below.
            if not any(r.event == "IO_RESULT" and r.fields["failure_op"] != "none" for r in rows):
                raise ContractError("file-operation failure lacks IO_RESULT")
        elif stage == "semantics":
            direct = [r for r in rows if r.event in {"DIRECTORY_RESULT", "RENAME_RESULT", "RETAINED_RESULT"}
                      and r.fields["status"] == "failed" and int(r.fields["error"]) == int(complete.fields["error"])]
            io = [r for r in rows if r.event == "IO_RESULT" and r.fields["failure_op"] in {"free_space", "retention"}
                  and r.fields["status"] == "failed" and int(r.fields["error"]) == int(complete.fields["error"])]
            if not direct and not io:
                raise ContractError("semantics failure lacks matching fixture result")
        elif stage in {"reset", "geometry", "budget", "timeout"}:
            # These stages have no dedicated signed-error record in v1. A
            # terminal alone cannot prove the cause; keep the capture partial.
            return ParsedCapture(expected_epoch, expected_elf_sha256, rows, "incomplete", len(raw))
        else:
            raise ContractError("failure stage has no evidence rule")
        cleanup_incomplete = False
        if "CLEANUP" in events:
            cl = _one(rows, "CLEANUP").fields
            if any(cl[k] not in ("0","1") for k in ("sd_unmount_attempted","host_deinit_attempted","power_off_attempted")):
                raise ContractError("failed cleanup attempt flags invalid")
            for attempted, error in (("sd_unmount_attempted","sd_unmount_error"),
                                     ("host_deinit_attempted","host_deinit_error"),
                                     ("power_off_attempted","power_off_error")):
                if cl[attempted] == "0" and cl[error] != "0":
                    raise ContractError("cleanup error exists without attempt")
            for key in ("sd_unmount_attempted","sd_unmount_error","host_deinit_attempted","host_deinit_error","power_off_attempted","power_off_error"):
                if complete.fields[key] != cl[key]:
                    raise ContractError("failed COMPLETE cleanup counters mismatch")
            # A cleanup record is only failure evidence if its attempts cover
            # resources that the successful transcript prefix proves were
            # acquired. MOUNT_RESULT.mounted reports the adapter's live state,
            # including a failed remount after an earlier successful unmount.
            host_rows = [r for r in rows if r.event == "HOST"]
            slot_rows = [r for r in rows if r.event == "SLOT"]
            card_rows = [r for r in rows if r.event == "CARD"]
            host_acquired = (any(r.fields["error"] == "0" for r in host_rows)
                             or any(r.fields["error"] == "0" for r in slot_rows)
                             or any(r.fields["error"] == "0" for r in card_rows))
            power_configured = bool(host_rows or slot_rows or card_rows)
            mount_rows = [r for r in rows if r.event == "MOUNT_RESULT"]
            mounted = bool(mount_rows and mount_rows[-1].fields["mounted"] == "1")
            # POWER is emitted both when gpio_config fails and when the later
            # gpio_set_level fails. V1 cannot distinguish those resource
            # states, so retain that narrow ambiguity instead of guessing.
            power_state_ambiguous = stage == "power" and "POWER" in events
            required_attempts = {
                "sd_unmount_attempted": mounted,
                "host_deinit_attempted": host_acquired,
            }
            if not power_state_ambiguous:
                required_attempts["power_off_attempted"] = power_configured
            if any(cl[key] != ("1" if needed else "0")
                   for key, needed in required_attempts.items()):
                cleanup_incomplete = True
        else:
            cleanup_values = ("sd_unmount_attempted","sd_unmount_error","host_deinit_attempted","host_deinit_error","power_off_attempted","power_off_error")
            if any(complete.fields[k] != "0" for k in cleanup_values):
                raise ContractError("terminal claims cleanup without CLEANUP record")
            if stage != "resources":
                return ParsedCapture(expected_epoch, expected_elf_sha256, rows, "incomplete", len(raw))
        failed_ios = [r for r in rows if r.event == "IO_RESULT" and r.fields["failure_op"] != "none"]
        if failed_ios:
            first = failed_ios[0]
            mapped = {"create":"io_create","write":"io_write","fflush":"io_fflush","fsync":"io_fsync","fclose":"io_fclose","rename":"io_rename","readback":"io_readback","checksum":"io_checksum","delete":"io_delete","free_space":"semantics","retention":"semantics"}
            if (complete.fields["failure_stage"] != mapped[first.fields["failure_op"]]
                    or first.fields["status"] != "failed"
                    or int(complete.fields["error"]) != int(first.fields["error"])):
                raise ContractError("failed terminal does not preserve first I/O failure")
        witness = {
            "host": "HOST", "slot": "SLOT", "card": "CARD", "bind": "IDENTITY_MATCH",
            "layout": "LAYOUT_RESULT", "format": "FORMAT_RESULT", "mount": "MOUNT_RESULT",
            "remount": "MOUNT_RESULT", "probe": "PROBE_RESULT", "reclaim": "RECLAIM_RESULT",
        }.get(stage)
        if stage.startswith("io_") or (stage == "semantics" and failed_ios):
            witness = "IO_RESULT"
        if stage == "semantics" and not failed_ios:
            witness = next((r.event for r in reversed(rows) if r.event in {"DIRECTORY_RESULT", "RENAME_RESULT", "RETAINED_RESULT"}
                            and r.fields["status"] == "failed"), None)
        if witness is not None and (not base or base[-1] != witness):
            raise ContractError("failure witness is not the final operation before cleanup")
        if stage == "power" and ("POWER" not in events or any(e in events for e in ("HOST", "SLOT", "CARD"))):
            raise ContractError("power failure is not the final pre-host operation")
        if any(e in events for e in ("POWER", "HOST", "SLOT", "CARD")):
            if int(_one(rows, "BOOT").fields["reset_reason"]) not in {1, 3, 11}:
                raise ContractError("failed capture continued after an unapproved reset reason")
        # A terminal can identify only the first operation failure. Everything
        # before that matching witness must be successful; otherwise a later
        # matching error could conceal an earlier fault in the same prefix.
        if stage == "cleanup":
            witness_index = next(i for i, r in enumerate(rows) if r.event == "CLEANUP")
        elif stage == "power":
            witness_index = next(i for i, r in enumerate(rows) if r.event == "POWER")
        elif stage == "resources":
            witness_index = len(rows) - (1 if events[-1] == "COMPLETE" else 0)
        elif witness is not None:
            witness_index = max(i for i, r in enumerate(rows) if r.event == witness)
        else:
            witness_index = len(rows)
        for prior in rows[:witness_index]:
            fields = prior.fields
            if prior.event in {"HOST", "SLOT", "CARD"} and fields["error"] != "0":
                raise ContractError("failure terminal follows an earlier resource error")
            if prior.event == "IDENTITY_MATCH":
                if (fields["reference_epoch"] != H35_REFERENCE_EPOCH
                        or fields["match"] != "1" or fields["error"] != "0"):
                    raise ContractError("failure terminal follows an earlier identity failure")
            if "status" in fields and (fields["status"] != "ok" or int(fields["error"]) != 0):
                raise ContractError("failure terminal follows an earlier failed result")
            if prior.event == "LAYOUT_RESULT" and fields["readback_match"] != "1":
                raise ContractError("failure terminal follows an unsuccessful layout readback")
            if prior.event == "MOUNT_RESULT" and (fields["mounted"] != "1" or fields["fs_type"] != "3"):
                raise ContractError("failure terminal follows an unsuccessful mount")
            if prior.event == "IO_RESULT" and (fields["failure_op"] != "none" or fields["checksum_match"] != "1"):
                raise ContractError("failure terminal follows an unsuccessful file operation")
            if prior.event == "PROBE_RESULT" and fields["verified"] != "1":
                raise ContractError("failure terminal follows an unsuccessful probe")
            if prior.event == "RETAINED_RESULT" and fields["checksum_match"] != "1":
                raise ContractError("failure terminal follows an unsuccessful retained-file check")
            if prior.event == "FORMAT_RESULT" and fields["f_result"] != "0":
                raise ContractError("failure terminal follows an unsuccessful format result")
        geometry_rows = [r for r in rows if r.event == "GEOMETRY"]
        if geometry_rows and stage != "geometry" and failure_intent is not None:
            geom = geometry_rows[0].fields
            expected_geom = geometry_record_fields(failure_intent["card_sector_count"])
            if {k: geom[k] for k in expected_geom} != expected_geom:
                raise ContractError("failed capture card geometry mismatch")
        identity_rows = [r for r in rows if r.event == "IDENTITY_MATCH"]
        if identity_rows:
            ident = identity_rows[0].fields
            if failure_intent is not None and ident["reference_epoch"] != failure_intent["h35_reference_epoch"]:
                raise ContractError("failed capture H35 reference epoch mismatch")
            if failure_intent is not None and ident["reference_epoch"] != failure_intent["h35_reference_epoch"]:
                raise ContractError("failed capture identity reference does not match intent")
            if failure_intent is not None and ident["match"] == "1":
                cid_rows = [r for r in rows if r.event == "CID_PRIVATE"]
                if len(cid_rows) != 1:
                    raise ContractError("failed capture identity match lacks CID witness")
                cid = cid_rows[0].fields
                digest = h35_cid_digest(failure_intent["h35_reference_epoch"], {
                    "mfg_id": int(cid["mfg_id"]), "oem_id": int(cid["oem_id"]),
                    "revision": int(cid["revision"]), "serial": int(cid["serial"]),
                    "date": int(cid["date"]), "name_size": int(cid["name_size"]),
                    "name_hex": cid["name_hex"],
                })
                if digest != failure_intent["private_cid_sha256"]:
                    raise ContractError("failed capture private CID does not match intent")
        # Failed operations can terminate before later phases exist. Retain an
        # ordered partial transcript as failure evidence without success claims.
        result = "incomplete" if cleanup_incomplete else "failed"
        return ParsedCapture(expected_epoch, expected_elf_sha256, rows, result, len(raw))
    # Phase records have a constrained order; repeated fixture records are checked below.
    head = ["BOOT","TRANSPORT","HOST","SLOT","CARD","GEOMETRY","CID_PRIVATE","READY","IDENTITY_MATCH","LAYOUT_RESULT","FORMAT_START","FORMAT_RESULT","MOUNT_RESULT"]
    if events[:len(head)] != head:
        raise ContractError("capture header/phase ordering invalid")
    tail = ["DIRECTORY_RESULT","RENAME_RESULT","RECLAIM_RESULT","CLEANUP","COMPLETE"]
    if events[-len(tail):] != tail:
        raise ContractError("capture terminal ordering invalid")
    if any(e not in set(head + tail + ["IO_RESULT","PROBE_RESULT","RETAINED_RESULT","MOUNT_RESULT"]) for e in events):
        raise ContractError("unexpected record event")
    if "POWER" in events and events.index("POWER") > events.index("HOST"):
        raise ContractError("POWER record out of order")
    if events.count("POWER") > 1:
        raise ContractError("duplicate POWER record")
    if any(events.count(e) != 1 for e in ("BOOT","TRANSPORT","HOST","SLOT","CARD","GEOMETRY","CID_PRIVATE","READY","IDENTITY_MATCH","LAYOUT_RESULT","FORMAT_START","FORMAT_RESULT","DIRECTORY_RESULT","RENAME_RESULT","RECLAIM_RESULT","CLEANUP","COMPLETE")):
        raise ContractError("one-shot event cardinality invalid")
    if events != SUCCESS_EVENT_SCHEDULE:
        raise ContractError("matrix/remount/probe/retained event sequence invalid")
    tr = _one(rows, "TRANSPORT")
    if dict((k, tr.fields[k]) for k in TRANSPORT) != TRANSPORT:
        raise ContractError("transport profile mismatch")
    if complete.fields["scope"] != "bounded_fat32_filesystem_io":
        raise ContractError("completion scope mismatch")
    if complete.fields["failure_stage"] != "none" or int(complete.fields["error"]) != 0:
        raise ContractError("success terminal carries failure")
    if expected_intent is None:
        raise ContractError("successful capture requires validated intent binding")
    intent = validate_intent(dict(expected_intent))
    if intent["epoch"] != expected_epoch:
        raise ContractError("successful capture intent epoch mismatch")
    if "POWER" in events:
        raise ContractError("successful capture contains power failure record")
    for event in ("HOST", "SLOT", "CARD"):
        if _one(rows, event).fields["error"] != "0":
            raise ContractError(f"successful capture has {event} error")
    geom = _one(rows, "GEOMETRY").fields
    expected_geom = geometry_record_fields(intent["card_sector_count"])
    if {k: geom[k] for k in expected_geom} != expected_geom:
        raise ContractError("card geometry mismatch")
    ident = _one(rows, "IDENTITY_MATCH").fields
    if ident["reference_epoch"] != intent["h35_reference_epoch"] or ident["match"] != "1" or ident["error"] != "0":
        raise ContractError("H35 identity match evidence mismatch")
    cid = _one(rows, "CID_PRIVATE").fields
    cid_digest = h35_cid_digest(intent["h35_reference_epoch"], {
        "mfg_id": int(cid["mfg_id"]), "oem_id": int(cid["oem_id"]),
        "revision": int(cid["revision"]), "serial": int(cid["serial"]),
        "date": int(cid["date"]), "name_size": int(cid["name_size"]), "name_hex": cid["name_hex"],
    })
    if cid_digest != intent["private_cid_sha256"]:
        raise ContractError("private CID digest does not match bound intent")
    if int(_one(rows,"BOOT").fields["reset_reason"]) not in {1,3,11}:
        raise ContractError("successful capture reset cause is outside the approved set")
    layout, fmt, fresult = _one(rows,"LAYOUT_RESULT"), _one(rows,"FORMAT_START"), _one(rows,"FORMAT_RESULT")
    layout_bytes = base64.b64decode(layout.fields["readback_base64_private"], validate=True)
    validate_mbr(layout_bytes, expected_epoch, intent["card_sector_count"])
    if hashlib.sha256(layout_bytes).hexdigest() != layout.fields["readback_sha256"] or layout.fields["readback_match"] != "1":
        raise ContractError("MBR readback evidence mismatch")
    if (layout.fields["physical_lba"] != "0" or layout.fields["write_sectors"] != "1"
            or layout.fields["write_bytes"] != "512" or layout.fields["write_count"] != "1"
            or layout.fields["trim_requests"] != "0" or layout.fields["erase_calls"] != "0"
            or layout.fields["status"] != "ok" or layout.fields["error"] != "0"):
        raise ContractError("layout operation differs from one-sector profile")
    bpb = base64.b64decode(fresult.fields["bpb_base64_private"], validate=True)
    validate_bpb(bpb)
    if hashlib.sha256(bpb).hexdigest() != fresult.fields["bpb_sha256"]:
        raise ContractError("BPB digest mismatch")
    fixed_format = {"volume_sectors":"1048576","sector_bytes":"512","fat_type":"FAT32","fat_count":"2","allocation_unit_bytes":"4096","format_flags":"10","work_buffer_bytes":"4096","write_limit_bytes":"4194304","read_limit_bytes":"16777216","time_limit_ms":"120000"}
    if any(fmt.fields[k] != v for k,v in fixed_format.items()):
        raise ContractError("format request differs from frozen profile")
    if (fresult.fields["status"] != "ok" or int(fresult.fields["error"]) != 0 or int(fresult.fields["f_result"]) != 0
            or fresult.fields["fat_type"] != "FAT32" or int(fresult.fields["volume_sectors"]) != VOLUME_SECTORS
            or fresult.fields["sector_bytes"] != "512"
            or int(fresult.fields["cluster_count"]) != 130_811 or int(fresult.fields["fat_sectors"]) != 1025
            or int(fresult.fields["root_cluster_sectors"]) != 8 or int(fresult.fields["max_call_sectors"]) > 8
            or int(fresult.fields["write_bytes"]) > 4_194_304 or int(fresult.fields["read_bytes"]) > 16_777_216
            or int(fresult.fields["fat_count"]) != 2 or int(fresult.fields["allocation_unit_bytes"]) != 4096
            or int(fresult.fields["elapsed_us"]) > 120_000_000 or int(fresult.fields["write_calls"]) < 1
            or int(fresult.fields["erase_calls"]) != 0
            or fresult.fields["bpb_valid"] != "1"):
        raise ContractError("format result outside frozen bounds")
    _check_write_call_bounds(int(fresult.fields["write_bytes"]), int(fresult.fields["write_calls"]), "FORMAT_RESULT")
    if int(fresult.fields["max_call_sectors"]) < 1:
        raise ContractError("format reports no driver sector dispatch")
    mounts = [r for r in rows if r.event == "MOUNT_RESULT"]
    if len(mounts) != 6 or mounts[0].fields["kind"] != "initial" or mounts[0].fields["cycle"] != "0" or any(m.fields["fs_type"] != "3" for m in mounts):
        raise ContractError("mount cardinality/type invalid")
    for m in mounts:
        mf = m.fields
        if (mf["mounted"] != "1" or mf["status"] != "ok" or mf["error"] != "0"
                or mf["sector_bytes"] != "512" or mf["volume_sectors"] != "1048576"
                or mf["allocation_unit_bytes"] != "4096" or mf["cluster_count"] != "130811"
                or mf["total_bytes"] != "535801856" or int(mf["free_bytes"]) > 535801856
                or int(mf["free_bytes"]) % 4096 != 0
                or int(mf["read_bytes"]) % 512 != 0 or int(mf["write_bytes"]) % 512 != 0):
            raise ContractError("mount geometry or status mismatch")
    if int(fresult.fields["elapsed_us"]) > 120_000_000 or int(mounts[0].fields["elapsed_us"]) > int(fresult.fields["elapsed_us"]) or sum(int(m.fields["elapsed_us"]) for m in mounts[1:]) > 900_000_000:
        raise ContractError("mount phase time budget exceeded")
    remounts = [m for m in mounts if m.fields["kind"] == "remount"]
    if len(remounts) != 5 or [int(m.fields["cycle"]) for m in remounts] != list(range(5)):
        raise ContractError("remount sequence invalid")
    ios = [r for r in rows if r.event == "IO_RESULT"]
    if len(ios) != 40 or [int(r.fields["index"]) for r in ios] != list(range(40)):
        raise ContractError("matrix cardinality/order invalid")
    retained = [r for r in rows if r.event == "RETAINED_RESULT"]
    keep = [0,10,19,20,30,39]
    expected_pairs = [(c,i) for c in range(5) for i in keep]
    if len(retained) != 30 or [(int(r.fields["cycle"]),int(r.fields["index"])) for r in retained] != expected_pairs:
        raise ContractError("retained-file cardinality/order invalid")
    for i,r in enumerate(ios):
        size = 65_536 if i < 20 else 196_608
        iteration = i if i < 20 else i-20
        expected_hash = _pattern_sha256(size, 1_048_576 + size + iteration)
        if (int(r.fields["size_bytes"]) != size or int(r.fields["seed"]) != 1_048_576+size+iteration
                or r.fields["failure_op"] != "none" or r.fields["status"] != "ok" or int(r.fields["error"]) != 0
                or r.fields["expected_sha256"] != expected_hash or r.fields["actual_sha256"] != expected_hash or r.fields["checksum_match"] != "1"
                or r.fields["fflush_ok"] != "1" or r.fields["fsync_ok"] != "1" or r.fields["close_ok"] != "1"):
            raise ContractError("matrix row differs from generated fixture")
        if (int(r.fields["payload_write_bytes"]) != size or int(r.fields["payload_read_bytes"]) != size
                or int(r.fields["total_bytes"]) != 2 * size or r.fields["rename_rc"] != "0"
                or r.fields["rename_errno"] != "0"
                or int(r.fields["free_before"]) > 535801856 or int(r.fields["free_after"]) > 535801856
                or int(r.fields["free_before"]) % 4096 != 0 or int(r.fields["free_after"]) % 4096 != 0):
            raise ContractError("matrix byte or rename semantics mismatch")
        elapsed = sum(int(r.fields[k]) for k in ("open_us","write_us","fflush_us","fsync_us","close_us","rename_us","read_verify_us","delete_us"))
        if elapsed > 30_000_000:
            raise ContractError("matrix transaction time limit exceeded")
        is_kept = i in keep
        if (r.fields["retained"] == "1") != is_kept or (r.fields["deleted"] == "1") == is_kept:
            raise ContractError("matrix retention/deletion mismatch")
    for r in retained:
        i = int(r.fields["index"])
        source = ios[i]
        size = 65_536 if i < 20 else 196_608
        iteration = i if i < 20 else i-20
        expected_hash = _pattern_sha256(size, 1_048_576 + size + iteration)
        if (int(r.fields["size_bytes"]) != size or int(r.fields["seed"]) != 1_048_576+size+iteration
                or r.fields["expected_sha256"] != source.fields["expected_sha256"]
                or r.fields["expected_sha256"] != expected_hash or r.fields["actual_sha256"] != expected_hash or r.fields["checksum_match"] != "1"
                or r.fields["status"] != "ok" or int(r.fields["error"]) != 0):
            raise ContractError("retained-file integrity mismatch")
    probes = [r for r in rows if r.event == "PROBE_RESULT"]
    if len(probes) != 5 or [int(p.fields["cycle"]) for p in probes] != list(range(5)):
        raise ContractError("probe sequence invalid")
    if any(p.fields["size_bytes"] != "65536" or p.fields["seed"] != "849697315" or p.fields["verified"] != "1" or p.fields["status"] != "ok" or p.fields["error"] != "0" for p in probes):
        raise ContractError("probe fixture mismatch")
    io_elapsed = sum(sum(int(r.fields[k]) for k in ("open_us","write_us","fflush_us","fsync_us","close_us","rename_us","read_verify_us","delete_us")) for r in ios)
    io_elapsed += sum(int(r.fields["elapsed_us"]) for r in probes)
    io_elapsed += sum(int(r.fields["elapsed_us"]) for r in remounts)
    if io_elapsed > 900_000_000:
        raise ContractError("I/O phase record time exceeds frozen cap")
    ren = _one(rows,"RENAME_RESULT")
    if any(ren.fields[k] != "1" for k in ("old_target_valid","source_present","old_target_preserved")) or ren.fields["rename_rc"] != "-1" or ren.fields["rename_errno"] != "17" or ren.fields["expected_errno"] != "17" or ren.fields["status"] != "ok" or ren.fields["error"] != "0":
        raise ContractError("rename preservation fixture mismatch")
    if (ren.fields["target_sha256"] != _pattern_sha256(16_384, 0x610001)
            or ren.fields["source_sha256"] != _pattern_sha256(24_576, 0x610002)):
        raise ContractError("rename source/target bytes do not match H32 fixtures")
    directory = _one(rows,"DIRECTORY_RESULT")
    reclaim = _one(rows,"RECLAIM_RESULT")
    # H38 v1 has one 4096-byte owned stale .part, then ten retained owned files.
    if (directory.fields["status"] != "ok" or directory.fields["error"] != "0" or directory.fields["entries"] != "11" or directory.fields["owned_parts"] != "1"
            or directory.fields["unknown_entries"] != "0" or directory.fields["cleanup_count"] != "1"):
        raise ContractError("directory ownership/cleanup mismatch")
    if (reclaim.fields["status"] != "ok" or reclaim.fields["error"] != "0"
            or int(reclaim.fields["bytes_after"]) - int(reclaim.fields["bytes_before"]) != 4096 or reclaim.fields["reclaimed_bytes"] != "4096"
            or int(reclaim.fields["bytes_after"]) > 535801856 or int(reclaim.fields["bytes_before"]) > 535801856
            or int(reclaim.fields["bytes_after"]) % 4096 != 0 or int(reclaim.fields["bytes_before"]) % 4096 != 0
            or reclaim.fields["expected_bytes"] != "4096" or reclaim.fields["residual_owned_files"] != "10"):
        raise ContractError("reclamation evidence mismatch")
    cleanup = _one(rows,"CLEANUP")
    if any(cleanup.fields[k] != "1" for k in ("sd_unmount_attempted","host_deinit_attempted","power_off_attempted")) or any(cleanup.fields[k] != "0" for k in ("sd_unmount_error","host_deinit_error","power_off_error")):
        raise ContractError("cleanup evidence mismatch")
    counts = {"command_count":4,"mbr_write_count":1,"io_file_count":40,"probe_count":5,"mount_count":6,"remount_count":5,"retained_check_count":30}
    if any(int(complete.fields[k]) != n for k,n in counts.items()):
        raise ContractError("completion cardinality counters mismatch")
    if int(complete.fields["erase_calls"]) != 0 or int(complete.fields["out_of_bounds_attempts"]) != 0:
        raise ContractError("forbidden media operation reported")
    if int(complete.fields["trim_requests"]) < int(fresult.fields["trim_requests"]):
        raise ContractError("whole-run trim counter omits intercepted requests")
    if int(complete.fields["read_bytes"]) < sum(int(r.fields["payload_read_bytes"]) for r in ios):
        raise ContractError("whole-run read bytes omit matrix verification")
    if int(complete.fields["io_write_bytes"]) < sum(int(r.fields["payload_write_bytes"]) for r in ios):
        raise ContractError("I/O write bytes omit matrix payloads")
    initial_mount = mounts[0]
    if (int(initial_mount.fields["read_bytes"]) > int(fresult.fields["read_bytes"])
            or int(initial_mount.fields["write_bytes"]) > int(fresult.fields["write_bytes"])):
        raise ContractError("initial mount counter exceeds FORMAT aggregate")
    if complete.fields["reclaim_status"] != "ok" or complete.fields["bound"] != "1":
        raise ContractError("completion ownership/binding mismatch")
    for key in ("sd_unmount_attempted","sd_unmount_error","host_deinit_attempted","host_deinit_error","power_off_attempted","power_off_error"):
        if complete.fields[key] != cleanup.fields[key]:
            raise ContractError("COMPLETE cleanup counters disagree with CLEANUP record")
    if int(complete.fields["format_write_bytes"]) != int(fresult.fields["write_bytes"]):
        raise ContractError("format write counter mismatch")
    if int(complete.fields["media_writes"]) < 1 + int(fresult.fields["write_calls"]):
        raise ContractError("media write call count inconsistent")
    if int(complete.fields["write_bytes"]) != 512 + int(complete.fields["format_write_bytes"]) + int(complete.fields["io_write_bytes"]):
        raise ContractError("whole-run write bytes do not reconcile")
    _check_write_call_bounds(int(complete.fields["write_bytes"]), int(complete.fields["media_writes"]), "COMPLETE")
    for field in ("read_bytes","write_bytes","format_write_bytes","io_write_bytes"):
        if int(complete.fields[field]) % 512:
            raise ContractError(f"COMPLETE.{field} is not sector aligned")
    if int(fresult.fields["read_bytes"]) % 512:
        raise ContractError("FORMAT_RESULT read charge is not sector aligned")
    io_reads = int(complete.fields["read_bytes"]) - int(fresult.fields["read_bytes"]) - 1536
    io_writes = int(complete.fields["io_write_bytes"])
    remount_reads = sum(int(m.fields["read_bytes"]) for m in remounts)
    remount_writes = sum(int(m.fields["write_bytes"]) for m in remounts)
    if (int(complete.fields["format_write_bytes"]) > 4_194_304
            or int(complete.fields["io_write_bytes"]) > 33_554_432
            or int(fresult.fields["read_bytes"]) > 16_777_216
            or io_reads > 67_108_864
            or io_reads < remount_reads + sum(int(r.fields["payload_read_bytes"]) for r in ios)
                + 5 * sum(int(ios[i].fields["size_bytes"]) for i in (0,10,19,20,30,39))
                + 5 * 65_536 + 16_384 + 24_576
            or io_writes < remount_writes + sum(int(r.fields["payload_write_bytes"]) for r in ios)
                + 65_536 + 16_384 + 24_576 + 4096 + 512):
        raise ContractError("whole-run budgets exceeded")
    if int(complete.fields["retained_file_bytes"]) < 786_432 + 65_536 + 16_384 + 24_576 + 1 or int(complete.fields["retained_file_bytes"]) > 786_432 + 65_536 + 16_384 + 24_576 + 4096:
        raise ContractError("retained logical byte count outside profile")
    if (int(complete.fields["read_bytes"]) < int(fresult.fields["read_bytes"]) + sum(int(m.fields["read_bytes"]) for m in mounts[1:]) + 1536
            or int(complete.fields["write_bytes"]) < 512 + int(fresult.fields["write_bytes"]) + sum(int(m.fields["write_bytes"]) for m in mounts[1:])):
        raise ContractError("whole-run byte counters omit phase records")
    return ParsedCapture(expected_epoch, expected_elf_sha256, rows, "io_complete", len(raw))


def sanitized_summary(capture: ParsedCapture) -> dict[str, Any]:
    """Allowlisted summary, intentionally omitting private hashes and payloads."""
    if not isinstance(capture, ParsedCapture):
        raise ContractError("capture type invalid")
    return {"epoch": capture.epoch, "result": capture.result, "record_count": len(capture.records),
            "raw_capture_bytes": capture.raw_bytes, "scope": "bounded_fat32_filesystem_io"}
